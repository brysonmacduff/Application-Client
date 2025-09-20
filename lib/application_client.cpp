#include "application_client.h"

namespace InterProcessCommunication
{
ApplicationClient::~ApplicationClient()
{
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

    if(OpenConnection())
    {
        StartThreads();
    }

    return true;
}

bool ApplicationClient::RequestClose()
{
    if(GetClientState() != ClientState::CONNECTED)
    {
        return false;
    }

    SetClientState(ClientState::CLOSING);

    CloseSocket();

    // Signal the TX payload sender worker thread to wake up
    m_process_tx_payloads_semaphore.release();

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

void ApplicationClient::Clear()
{
    std::lock_guard<std::mutex> lock(m_tx_queue_mutex);
    m_tx_queue.clear();
}

void ApplicationClient::StartThreads()
{
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
}

void ApplicationClient::ExecuteErrorCallback(const Error& error, const std::optional<std::vector<char>>& tx_payload_opt)
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
    if(not OpenSocket())
    {
        return false;
    }

    if(not OpenConnection())
    {
        return false;
    }

    return true;
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
        const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to open socket! Error code: " + std::to_string(errno);
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
        const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to open socket! Error code: " + std::to_string(errno);
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
    close(m_client_file_descriptor);
    m_disconnected_callback();
}

void ApplicationClient::MonitorConnection()
{
    
}

void ApplicationClient::ProcessTxPayloads()
{
    while(GetClientState() != ClientState::CLOSING)
    {
        /* Wait here until signaled to resume: This happens in the following cases:
            1. The TX payload queue is no longer empty
            2. The socket is being closed
        */
        m_process_tx_payloads_semaphore.acquire();

        if(GetClientState() == ClientState::CLOSING)
        {
            break;
        }

        std::lock_guard<std::mutex> lock(m_tx_queue_mutex);

        while(not m_tx_queue.empty())
        {
            SendNextPayload();
        }
    }
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
    while(GetClientState() != ClientState::CLOSING)
    {
        std::vector<char> rx_buffer(RX_BUFFER_SIZE);
        std::span<char> rx_buffer_view(rx_buffer);

        const ssize_t read_bytes = read(m_client_file_descriptor, rx_buffer_view.data(), rx_buffer_view.size());

        if(read_bytes < 0)
        {
            if(GetClientState() == ClientState::CLOSING)
            {
                break;
            }

            const std::string error_message = std::string(CLASS_NAME) + "::" + __func__ + "() -> Failed to read!";
            perror(error_message.c_str());
            ExecuteErrorCallback(Error::SOCKET_READ_FAILURE, std::nullopt);

            continue;
        }

        m_rx_callback(rx_buffer_view);
    }
}

} // namespace InterProcessCommunication