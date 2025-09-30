#include "application_client.h"

namespace InterProcessCommunication
{
ApplicationClient::~ApplicationClient()
{
    SignalRxWorkerThreadShutdown();
    SignalTxWorkerThreadShutdown();
    SignalMonitorWorkerThreadShutdown();
    JoinThreads();
}

ApplicationClient::ApplicationClient(const std::string &ipv4_address, uint16_t port)
: m_endpoint(Endpoint{.socket_mode = SocketMode::TCP_IPV4, .ip_address = ipv4_address, .port = port})
{
}

ApplicationClient::ApplicationClient(const std::string &unix_socket_path)
: m_endpoint(Endpoint{.socket_mode = SocketMode::UNIX_DOMAIN, .unix_socket_path = unix_socket_path})
{
}

void ApplicationClient::SetConnectionCallback(ConnectedCallback callback)
{
    m_connected_callback = std::move(callback);
}

void ApplicationClient::SetDisconnectedCallback(DisconnectedCallback callback)
{
    m_disconnected_callback = std::move(callback);
}

void ApplicationClient::SetRxCallback(RxCallback callback)
{
    m_rx_callback = std::move(callback);
}

void ApplicationClient::SetErrorCallback(ErrorCallback callback)
{
    m_error_callback = std::move(callback);
}

bool ApplicationClient::Start()
{
    if(m_worker_threads_started)
    {
        return false;
    }

    SetMonitorWorkerThreadState(WorkerThreadState::STARTING);
    SetTxWorkerThreadState(WorkerThreadState::STARTING);
    SetRxWorkerThreadState(WorkerThreadState::STARTING);

    m_monitor_connection_thread = std::thread(&ApplicationClient::MonitorConnection, this);
    m_process_tx_payloads_thread = std::thread(&ApplicationClient::ProcessTxPayloads, this);
    m_process_rx_payloads_thread = std::thread(&ApplicationClient::ProcessRxPayloads, this);

    m_worker_threads_started = true;

    return m_worker_threads_started;
}

bool ApplicationClient::IsRunning() const
{
    return m_monitor_connection_thread_state == WorkerThreadState::RUNNING;
}

ClientState ApplicationClient::GetClientState() const
{
    std::shared_lock lock(m_client_state_mutex);
    return m_client_state;
}

bool ApplicationClient::RequestOpen()
{
    if(GetClientState() != ClientState::NOT_CONNECTED)
    {
        return false;
    }

    SetClientState(ClientState::OPENING);

    // Signal the connection monitor to open a connection
    m_monitor_connection_semaphore.release();

    return true;
}

bool ApplicationClient::RequestClose()
{
    if(GetClientState() != ClientState::CONNECTED)
    {
        return false;
    }

    SetClientState(ClientState::CLOSING);

    // Signal the connection monitor thread to close the socket
    m_monitor_connection_semaphore.release();

    return true;
}

bool ApplicationClient::EnqueuePayload(const std::span<char>& tx_bytes)
{
    if(tx_bytes.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_tx_queue_mutex);

    m_tx_queue.emplace_back(std::move(std::vector<char>(tx_bytes.begin(),tx_bytes.end())));

    // Signal the TX sender thread to resume
    m_process_tx_payloads_semaphore.release();

    return true;
}

void ApplicationClient::ClearOutboundPayloads()
{
    std::lock_guard<std::mutex> lock(m_tx_queue_mutex);
    m_tx_queue.clear();
}

void ApplicationClient::JoinThreads()
{
    if(m_monitor_connection_thread.joinable())
    {
        m_monitor_connection_thread.join();
    }
    
    if(m_process_rx_payloads_thread.joinable())
    {
        m_process_rx_payloads_thread.join();
    }

    if(m_process_tx_payloads_thread.joinable())
    {
        m_process_tx_payloads_thread.join();
    }

    m_worker_threads_started = false;
}

void ApplicationClient::SetMonitorWorkerThreadState(const WorkerThreadState &worker_thread_state)
{
    std::shared_lock lock(m_monitor_connection_thread_state_mutex);
    m_monitor_connection_thread_state = worker_thread_state;
}

ApplicationClient::WorkerThreadState ApplicationClient::GetMonitorWorkerThreadState() const
{
    std::shared_lock lock(m_monitor_connection_thread_state_mutex);
    return m_monitor_connection_thread_state;
}

void ApplicationClient::SignalMonitorWorkerThreadShutdown()
{
    SetMonitorWorkerThreadState(WorkerThreadState::ENDING);
    m_monitor_connection_semaphore.release();
}

void ApplicationClient::SetTxWorkerThreadState(const WorkerThreadState &worker_thread_state)
{
    std::shared_lock lock(m_process_tx_payloads_thread_state_mutex);
    m_process_tx_payloads_thread_state = worker_thread_state;
}

ApplicationClient::WorkerThreadState ApplicationClient::GetTxWorkerThreadState() const
{
    std::shared_lock lock(m_process_tx_payloads_thread_state_mutex);
    return m_process_tx_payloads_thread_state;
}

void ApplicationClient::SignalTxWorkerThreadShutdown()
{
    SetTxWorkerThreadState(WorkerThreadState::ENDING);
    m_process_tx_payloads_semaphore.release();
}

void ApplicationClient::SetRxWorkerThreadState(const WorkerThreadState &worker_thread_state)
{
    std::shared_lock lock(m_process_rx_payloads_thread_state_mutex);
    m_process_rx_payloads_thread_state = worker_thread_state;
}

ApplicationClient::WorkerThreadState ApplicationClient::GetRxWorkerThreadState() const
{
    std::shared_lock lock(m_process_rx_payloads_thread_state_mutex);
    return m_process_rx_payloads_thread_state;
}

void ApplicationClient::SignalRxWorkerThreadShutdown()
{
    SetRxWorkerThreadState(WorkerThreadState::ENDING);
}

void ApplicationClient::ExecuteErrorCallback(const Error &error, const std::optional<std::vector<char>> &tx_payload_opt)
{
    std::lock_guard<std::mutex> lock(m_error_callback_mutex);
    m_error_callback(error, tx_payload_opt);
}

void ApplicationClient::SetClientState(const ClientState &client_state)
{
    std::unique_lock lock(m_client_state_mutex);
    m_client_state = client_state;
}

bool ApplicationClient::OpenConnection()
{
    if(OpenSocket() && Connect())
    {
        SetClientState(ClientState::CONNECTED);
        m_connected_callback();
        return true;
    }

    return false;
}

bool ApplicationClient::OpenSocket()
{
    SetClientState(ClientState::OPENING);

    bool result = false;

    if(m_endpoint.socket_mode == SocketMode::TCP_IPV4)
    {
        result = OpenTcpIpv4Socket();
    }
    else if(m_endpoint.socket_mode == SocketMode::UNIX_DOMAIN)
    {
        result = OpenUnixDomainSocket();
    }

    return result;
}

bool ApplicationClient::OpenTcpIpv4Socket()
{
    int client_socket_fd = -1;

    client_socket_fd = socket(AF_INET, SOCK_STREAM, 0);

    if(client_socket_fd < 0)
    {
        const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to open socket! Error code: {" + std::to_string(errno) +"}";
        perror(error_message.c_str());
        ExecuteErrorCallback(Error::SOCKET_OPEN_FAILURE,std::nullopt);
        return false;
    }

    m_client_file_descriptor = client_socket_fd;
    return true;
}

bool ApplicationClient::OpenUnixDomainSocket()
{
    int client_socket_fd = -1;

    client_socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);

    if(client_socket_fd < 0)
    {
        const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to open socket! Error code: {" + std::to_string(errno) +"}";
        perror(error_message.c_str());
        ExecuteErrorCallback(Error::SOCKET_OPEN_FAILURE,std::nullopt);
        return false;
    }

    m_client_file_descriptor = client_socket_fd;
    return true;
}

bool ApplicationClient::Connect()
{
    bool result = false;

    if(m_endpoint.socket_mode == SocketMode::TCP_IPV4)
    {
        result = ConnectToTcpIpv4Address();
    }
    else if(m_endpoint.socket_mode == SocketMode::UNIX_DOMAIN)
    {
        result = ConnectToUnixDomainSocketAddress();
    }

    return result;
}

bool ApplicationClient::ConnectToTcpIpv4Address()
{
    sockaddr_in server_address {};
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(m_endpoint.port); // Server port
    inet_pton(AF_INET, m_endpoint.ip_address.c_str(), &server_address.sin_addr); // Server IP

    if (connect(m_client_file_descriptor, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) 
    {
        const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to connect to address: {"+m_endpoint.ip_address+":"+std::to_string(m_endpoint.port)+"}";
        perror(error_message.c_str());
        ExecuteErrorCallback(Error::SOCKET_CONNECT_FAILURE, std::nullopt);
        return false;
    }

    m_connected_callback();

    return true;
}

bool ApplicationClient::ConnectToUnixDomainSocketAddress()
{
    sockaddr_un server_address {};
    server_address.sun_family = AF_UNIX;
    strncpy(server_address.sun_path, m_endpoint.unix_socket_path.c_str(), sizeof(server_address.sun_path) - 1);

    inet_pton(AF_INET, m_endpoint.ip_address.c_str(), &server_address.sun_path); // server unix domain socket path

    if (connect(m_client_file_descriptor, (struct sockaddr*)&server_address, sizeof(server_address)) < 0) 
    {
        const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to connect to address: {"+m_endpoint.ip_address+"}";
        perror(error_message.c_str());
        ExecuteErrorCallback(Error::SOCKET_CONNECT_FAILURE, std::nullopt);
        return false;
    }

    m_connected_callback();

    return true;
}

void ApplicationClient::CloseSocket()
{
    shutdown(m_client_file_descriptor, SHUT_RDWR);
    close(m_client_file_descriptor);
    SetClientState(ClientState::NOT_CONNECTED);
    m_disconnected_callback();
}

void ApplicationClient::MonitorConnection()
{
    SetMonitorWorkerThreadState(WorkerThreadState::RUNNING);

    while(GetMonitorWorkerThreadState() != WorkerThreadState::ENDING)
    {
        /*
            Wait here for the following events:
                1. Open the socket and connect
                2. Close the socket
                3. The worker thread is being signaled to shutdown
        */
        m_monitor_connection_semaphore.acquire();

        if(GetMonitorWorkerThreadState() == WorkerThreadState::ENDING)
        {
            break;
        }

        if(GetClientState() == ClientState::OPENING)
        {
            // If the connection is succesful then the client state will transition to CONNECTED, which happens inside OpenConnection()
            if(not OpenConnection())
            {
                // If opening a connection fails, then close the file descriptor and transition back to the NOT_CONNECTED state, which happens inside CloseSocket()
                CloseSocket();
            }
        }
        else if(GetClientState() == ClientState::CLOSING)
        {
            CloseSocket();
        }
    }

    CloseSocket();

    SetMonitorWorkerThreadState(WorkerThreadState::INACTIVE);
}

void ApplicationClient::ProcessTxPayloads()
{
    SetTxWorkerThreadState(WorkerThreadState::RUNNING);

    while(GetTxWorkerThreadState() != WorkerThreadState::ENDING)
    {
        /* Wait here until signaled to resume: This happens in the following cases:
            1. The TX payload queue is no longer empty
            2. This worker thread is being told to shut down
        */
        m_process_tx_payloads_semaphore.acquire();

        if(GetTxWorkerThreadState() == WorkerThreadState::ENDING)
        {
            break;
        }

        std::lock_guard<std::mutex> lock(m_tx_queue_mutex);

        while(not m_tx_queue.empty())
        {
            SendNextPayload();
        }
    }

    SetTxWorkerThreadState(WorkerThreadState::INACTIVE);
}

bool ApplicationClient::SendNextPayload()
{
    std::vector<char> tx_payload = m_tx_queue.front();
    m_tx_queue.pop_front();
    std::span<char> tx_payload_view (tx_payload.begin(),tx_payload.end());

    while(not tx_payload_view.empty())
    {
        const ssize_t sent_bytes = send(m_client_file_descriptor,tx_payload_view.data(), tx_payload_view.size(),0);

        if(sent_bytes < 0)
        {
            const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to send payload!";
            perror(error_message.c_str());
            ExecuteErrorCallback(Error::SOCKET_SEND_FAILURE, tx_payload);
            return false;
        }

        tx_payload_view = tx_payload_view.subspan(sent_bytes,tx_payload_view.size()-sent_bytes);
    }
    
    return true;
}

void ApplicationClient::ProcessRxPayloads()
{
    SetRxWorkerThreadState(WorkerThreadState::RUNNING);

    while(GetRxWorkerThreadState() != WorkerThreadState::ENDING)
    {
        if(GetClientState() != ClientState::CONNECTED)
        {
            if(GetRxWorkerThreadState() == WorkerThreadState::ENDING)
            {
                break;
            }

            // FIXME: Implement a non-polling alternative so that this thread is resumed upon a connection being established
            std::this_thread::sleep_for(RX_CONNECTION_POLL_INTERVAL);

            continue;
        }

        std::vector<char> rx_buffer(RX_BUFFER_SIZE);
        std::span<char> rx_buffer_view(rx_buffer);

        const ssize_t read_bytes = recv(m_client_file_descriptor, rx_buffer_view.data(), rx_buffer_view.size(),0);

        if(read_bytes < 0)
        {
            const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to read!";
            perror(error_message.c_str());
            ExecuteErrorCallback(Error::SOCKET_READ_FAILURE, std::nullopt);

            continue;
        }

        m_rx_callback(rx_buffer_view);
    }

    SetRxWorkerThreadState(WorkerThreadState::INACTIVE);
}

} // namespace InterProcessCommunication