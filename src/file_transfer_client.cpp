#include <file_transfer_client.h>
#include <packet_helper.hpp>

#include <iostream>
#include <fstream>
#include <filesystem>
#include <future>
#include <thread>
#include <chrono>
namespace fs = std::filesystem;

constexpr auto CHECKSUM_FLAG = true;

FileTransferClient::FileTransferClient()
{
	m_connection = std::make_unique<NetworkConnection>();
	m_session_manager = std::make_unique<SessionManager>(*m_connection);
	m_pb_manager = std::make_unique<ProgressBarManager>();
}

std::string fileTypeToString(fs::file_type type)
{
	switch (type)
	{
	case fs::file_type::regular:
		return "regular";
	case fs::file_type::directory:
		return "directory";
	case fs::file_type::symlink:
		return "symlink";
	default:
		return "unknown"; // Nếu không phải các loại trên, trả về "unknown"
	}
}

bool FileTransferClient::UploadFile(const std::string& file_path)
{
	// Get the file size
	std::error_code ec;
	std::streamsize fileSize = fs::file_size(file_path, ec);

	if (ec)
	{
		throw std::runtime_error("Failed to get file size.");
	}

	std::cout << "File size: " << fileSize << " bytes (" << (fileSize / 1024) << " KB)" << std::endl;

	// Open the file and move to the end to get the size
	std::ifstream file(file_path, std::ios::binary);
	if (!file.is_open())
	{
		throw std::runtime_error("Failed to open file.");
	}

	// Calculate the checksum
	std::cout << "Calculating MD5 checksum..." << std::endl;
	std::vector<uint8_t> checksum = md5Handler->calcCheckSumFile(file_path);

	// Show MD5 checksum
	std::cout << "MD5 checksum: ";
	for (int i = 0; i < 16; i++)
	{
		printf("%02x", checksum[i]);
	}

	std::cout << std::endl;

	// Prepare the upload request
	PacketUploadRequest uploadReq(file_path, "File", fileSize, checksum.data());

	if (!m_connection->sendPacket(PacketType::UPLOAD_REQUEST, uploadReq))
	{
		throw std::runtime_error("Failed to send upload request.");
	}

	PacketHeader header;
	PacketUploadResponse uploadResp;

	if (!m_connection->recvPacket(PacketType::UPLOAD_RESPONSE, header, uploadResp))
	{
		throw std::runtime_error("Failed to receive upload response.");
	}

	if (uploadResp.status == UploadStatus::UPLOAD_ALLOWED)
	{
		std::cout << "The server has allowed the upload." << std::endl;
		std::cout << "File ID of this upload: " << uploadResp.upload_allowed.file_id << std::endl;
		std::cout << "Chunk size: " << uploadResp.upload_allowed.chunk_size << std::endl;
	}
	else
	{
		std::cerr << "The server has denied the upload." << std::endl;
		std::cerr << "Error message: " << uploadResp.out_of_space.message << std::endl;
	}

	// Upload the file
	size_t chunk_size = uploadResp.upload_allowed.chunk_size;
	size_t chunk_count = (fileSize + chunk_size - 1) / chunk_size;

	std::cout << "Starting to upload the file in " << chunk_count << " chunks." << std::endl;

	auto start_time = std::chrono::steady_clock::now();
	auto last_time = start_time;
	size_t last_sent = 0;
	double total_speed = 0.0;

	size_t total_sent = 0;

	const int MAX_RETRIES = 3;
	const int BASE_TIMEOUT = 1000; // 1 second

	m_pb_manager->AddFile(file_path);

	for (size_t i = 0; i < chunk_count; i++)
	{
		int retries = 0;
		bool chunk_sent = false;

		while (!chunk_sent && retries < MAX_RETRIES)
		{
			try
			{
				// Read the chunk data from the file
				size_t current_chunk_size = std::min<size_t>(chunk_size, fileSize - total_sent);

				std::vector<uint8_t> chunk_data(current_chunk_size);
				if (!file.read(reinterpret_cast<char*>(chunk_data.data()), current_chunk_size))
				{
					throw std::runtime_error("Failed to read file chunk.");
				}

				// Caclulate the checksum
				std::vector<uint8_t> checksum;
				if (CHECKSUM_FLAG)
				{
					checksum = md5Handler->calcCheckSum(chunk_data);
				}

				// Prepare the file chunk packet
				PacketFileChunk fileChunk(
					uploadResp.upload_allowed.file_id,				// File ID
					static_cast<uint32_t>(i),						// Chunk index
					static_cast<uint32_t>(current_chunk_size),		// Chunk size
					checksum.data(),								// Checksum
					reinterpret_cast<uint8_t*>(chunk_data.data())	// Chunk data
				);

				if (!m_connection->sendPacket(PacketType::FILE_CHUNK, fileChunk))
				{
					throw std::runtime_error("Failed to send file chunk.");
				}

				PacketHeader ackHeader;
				PacketFileChunkACK ack;

				if (!m_connection->recvPacket(PacketType::FILE_CHUNK_ACK, ackHeader, ack))
				{
					throw std::runtime_error("Failed to receive chunk acknowledgment.");
				}

				if (!ack.success ||
					ack.file_id != uploadResp.upload_allowed.file_id ||
					ack.chunk_index != i)
				{
					throw std::runtime_error("Chunk ACK validation failed.");
				}

				// Success
				chunk_sent = true;
				total_sent += current_chunk_size;

				// Tính tốc độ upload
				auto current_time = std::chrono::steady_clock::now();
				std::chrono::duration<double> elapsed = current_time - last_time;
				size_t bytes_sent_since_last = total_sent - last_sent;
				double seconds = elapsed.count();

				double speed_mbps = 0.0;
				if (seconds > 0)
				{
					speed_mbps = (bytes_sent_since_last * 8.0) / (seconds * 1000000.0); // Mbps
				}

				// Cập nhật thời gian và bytes gửi
				last_time = current_time;
				last_sent = total_sent;

				float progress = (static_cast<float>(total_sent) / fileSize) * 100.0f;
				m_pb_manager->UpdateProgress(file_path, progress);
			}
			catch (const std::exception& e)
			{
				retries++;
				std::cerr << "Attempt " << retries << " failed: " << e.what() << std::endl;

				if (retries >= MAX_RETRIES)
				{
					std::cerr << "Max retries reached. Aborting." << std::endl;
					return false;
				}

				// Exponential backoff
				int timeout = BASE_TIMEOUT * (1 << retries);
				std::cout << "Retrying in " << timeout << " ms..." << std::endl;
				std::this_thread::sleep_for(std::chrono::milliseconds(timeout));
			}
		}

		if (!chunk_sent)
		{
			std::cerr << "Failed to send chunk " << i << std::endl;
			return false;
		}
	}

	auto end_time = std::chrono::steady_clock::now();
	std::chrono::duration<double> total_duration = end_time - start_time;

	std::cout << std::endl
		<< "File uploaded successfully." << std::endl;
	std::cout << "Total time: " << std::fixed << std::setprecision(2) << total_duration.count() << " seconds" << std::endl;
	double avg_speed = (total_sent * 8.0) / (total_duration.count() * 1000000.0); // Mbps
	std::cout << "Average speed: " << std::fixed << std::setprecision(2) << avg_speed << " Mbps" << std::endl;

	return true;
}



bool FileTransferClient::DownloadFile(const std::string& file_name)
{
	// Prepare the download request
	PacketDownloadRequest p_request(file_name);

	if (!m_connection->sendPacket(PacketType::DOWNLOAD_REQUEST, p_request))
	{
		throw std::runtime_error("Failed to send download request.");
	}

	PacketHeader header;
	PacketDownloadResponse p_response;

	if (!m_connection->recvPacket(PacketType::DOWNLOAD_RESPONSE, header, p_response))
	{
		throw std::runtime_error("Failed to receive download response.");
	}

	if (p_response.status != DownloadStatus::FILE_FOUND)
	{
		if (p_response.status == DownloadStatus::FILE_ACCESS_DENIED)
			std::cerr << "Server denied the download. Message: " << p_response.error_info.message << std::endl;
		else if (p_response.status == DownloadStatus::FILE_NOT_FOUND)
			std::cerr << "Server does not find that file. Message: " << p_response.error_info.message << std::endl;
		return false;
	}

	std::cout << "The server has allowed the download." << std::endl;
	std::cout << "File size: " << p_response.file_info.file_size << " bytes." << std::endl;

	// Receive file chunks
	size_t total_received = 0;
	size_t file_size = p_response.file_info.file_size;
	std::vector<uint8_t> checksum(p_response.file_info.checksum, p_response.file_info.checksum + 16);

	std::ofstream file(file_name, std::ios::binary);

	auto start_time = std::chrono::steady_clock::now();
	auto last_time = start_time;
	size_t last_received = 0;

	m_pb_manager->AddFile(file_name);

	std::unordered_map<uint32_t, int> retry_counts;

	if (!file.is_open())
	{
		std::cout << "Cannot open file to write." << std::endl;
		return false;
	}

	while (total_received < file_size)
	{
		PacketHeader header;
		PacketFileChunk fileChunk;

		if (!m_connection->recvPacket(PacketType::FILE_CHUNK, header, fileChunk))
		{
			throw std::runtime_error("Failed to receive file chunk.");
		}

		if (fileChunk.file_id != p_response.file_info.file_id)
		{
			throw std::runtime_error("Invalid file ID in file chunk.");
		}


		// Validate checksum
		bool checksum_valid = true;
		if (CHECKSUM_FLAG)
		{
			std::vector<uint8_t> chunk_checksum = md5Handler->calcCheckSum(fileChunk.data);

			if (memcmp(chunk_checksum.data(), fileChunk.checksum, 16) != 0)
			{
				std::cerr << "Checksum mismatch in chunk " << fileChunk.chunk_index << std::endl;
				checksum_valid = false;
			}
		}

		// Acknowledge the chunk
		PacketFileChunkACK chunkACK(
			fileChunk.file_id,
			fileChunk.chunk_index,
			checksum_valid);

		if (!m_connection->sendPacket(PacketType::FILE_CHUNK_ACK, chunkACK))
		{
			throw std::runtime_error("Failed to send chunk acknowledgment.");
		}

		if (checksum_valid)
		{
			// Write the chunk data to the file
			file.write(reinterpret_cast<const char*>(fileChunk.data.data()), fileChunk.chunk_size);
			file.flush();

			total_received += fileChunk.chunk_size;

			// Calculate download speed
			auto current_time = std::chrono::steady_clock::now();
			std::chrono::duration<double> elapsed = current_time - last_time;
			size_t bytes_received_since_last = total_received - last_received;
			double seconds = elapsed.count();

			double speed_mbps = 0.0;
			if (seconds > 0)
			{
				speed_mbps = (bytes_received_since_last * 8.0) / (seconds * 1'000'000.0); // Mbps
			}

			// Update time and bytes received
			last_time = current_time;
			last_received = total_received;

			// Calculate progress
			float progress = (static_cast<float>(total_received) / file_size) * 100.0f;
			m_pb_manager->UpdateProgress(file_name, progress);
		}
		else
		{
			retry_counts[fileChunk.chunk_index]++;
			if (retry_counts[fileChunk.chunk_index] >= 3)
			{
				std::cerr << "Max retries reached for chunk " << fileChunk.chunk_index << ". Aborting." << std::endl;
				file.close();
				return false;
			}

			// Inform the user and wait before retrying
			std::cerr << "\nRequesting retransmission of chunk " << fileChunk.chunk_index << " (Retry " << retry_counts[fileChunk.chunk_index] << ")" << std::endl;
			// Optionally, add a delay before the next attempt
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
		}
	}

	file.close();

	// Validate checksum
	if (CHECKSUM_FLAG)
	{
		std::vector<uint8_t> file_checksum = md5Handler->calcCheckSumFile(file_name);
		if (memcmp(file_checksum.data(), checksum.data(), 16) != 0)
		{
			std::cerr << "Checksum mismatch in the downloaded file." << std::endl;
			return false;
		}
	}

	std::cout << std::endl
		<< "File downloaded successfully." << std::endl;

	auto end_time = std::chrono::steady_clock::now();
	std::chrono::duration<double> total_duration = end_time - start_time;

	std::cout << "Total time: " << std::fixed << std::setprecision(2) << total_duration.count() << " seconds" << std::endl;
	double avg_speed = (total_received * 8.0) / (total_duration.count() * 1'000'000.0); // Mbps
	std::cout << "Average speed: " << std::fixed << std::setprecision(2) << avg_speed << " Mbps" << std::endl;

	return true;
}

bool FileTransferClient::UploadDirectory(const std::string& dir_path)
{
	// Quét thư mục và lưu các file vào danh sách file entries
	std::vector<FileEntry> fileEntries;
	std::error_code ec;

	// Kiểm tra xem thư mục có tồn tại không
	if (!fs::exists(dir_path) || !fs::is_directory(dir_path))
	{
		std::cerr << "Directory does not exist or is not a valid directory: " << dir_path << std::endl;
		return false;
	}

	// Quét tất cả các file trong thư mục
	for (const auto& entry : fs::recursive_directory_iterator(dir_path, fs::directory_options::skip_permission_denied, ec))
	{
		if (ec) // Nếu gặp lỗi trong quá trình quét thư mục
		{
			std::cerr << "Error scanning directory: " << ec.message() << std::endl;
			return false;
		}

		if (fs::is_regular_file(entry)) // Kiểm tra nếu là file
		{
			std::string file_path = entry.path().string();
			std::string file_name = entry.path().filename().string();
			uint64_t file_size = fs::file_size(entry);
			uint64_t last_modified = fs::last_write_time(entry).time_since_epoch().count();

			// Tính checksum cho file
			std::vector<uint8_t> checksum = md5Handler->calcCheckSumFile(file_path);

			// Chuyển đổi file_type thành chuỗi hoặc loại dữ liệu khác
			std::string file_type_str = fileTypeToString(entry.status().type());

			// Tạo đối tượng FileEntry
			FileEntry fileEntry(file_path, file_name, file_type_str, file_size, last_modified, entry.is_directory());
			fileEntries.push_back(fileEntry);
		}
	}

	// Đảm bảo có file trong thư mục
	if (fileEntries.empty())
	{
		std::cerr << "No files found in the directory." << std::endl;
		return false;
	}

	// Xử lý từng file trong thư mục
	for (const auto& fileEntry : fileEntries)
	{
		std::cout << "Uploading file: " << fileEntry.GetFileName() << std::endl;

		// Gọi lại hàm UploadFile để upload từng file
		std::string file_path = fileEntry.GetFilePath();
		if (!UploadFile(file_path))
		{
			std::cerr << "Failed to upload file: " << fileEntry.GetFileName() << std::endl;
			return false;
		}

		std::cout << "File uploaded successfully: " << fileEntry.GetFileName() << std::endl;
	}

	std::cout << "All files uploaded successfully." << std::endl;
	return true;
}



void FileTransferClient::CloseSession()
{
	PacketCloseSession closeReq = PacketCloseSession();

	if (!m_connection->sendPacket(PacketType::CLOSE_SESSION, closeReq))
	{
		throw std::runtime_error("Failed to send close session request.");
	}

	std::cout << "Session closed successfully." << std::endl;
}
