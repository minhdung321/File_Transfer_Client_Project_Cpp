#include <session_manager.h>
#include <algorithm>

SessionManager::SessionManager(NetworkConnection& connection)
	: m_connection(connection), m_session_id{}, m_username(), m_password()
{
}

SessionManager::~SessionManager()
{
}

bool SessionManager::PerformHandshake()
{
	PacketHandshakeRequest handshakeReq = { 1 };

	if (!m_connection.sendPacket(PacketType::HANDSHAKE_REQUEST, handshakeReq, m_session_id.data()))
	{
		throw std::runtime_error("Failed to send handshake request.");
	}

	PacketHeader responseHeader{};
	PacketHandshakeResponse response{};

	if (!m_connection.recvPacket(PacketType::HANDSHAKE_RESPONSE, responseHeader, response))
	{
		throw std::runtime_error("Failed to receive handshake response.");
	}

	std::cout << "Handshake successful. Server version: " << static_cast<int>(response.server_version) << std::endl;
	std::cout << "Server message: " << response.message << std::endl;

	return true;
}

bool SessionManager::PerformAuthentication(const std::string& username, const std::string& password)
{
	PacketAuthenticationRequest authReq;
	strncpy_s(authReq.username, username.c_str(), MAX_USERNAME_LENGTH);
	strncpy_s(authReq.password, password.c_str(), MAX_PASSWORD_LENGTH);

	if (!m_connection.sendPacket(PacketType::AUTHENTICATION_REQUEST, authReq))
	{
		throw std::runtime_error("Failed to send authentication request.");
	}

	PacketHeader header;
	PacketAuthenticationResponse authResp;

	if (!m_connection.recvPacket(PacketType::AUTHENTICATION_RESPONSE, header, authResp))
	{
		throw std::runtime_error("Failed to receive authentication response.");
	}

	if (authResp.authenticated)
	{
		std::copy(authResp.session_id, authResp.session_id + 16, m_session_id.begin());
		std::cout << "Authentication successful." << std::endl;
		std::cout << "Server message: " << authResp.message << std::endl;
		std::cout << "Session ID: ";
		for (int i = 0; i < 16; i++)
		{
			printf("%02X", m_session_id[i]);
		}
		std::cout << std::endl;
	}
	else
	{
		std::cerr << "Server message: " << authResp.message << std::endl;
	}

	return authResp.authenticated;
}

bool SessionManager::PerformReconnect()
{
	return false;
}

const uint8_t* SessionManager::GetSessionID() const
{
	return m_session_id.data();
}

void SessionManager::SetSessionID(const uint8_t* new_session_id)
{
	if (new_session_id)
	{
		std::copy(new_session_id, new_session_id + 16, m_session_id.begin());
	}
}


