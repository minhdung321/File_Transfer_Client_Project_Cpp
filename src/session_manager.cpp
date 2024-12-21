#include <session_manager.h>
#include <algorithm>

constexpr auto DEBUG_MODE = true;

SessionManager::SessionManager(NetworkConnection& connection)
	: m_connection(connection),
	m_session_id{},
	m_username(),
	m_password()
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

	if (DEBUG_MODE)
	{
		std::cout << "Handshake successful. Server version: " << static_cast<int>(response.server_version) << std::endl;
		std::cout << "Server message: " << response.message << std::endl;
	}

	return true;
}

bool SessionManager::PerformAuthentication(const std::string& username, const std::string& password)
{
	PacketAuthenticationRequest authReq;
	strncpy_s(authReq.username, username.c_str(), MAX_USERNAME_LENGTH);
	strncpy_s(authReq.password, password.c_str(), MAX_PASSWORD_LENGTH);

	if (!m_connection.sendPacket(PacketType::AUTHENTICATION_REQUEST, authReq, m_session_id.data()))
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

		m_username = username;
		m_password = password;

		if (DEBUG_MODE)
		{
			std::cout << "Authentication successful." << std::endl;
			std::cout << "Server message: " << authResp.message << std::endl;
			std::cout << "Session ID: ";
			for (int i = 0; i < 16; i++)
			{
				printf("%02X", m_session_id[i]);
			}
			std::cout << std::endl;
		}
	}
	else
	{
		if (DEBUG_MODE)
		{
			std::cerr << "Server message: " << authResp.message << std::endl;
		}
	}

	return authResp.authenticated;
}

bool SessionManager::PerformReconnect()
{
	if (m_session_id[0] == 0 && m_session_id[1] == 0 && m_session_id[2] == 0 && m_session_id[3] == 0)
	{
		throw std::runtime_error("Invalid session ID for reconnection.");
	}

	if (m_username.empty() || m_password.empty())
	{
		throw std::runtime_error("Invalid username or password for reconnection.");
	}

	if (!m_connection.Reconnect())
	{
		throw std::runtime_error("Failed to reconnect to the server.");
	}

	if (!PerformHandshake())
	{
		throw std::runtime_error("Failed to perform handshake after reconnecting.");
	}

	if (!PerformAuthentication(m_username, m_password))
	{
		throw std::runtime_error("Failed to authenticate after reconnecting.");
	}

	return true;
}

void SessionManager::ResetSession()
{
	m_session_id.fill(0);
	m_username.clear();
	m_password.clear();
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


