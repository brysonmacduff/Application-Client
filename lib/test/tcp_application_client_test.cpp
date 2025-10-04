#include "application_client.h"
#include <gtest/gtest.h>
#include <vector>

namespace InterProcessCommunication::Test
{

class TcpApplicationTest : public ::testing::Test
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
        const int m_server_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
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
            const int m_client_file_descriptor = accept(m_server_file_descriptor, nullptr, nullptr);

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
        const int m_server_file_descriptor = socket(AF_INET, SOCK_STREAM, 0);
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

        const int m_client_file_descriptor = accept(m_server_file_descriptor, nullptr, nullptr);

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

private:
    int m_client_file_descriptor { -1 };
    int m_server_file_descriptor { -1 };
};

TEST_F(TcpApplicationTest, ConnectAndDisconnect)
{
    const int connection_attempts = 1;
    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationTest::StartConnectionAccepterTcpServer, this, std::ref(server_running_semaphore), std::ref(server_shutdown_semaphore), connection_attempts);

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

TEST_F(TcpApplicationTest, ConnectAndDisconnectRepeatedly)
{
    const int connection_attempts = 3;
    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationTest::StartConnectionAccepterTcpServer, this, std::ref(server_running_semaphore), std::ref(server_shutdown_semaphore), connection_attempts);

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

TEST_F(TcpApplicationTest, SendSingleMessage)
{
    std::string message = "hello there";

    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_done_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationTest::StartMessageReceiverTcpServer, this, std::ref(server_running_semaphore), std::ref(server_done_semaphore), std::ref(server_shutdown_semaphore), message);

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

TEST_F(TcpApplicationTest, SendMultipleMessages)
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
    std::thread server_thread (&TcpApplicationTest::StartMessageReceiverTcpServer, this, std::ref(server_running_semaphore), std::ref(server_done_semaphore), std::ref(server_shutdown_semaphore), total_payload);

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

TEST_F(TcpApplicationTest, FailSendingEmptyMessage)
{   
    std::string empty_message;
    std::span<char> empty_message_view(empty_message);
    EXPECT_FALSE(m_client.EnqueuePayload(empty_message_view));
}

TEST_F(TcpApplicationTest, SendLargeMessage)
{
    const size_t message_size = 8192;
    std::string message (message_size,'x');

    std::binary_semaphore server_running_semaphore(0);
    std::binary_semaphore server_done_semaphore(0);
    std::binary_semaphore server_shutdown_semaphore(0);
    std::thread server_thread (&TcpApplicationTest::StartMessageReceiverTcpServer, this, std::ref(server_running_semaphore), std::ref(server_done_semaphore), std::ref(server_shutdown_semaphore), message);

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

TEST_F(TcpApplicationTest, FailSendingMessageBeforeConnecting)
{
    std::string message = "hello there";

    bool is_error_callback_activated = false;

    m_client.SetErrorCallback([&](Error error, std::optional<std::vector<char>> failed_tx_payload)
    {   
        EXPECT_EQ(Error::SOCKET_SEND_FAILURE, error);
        EXPECT_TRUE(failed_tx_payload.has_value());

        const std::vector<char> failed_tx_payload_vec = failed_tx_payload.value();
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

} // namespace InterProcessCommunication::Test