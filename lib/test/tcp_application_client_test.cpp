#include "application_client.h"
#include <gtest/gtest.h>
#include <vector>

namespace InterProcessCommunication::Test
{

class TcpApplicationClientTest : public ::testing::Test
{
public:
    static constexpr std::chrono::milliseconds CLIENT_STATE_POLL_INTERVAL { 10 };
    static constexpr int BUFFER_SIZE = 1024;
    const std::string IPV4_ADDRESS { "127.0.0.1" };
    const uint16_t PORT = 5000;
    ApplicationClient m_client {IPV4_ADDRESS, PORT};

    void TearDown() override 
    {
        close(m_client_file_descriptor);
        close(m_server_file_descriptor);
    }
    
    void StartConnectionAccepterTcpServer(std::binary_semaphore& server_running_semaphore, std::binary_semaphore& server_shutdown_semaphore, int connection_limit)
    {
        m_server_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_NE(m_server_file_descriptor, -1);
        sockaddr_in address{};

        // Force the port to be freed after use by the server
        const int server_socket_option = 1;
        setsockopt(m_server_file_descriptor, SOL_SOCKET, SO_REUSEADDR, &server_socket_option, sizeof(server_socket_option));
  
        address.sin_family = AF_INET;
        inet_pton(AF_INET, IPV4_ADDRESS.c_str(), &address.sin_addr);
        address.sin_port = htons(PORT);

        EXPECT_NE(bind(m_server_file_descriptor, (sockaddr*)&address, sizeof(address)),-1);
        EXPECT_NE(listen(m_server_file_descriptor, connection_limit),-1);

        // Signal to the test case that the server is ready to accept a connection
        server_running_semaphore.release();

        std::cout << "TCP_SERVER -> Server is listening for connection attempts...\n";

        for(int count = 0; count < connection_limit; ++count)
        {
            m_client_file_descriptor = accept(m_server_file_descriptor, nullptr, nullptr);

            EXPECT_NE(m_client_file_descriptor, -1);

            std::cout << "TCP_SERVER -> Accepted client connection: " << count+1 << "\n";
        }

        // Wait until signalled by the test case to close the server
        server_shutdown_semaphore.acquire();

        close(m_client_file_descriptor);
        close(m_server_file_descriptor);

        std::cout << "TCP_SERVER -> Server has shutdown.\n";
    }

    void StartMessageReceiverTcpServer(std::binary_semaphore& server_running_semaphore, std::binary_semaphore& server_done_semaphore, std::binary_semaphore& server_shutdown_semaphore, std::string expected_payload)
    {
        const int connection_limit = 1;
        m_server_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_NE(m_server_file_descriptor, -1);
        sockaddr_in address{};

        // Force the port to be freed after use by the server
        const int server_socket_option = 1;
        setsockopt(m_server_file_descriptor, SOL_SOCKET, SO_REUSEADDR, &server_socket_option, sizeof(server_socket_option));
  
        address.sin_family = AF_INET;
        inet_pton(AF_INET, IPV4_ADDRESS.c_str(), &address.sin_addr);
        address.sin_port = htons(PORT);

        EXPECT_NE(bind(m_server_file_descriptor, (sockaddr*)&address, sizeof(address)),-1);
        EXPECT_NE(listen(m_server_file_descriptor, connection_limit),-1);

        // Signal to the test case that the server is ready to accept a connection
        server_running_semaphore.release();

        std::cout << "TCP_SERVER -> Server is listening for connection attempts...\n";

        m_client_file_descriptor = accept(m_server_file_descriptor, nullptr, nullptr);

        EXPECT_NE(m_client_file_descriptor, -1);

        std::cout << "TCP_SERVER -> Accepted client connection.\n";

        std::string received_payload;

        while(received_payload.size() < expected_payload.size())
        {
            std::vector<char> buffer(BUFFER_SIZE);
            std::span<char> buffer_view(buffer);

            const ssize_t bytes = read(m_client_file_descriptor, buffer_view.data(), buffer_view.size());

            EXPECT_GT(bytes, -1);

            const std::string received_message (buffer_view.data(), bytes);

            //std::cout << "TCP_SERVER -> Received message: {"+received_message+"}\n";

            received_payload += received_message;
        }

        EXPECT_EQ(received_payload, expected_payload);

        // signal the client that the server is done reading messages
        server_done_semaphore.release();

        // Wait until signalled by the test case to close the server
        server_shutdown_semaphore.acquire();

        close(m_client_file_descriptor);
        close(m_server_file_descriptor);

        std::cout << "TCP_SERVER -> Server has shutdown.\n";
    }

    void StartMessageSenderTcpServer(std::binary_semaphore& server_running_semaphore, std::binary_semaphore& server_shutdown_semaphore, std::string outbound_payload)
    {
        const int connection_limit = 1;
        m_server_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
        EXPECT_NE(m_server_file_descriptor, -1);
        sockaddr_in address{};

        // Force the port to be freed after use by the server
        const int server_socket_option = 1;
        setsockopt(m_server_file_descriptor, SOL_SOCKET, SO_REUSEADDR, &server_socket_option, sizeof(server_socket_option));
  
        address.sin_family = AF_INET;
        inet_pton(AF_INET, IPV4_ADDRESS.c_str(), &address.sin_addr);
        address.sin_port = htons(PORT);

        EXPECT_NE(bind(m_server_file_descriptor, (sockaddr*)&address, sizeof(address)),-1);
        EXPECT_NE(listen(m_server_file_descriptor, connection_limit),-1);

        // Signal to the test case that the server is ready to accept a connection
        server_running_semaphore.release();

        std::cout << "TCP_SERVER -> Server is listening for connection attempts...\n";

        m_client_file_descriptor = accept(m_server_file_descriptor, nullptr, nullptr);

        EXPECT_NE(m_client_file_descriptor, -1);

        std::cout << "TCP_SERVER -> Accepted client connection.\n";

        ssize_t bytes_sent = 0;

        while(bytes_sent < outbound_payload.size())
        {
            ssize_t remaining_bytes = outbound_payload.size() - bytes_sent;
            bytes_sent += send(m_client_file_descriptor, outbound_payload.data() + bytes_sent, remaining_bytes, 0);

            EXPECT_GT(bytes_sent, -1);
        }

        // Wait for the client to be done and signal the server
        server_shutdown_semaphore.acquire();

        close(m_client_file_descriptor);
        close(m_server_file_descriptor);

        std::cout << "TCP_SERVER -> Server has shutdown.\n";
    }

protected:
    int m_client_file_descriptor { -1 };
    int m_server_file_descriptor { -1 };
};

TEST_F(TcpApplicationClientTest, ConnectAndDisconnect)
{
    const int connection_attempts = 1;
    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationClientTest::StartConnectionAccepterTcpServer, this, std::ref(server_running_semaphore), std::ref(server_shutdown_semaphore), connection_attempts);

    EXPECT_TRUE(m_client.Start());

    // Do not proceed until the client is ready to begin
    while(not m_client.IsRunning())
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // Wait here until the server signals it is ready
    server_running_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestOpen());

    std::cout << __func__ << " -> Requested open!\n";

    while(m_client.GetClientState() != ClientState::CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::CONNECTED);

    std::cout << __func__ << " -> Requested close!\n";

    EXPECT_TRUE(m_client.RequestClose());

    while(m_client.GetClientState() != ClientState::NOT_CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::NOT_CONNECTED);

    std::cout << __func__ << " -> Releasing server!\n";

    // Signal the server that it is allowed to shutdown
    server_shutdown_semaphore.release();

    server_thread.join();
}

TEST_F(TcpApplicationClientTest, ConnectAndDisconnectRepeatedly)
{
    const int connection_attempts = 3;
    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationClientTest::StartConnectionAccepterTcpServer, this, std::ref(server_running_semaphore), std::ref(server_shutdown_semaphore), connection_attempts);

    EXPECT_TRUE(m_client.Start());

    // Do not proceed until the client is ready to begin
    while(not m_client.IsRunning())
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // Wait here until the server signals it is ready
    server_running_semaphore.acquire();

    for(int count = 0; count < connection_attempts; ++count)
    {
        EXPECT_TRUE(m_client.RequestOpen());

        std::cout << __func__ << " -> Requested open!\n";

        while(m_client.GetClientState() != ClientState::CONNECTED)
        {
            std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
        }

        EXPECT_EQ(m_client.GetClientState(), ClientState::CONNECTED);

        std::cout << __func__ << " -> Requested close!\n";

        EXPECT_TRUE(m_client.RequestClose());

        while(m_client.GetClientState() != ClientState::NOT_CONNECTED)
        {
            std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
        }

        EXPECT_EQ(m_client.GetClientState(), ClientState::NOT_CONNECTED);
    }

    std::cout << __func__ << " -> Releasing server!\n";

    // Signal the server that it is allowed to shutdown
    server_shutdown_semaphore.release();

    server_thread.join();
}

TEST_F(TcpApplicationClientTest, SendSingleMessage)
{
    std::string message = "hello there";

    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_done_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationClientTest::StartMessageReceiverTcpServer, this, std::ref(server_running_semaphore), std::ref(server_done_semaphore), std::ref(server_shutdown_semaphore), message);

    EXPECT_TRUE(m_client.Start());

    // Do not proceed until the client is ready to begin
    while(not m_client.IsRunning())
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // Wait here until the server signals it is ready
    server_running_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestOpen());

    while(m_client.GetClientState() != ClientState::CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::CONNECTED);

    const std::span<char> message_view (message);

    EXPECT_TRUE(m_client.EnqueuePayload(message_view));

    // wait for the server to signal that it is done reading
    server_done_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestClose());

    while(m_client.GetClientState() != ClientState::NOT_CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::NOT_CONNECTED);

    server_shutdown_semaphore.release();

    server_thread.join();
}

TEST_F(TcpApplicationClientTest, SendMultipleMessages)
{
    const size_t message_count = 100;
    std::vector<std::string> messages (message_count);
    std::string total_payload;

    for(size_t count = 0; count < message_count; ++count)
    {
        messages[count] = "<hello there " + std::to_string(count) + ">";
        total_payload += messages[count];
    }

    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_done_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationClientTest::StartMessageReceiverTcpServer, this, std::ref(server_running_semaphore), std::ref(server_done_semaphore), std::ref(server_shutdown_semaphore), total_payload);

    EXPECT_TRUE(m_client.Start());

    // Do not proceed until the client is ready to begin
    while(not m_client.IsRunning())
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // Wait here until the server signals it is ready
    server_running_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestOpen());

    while(m_client.GetClientState() != ClientState::CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::CONNECTED);

    for(std::string message : messages) 
    {
        const std::span<char> message_view (message);
        EXPECT_TRUE(m_client.EnqueuePayload(message_view));
    }

    // wait for the server to signal that it is done reading
    server_done_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestClose());

    while(m_client.GetClientState() != ClientState::NOT_CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::NOT_CONNECTED);

    server_shutdown_semaphore.release();

    server_thread.join();
}

TEST_F(TcpApplicationClientTest, FailSendingEmptyMessage)
{   
    std::string empty_message;
    std::span<char> empty_message_view(empty_message);
    EXPECT_FALSE(m_client.EnqueuePayload(empty_message_view));
}

TEST_F(TcpApplicationClientTest, SendLargeMessage)
{
    const size_t message_size = 8192;
    std::string message (message_size,'x');

    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_done_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationClientTest::StartMessageReceiverTcpServer, this, std::ref(server_running_semaphore), std::ref(server_done_semaphore), std::ref(server_shutdown_semaphore), message);

    EXPECT_TRUE(m_client.Start());

    // Do not proceed until the client is ready to begin
    while(not m_client.IsRunning())
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // Wait here until the server signals it is ready
    server_running_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestOpen());

    while(m_client.GetClientState() != ClientState::CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::CONNECTED);

    const std::span<char> message_view (message);

    EXPECT_TRUE(m_client.EnqueuePayload(message_view));

    // wait for the server to signal that it is done reading
    server_done_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestClose());

    while(m_client.GetClientState() != ClientState::NOT_CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::NOT_CONNECTED);

    server_shutdown_semaphore.release();

    server_thread.join();
}

TEST_F(TcpApplicationClientTest, FailSendingMessageBeforeConnecting)
{
    std::string message = "hello there";

    bool is_error_callback_activated = false;

    m_client.SetErrorCallback([&](const Error& error, const std::optional<std::span<char>>& failed_tx_payload)
    {   
        EXPECT_EQ(Error::SOCKET_SEND_FAILURE, error);
        EXPECT_TRUE(failed_tx_payload.has_value());

        const std::span<char> failed_tx_payload_vec = failed_tx_payload.value();
        const std::string failed_tx_payload_str(failed_tx_payload_vec.data(), failed_tx_payload_vec.size());

        EXPECT_EQ(failed_tx_payload_str, message);

        is_error_callback_activated = true;
    });

    EXPECT_TRUE(m_client.Start());

    // Do not proceed until the client worker threads are ready to begin
    while(not m_client.IsRunning())
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    const std::span<char> message_view (message);

    EXPECT_TRUE(m_client.EnqueuePayload(message_view));

    // wait for the error callback to be activated

    while(not is_error_callback_activated)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }
}

TEST_F(TcpApplicationClientTest, ReadLargeMessage)
{
    const size_t message_size = 8196;
    const std::string message (8196, 'x');

    bool is_rx_callback_activated = false;

    std::binary_semaphore callback_semaphore(0);
    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationClientTest::StartMessageSenderTcpServer, this, std::ref(server_running_semaphore), std::ref(server_shutdown_semaphore), message);

    size_t bytes_received = 0;
    std::string received_bytes;

    m_client.SetRxCallback([&](const std::span<char>& rx_payload_view)
    {
        const std::string rx_payload (rx_payload_view.data(), rx_payload_view.size());

        bytes_received += rx_payload.size();
        received_bytes += rx_payload;

        if(bytes_received == message.size())
        {
            // signal the gtest function body thread to resume
            callback_semaphore.release();
        }
    });

    EXPECT_TRUE(m_client.Start());

    // Do not proceed until the client worker threads are ready to begin
    while(not m_client.IsRunning())
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // wait here until the server is ready
    server_running_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestOpen());

    while(m_client.GetClientState() != ClientState::CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::CONNECTED);

    // wait for the RX callback to be activated
    callback_semaphore.acquire();

    EXPECT_TRUE(m_client.RequestClose());

    while(m_client.GetClientState() != ClientState::NOT_CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_EQ(m_client.GetClientState(), ClientState::NOT_CONNECTED);

    // tell the server it can now shutdown
    server_shutdown_semaphore.release();

    server_thread.join();

    EXPECT_EQ(message.size(), bytes_received);
    EXPECT_EQ(message, received_bytes);
}

TEST_F(TcpApplicationClientTest, ReconnectAfterServerDisconnect)
{
    const int connection_attempts = 2;
    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationClientTest::StartConnectionAccepterTcpServer, this, std::ref(server_running_semaphore), std::ref(server_shutdown_semaphore), connection_attempts);

    std::binary_semaphore callback_semaphore(0);
    const int expected_disconnect_count = 2;
    int disconnect_count = 0;

    m_client.SetDisconnectedCallback([&]()
    {
        // This callback will be called when the client properly closes itself after the connection is severed from the server side. After this, request the client to open.
        if(disconnect_count == 0)
        {
            EXPECT_TRUE(m_client.RequestOpen());
            callback_semaphore.release();
        }

        ++disconnect_count;
    });

    EXPECT_TRUE(m_client.Start());

    // Do not proceed until the client worker threads are ready to begin
    while(not m_client.IsRunning())
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // wait here until the server is ready
    server_running_semaphore.acquire();

    // connect to the server for the first time
    EXPECT_TRUE(m_client.RequestOpen());

    while(m_client.GetClientState() != ClientState::CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // After the client connects for the first time, sever the client connection by closing the client file descriptor from the server's side (the gtest)
    shutdown(m_client_file_descriptor,SHUT_RDWR);
    close(m_client_file_descriptor);

    // wait for the client to react to the disconnection
    callback_semaphore.acquire();

    while(m_client.GetClientState() != ClientState::CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    EXPECT_TRUE(m_client.RequestClose());

    while(m_client.GetClientState() != ClientState::NOT_CONNECTED)
    {
        std::this_thread::sleep_for(CLIENT_STATE_POLL_INTERVAL);
    }

    // tell the server it can now shutdown
    server_shutdown_semaphore.release();

    server_thread.join();

    EXPECT_EQ(disconnect_count, expected_disconnect_count);
}

} // namespace InterProcessCommunication::Test