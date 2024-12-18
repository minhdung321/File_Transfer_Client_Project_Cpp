#ifndef FILE_TRANSFER_MANAGER_H
#define FILE_TRANSFER_MANAGER_H

#include <network_connection.h>
#include <session_manager.h>
#include <progressbar_manager.h>
#include <encryption_handler.hpp>

#include <string>
#include <memory>
#include <filesystem>
namespace fs = std::filesystem;

class FileTransferClient
{
private:
	std::unique_ptr<NetworkConnection> m_connection;
	std::unique_ptr<SessionManager> m_session_manager;
	std::unique_ptr<ProgressBarManager> m_pb_manager; // Progress bar manager
	std::unique_ptr<security::datasecurity::integrity::MD5Handler> md5Handler;

public:
	FileTransferClient();

	NetworkConnection& GetConnection() { return *m_connection; }
	SessionManager& GetSessionManager() { return *m_session_manager; }

	bool UploadFile(const fs::path& file_path);
	bool DownloadFile(const std::string& file_name);

	bool UploadDirectory(const fs::path& dir_path);

	void CloseSession();
};

#endif // !FILE_TRANSFER_MANAGER_H