
#include <vector>
#include <functional>
#include <span>
#include <list>
#include <optional>
#include <string>
#include <thread>
#include <shared_mutex>
#include <mutex>
#include <arpa/inet.h>
#include <sys/un.h>
#include <iostream>
#include <cstring>
#include <semaphore>

namespace InterProcessCommunication
{

enum class ClientState
{
    NOT_CONNECTED,
    OPENING,
    CONNECTED,
    CLOSING
};

enum class Error
{
    SOCKET_OPEN_FAILURE,
    SOCKET_CLOSE_FAILURE,
    SOCKET_SEND_FAILURE,
    SOCKET_READ_FAILURE,
    SOCKET_CONNECT_FAILURE
};

using ErrorCallback = std::function<void(const Error& error, const std::optional<std::vector<char>>& failed_tx_payload)>;
using ConnectedCallback = std::function<void()>;
using DisconnectedCallback = std::function<void()>;
using RxCallback = std::function<void(const std::span<char>& rx_bytes)>;

class ApplicationClient
{
public:

    ApplicationClient(const ApplicationClient&) = delete;
    ApplicationClient& operator=(const ApplicationClient&) = delete;
    ApplicationClient(ApplicationClient&&) = delete;
    ApplicationClient& operator=(ApplicationClient&&) = delete;
    ~ApplicationClient();
    ApplicationClient(const std::string& ipv4_address, uint16_t port);
    ApplicationClient(const std::string& unix_socket_path);

    void SetConnectionCallback(ConnectedCallback callback);
    void SetDisconnectedCallback(DisconnectedCallback callback);
    void SetRxCallback(RxCallback callback);
    void SetErrorCallback(ErrorCallback callback);

    /*
        \brief This function starts the worker threads that are responsible for:
            1. Managing a TCP client connection
            2. Sending messages
            3. Receiving messages
    */
    bool Start();
    /*
        \brief This function reports whether all of the worker threads are running and ready to do work
    */
    bool IsRunning() const;
    ClientState GetClientState() const;
    bool RequestOpen();
    bool RequestClose();
    bool EnqueuePayload(const std::span<char>& tx_bytes);
    void ClearOutboundPayloads();

private:

    const std::string_view CLASS_NAME = "ApplicationClient";

    enum class SocketMode
    {
        TCP_IPV4,
        UNIX_DOMAIN,
        UNDEFINED
    };

    struct Endpoint
    {
        SocketMode socket_mode = SocketMode::UNDEFINED;
        std::string ip_address = "0.0.0.0";
        uint16_t port = 0;
        std::string unix_socket_path = "/";
    };

    enum class WorkerThreadState
    {
        STARTING,
        RUNNING,
        ENDING,
        INACTIVE
    };

    static constexpr size_t RX_BUFFER_SIZE = 1024;

    Endpoint m_endpoint;
    ClientState m_client_state { ClientState::NOT_CONNECTED };

    mutable std::shared_mutex m_client_state_mutex;
    std::list<std::vector<char>> m_tx_queue;
    std::mutex m_tx_queue_mutex;
    ConnectedCallback m_connected_callback = [](){};
    DisconnectedCallback m_disconnected_callback = [](){};
    RxCallback m_rx_callback = [](const std::span<char>& rx_bytes){(void)rx_bytes;};
    ErrorCallback m_error_callback = [](const Error& error, const std::optional<std::vector<char>>& failed_tx_payload){(void)error; (void)failed_tx_payload;};
    std::mutex m_error_callback_mutex;
    int m_client_file_descriptor {-1};
    int m_server_file_descriptor {-1};

    bool m_worker_threads_started { false };

    std::thread m_monitor_connection_thread;
    std::binary_semaphore m_monitor_connection_semaphore {0};
    WorkerThreadState m_monitor_connection_thread_state { WorkerThreadState::INACTIVE };
    mutable std::shared_mutex m_monitor_connection_thread_state_mutex;

    std::thread m_process_rx_payloads_thread;
    
    std::thread m_process_tx_payloads_thread;
    std::binary_semaphore m_process_tx_payloads_semaphore {0};
    WorkerThreadState m_process_tx_payloads_thread_state { WorkerThreadState::INACTIVE };
    mutable std::shared_mutex m_process_tx_payloads_thread_state_mutex;

    void SetMonitorWorkerThreadState(const WorkerThreadState& worker_thread_state);
    WorkerThreadState GetMonitorWorkerThreadState() const;
    void SignalMonitorWorkerThreadShutdown();

    void SetTxWorkerThreadState(const WorkerThreadState& worker_thread_state);
    WorkerThreadState GetTxWorkerThreadState() const;
    void SignalTxWorkerThreadShutdown();

    void ExecuteErrorCallback(const Error& error, const std::optional<std::vector<char>>& tx_payload_opt);

    void SetClientState(const ClientState& client_state);
    bool OpenConnection();
    bool OpenSocket();
    bool OpenTcpIpv4Socket();
    bool OpenUnixDomainSocket();
    bool Connect();
    bool ConnectToTcpIpv4Address();
    bool ConnectToUnixDomainSocketAddress();
    void CloseSocket();

    /* WORKER THREADS */
    void MonitorConnection();
    void ProcessTxPayloads();
    void ProcessRxPayloads();

    void JoinThreads();

    bool SendNextPayload();
};
} // namespace InterProcessCommunication