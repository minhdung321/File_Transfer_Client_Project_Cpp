#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <network_connection.h>
#include <packet_helper.hpp>
#include <array>

class SessionManager
{
private:
	NetworkConnection& m_connection;
	std::array<uint8_t, 16> m_session_id;
	std::string m_username;
	std::string m_password;

public:
	SessionManager(NetworkConnection& connection);
	~SessionManager();

	bool PerformHandshake();
	bool PerformAuthentication(const std::string& username, const std::string& password);
	bool PerformReconnect();

	void ResetSession();

	const uint8_t* GetSessionID() const;
	void SetSessionID(const uint8_t* new_session_id);
};

#endif // !SESSION_MANAGER_H