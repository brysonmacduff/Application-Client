#include "application_client.h"
#include <gtest/gtest.h>

namespace InterProcessCommunication::Test
{

class TcpApplicationTest : public ::testing::Test
{
public:
    static constexpr std::chrono::milliseconds CLIENT_STATE_POLL_INTERVAL { 10 };
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

} // namespace InterProcessCommunication::Test