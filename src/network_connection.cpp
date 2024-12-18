#include <network_connection.h>

NetworkConnection::NetworkConnection()
	: m_socket(INVALID_SOCKET), m_is_connected(false)
{
	WSADATA wsa_data;
	if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
	{
		throw std::runtime_error("Error: WSAStartup failed.");
	}
}

NetworkConnection::~NetworkConnection()
{
	Disconnect();
	WSACleanup();
}

void NetworkConnection::Connect(const std::string& server_ip, uint16_t server_port)
{
	m_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (m_socket == INVALID_SOCKET) {
		throw std::runtime_error("Failed to create socket.");
	}

	sockaddr_in addr = {};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(server_port);
	inet_pton(AF_INET, server_ip.c_str(), &addr.sin_addr);

	if (connect(m_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		closesocket(m_socket);
		throw std::runtime_error("Failed to connect to server.");
	}

	m_is_connected = true;
	std::cout << "Connected to server at " << server_ip << ":" << server_port << std::endl;
}

bool NetworkConnection::Send(const std::vector<uint8_t>& data)
{
	if (!m_is_connected)
	{
		std::cerr << "Error: Not connected to server." << std::endl;
		return false;
	}

	size_t totalSent = 0;
	size_t dataSize = data.size();

	while (totalSent < dataSize)
	{
		int bytesToSend = static_cast<int>(std::min<size_t>(dataSize - totalSent, INT32_MAX));
		int bytesSent = send(m_socket,
			reinterpret_cast<const char*>(data.data() + totalSent),
			bytesToSend, 0);

		if (bytesSent == SOCKET_ERROR)
		{
			std::cerr << "Send failed: " << WSAGetLastError() << std::endl;
			return false;
		}
		else if (bytesSent == 0)
		{
			std::cerr << "Connection closed by peer." << std::endl;
			return false;
		}

		totalSent += bytesSent;
	}

	return true;
}

bool NetworkConnection::Receive(uint8_t* data, size_t size)
{
	if (!m_is_connected)
	{
		std::cerr << "Error: Not connected to server." << std::endl;
		return false;
	}

	size_t totalReceived = 0;

	while (totalReceived < size)
	{
		int bytesReceived = recv(m_socket,
			reinterpret_cast<char*>(data + totalReceived),
			static_cast<int>(size - totalReceived), 0);

		if (bytesReceived <= 0)
		{
			std::cerr << "Receive failed: " << WSAGetLastError() << std::endl;
			return false;
		}

		totalReceived += bytesReceived;
	}

	return true;
}

void NetworkConnection::Disconnect()
{
	if (m_socket != INVALID_SOCKET)
	{
		shutdown(m_socket, SD_BOTH);
		closesocket(m_socket);
		m_socket = INVALID_SOCKET;
	}
	m_is_connected = false;
}

bool NetworkConnection::IsConnected() const
{
	return m_is_connected;
}