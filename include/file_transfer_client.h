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
	std::unique_ptr<security::datasecurity::integrity::MD5Handler> md5_handler;

public:
	FileTransferClient();

	NetworkConnection& GetConnection() { return *m_connection; }
	SessionManager& GetSessionManager() { return *m_session_manager; }
	ProgressBarManager& GetProgressBarManager() { return *m_pb_manager; }

	bool UploadFile(const fs::path& file_path, const std::string& remote_path);
	bool DownloadFile(const std::string& file_name);

	bool UploadDirectory(const fs::path& dir_path, size_t total_files);
	bool ResumeUpload(const fs::path& file_path);

	bool ResumeDownload(const std::string& filename);

	bool GetServerFileList();

	void CloseSession();

private:
	struct FileEntry
	{
		std::string absolute_path;
		std::string relative_path;
		std::string file_name;
		uint64_t file_size;
		bool is_directory;

		FileEntry() : file_size(0), is_directory(false)
		{
		}

		FileEntry(const std::string& abs_path,
			const std::string& rel_path,
			const std::string& name,
			uint64_t size, bool is_dir,
			const uint8_t* checksum = nullptr)
			: absolute_path(abs_path),
			relative_path(rel_path),
			file_name(name),
			file_size(size),
			is_directory(is_dir)
		{
		}
	};
public:
	FileTransferClient();

	NetworkConnection& GetConnection() { return *m_connection; }
	SessionManager& GetSessionManager() { return *m_session_manager; }
	ProgressBarManager& GetProgressBarManager() { return *m_pb_manager; }

	bool UploadFile(const fs::path& file_path, const std::string& remote_path);
	bool DownloadFile(const std::string& file_name);

	std::vector<FileEntry> ScanDirectory(const fs::path& dir_path, size_t total_files);
	bool UploadDirectory(const fs::path& dir_path, size_t total_files);
	bool UploadDirectoryParallel(const fs::path& dir_path, size_t total_files);
	bool ResumeUpload(const fs::path& file_path);

	void CloseSession();
};

#endif // !FILE_TRANSFER_MANAGER_H