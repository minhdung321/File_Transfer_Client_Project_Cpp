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

bool is_uploading_directory = false;

FileTransferClient::FileTransferClient()
{
	m_connection = std::make_unique<NetworkConnection>();
	m_session_manager = std::make_unique<SessionManager>(*m_connection);
	m_pb_manager = std::make_unique<ProgressBarManager>();
}

bool FileTransferClient::UploadFile(
	const std::filesystem::path& file_path,
	const std::string& remote_path)
{
	// Check if the file exists
	if (!fs::exists(file_path))
	{
		throw std::runtime_error("File does not exist.");
	}

	// Get the file size
	std::error_code ec;
	std::streamsize fileSize = fs::file_size(file_path, ec);

	if (ec)
	{
		throw std::runtime_error("Failed to get file size.");
	}

	// Calculate the checksum
	std::vector<uint8_t> checksum = md5_handler->calcCheckSumFile(file_path.string());

	// Prepare the upload request
	PacketUploadRequest uploadReq(remote_path, "File", fileSize, checksum.data());

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
		if (!is_uploading_directory)
		{
			std::cout << "The server has allowed the upload." << std::endl;
			std::cout << "File ID of this upload: " << uploadResp.upload_allowed.file_id << std::endl;
			std::cout << "Chunk size: " << uploadResp.upload_allowed.chunk_size << std::endl;
		}
	}
	else
	{
		std::cerr << "The server has denied the upload." << std::endl;
		std::cerr << "Error message: " << uploadResp.out_of_space.message << std::endl;
		return false;
	}

	// Trường hợp đặc biệt với file có kích thước bằng 0
	if (fileSize == 0)
	{
		// Gửi thông báo đặc biệt cho server biết rằng upload đã hoàn tất
		// Có thể gửi một PacketFileChunk với chunk_size = 0
		PacketFileChunk fileChunk(
			uploadResp.upload_allowed.file_id,    // File ID
			0,                                    // Chunk index
			0,                                    // Chunk size
			checksum.data(),                      // Checksum
			nullptr                               // Không có dữ liệu
		);

		if (!m_connection->sendPacket(PacketType::FILE_CHUNK, fileChunk))
		{
			throw std::runtime_error("Failed to send file chunk for zero-sized file.");
		}

		PacketHeader ackHeader;
		PacketFileChunkACK ack;

		if (!m_connection->recvPacket(PacketType::FILE_CHUNK_ACK, ackHeader, ack))
		{
			throw std::runtime_error("Failed to receive chunk acknowledgment for zero-sized file.");
		}

		if (!ack.success ||
			ack.file_id != uploadResp.upload_allowed.file_id ||
			ack.chunk_index != 0)
		{
			throw std::runtime_error("Chunk ACK validation failed for zero-sized file.");
		}

		return true;
	}

	const std::string file_ckp = file_path.stem().string() + ".ckp"; // Checkpoint file

	// Upload the file
	size_t chunk_size = uploadResp.upload_allowed.chunk_size;
	size_t chunk_count = ((fileSize + chunk_size - 1) / chunk_size);

	// Open the file
	std::ifstream file(file_path, std::ios::binary);
	if (!file.is_open())
	{
		throw std::runtime_error("Failed to open file.");
	}

	if (!is_uploading_directory)
	{
		std::cout << "Starting to upload the file in " << chunk_count << " chunks." << std::endl;
	}

	auto start_time = std::chrono::steady_clock::now();
	auto last_time = start_time;
	size_t last_sent = 0;
	double total_speed = 0.0;

	size_t total_sent = 0;

	const int MAX_RETRIES = 3;
	const int BASE_TIMEOUT = 1000; // 1 second

	m_pb_manager->AddFile(file_path.filename().string());

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
					checksum = md5_handler->calcCheckSum(chunk_data);
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
				m_pb_manager->UpdateProgress(file_path.filename().string(), progress);

				// Lưu trạng thái upload 
				std::ofstream state_file(file_ckp, std::ios::binary | std::ios::trunc);
				state_file.write(reinterpret_cast<const char*>(&uploadResp.upload_allowed.file_id), sizeof(uploadResp.upload_allowed.file_id));
				state_file.write(reinterpret_cast<const char*>(&chunk_size), sizeof(chunk_size));
				state_file.close();
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

	// Đóng checkpoint file
	if (fs::exists(file_ckp))
	{
		fs::remove(file_ckp);
	}

	file.close(); // Đóng file sau khi upload xong

	auto end_time = std::chrono::steady_clock::now();
	std::chrono::duration<double> total_duration = end_time - start_time;

	if (!is_uploading_directory)
	{
		std::cout << std::endl
			<< "File uploaded successfully : " << remote_path << std::endl;
		std::cout << "Total time: " << std::fixed << std::setprecision(2) << total_duration.count() << " seconds" << std::endl;
		double avg_speed = (total_sent * 8.0) / (total_duration.count() * 1000000.0); // Mbps
		std::cout << "Average speed: " << std::fixed << std::setprecision(2) << avg_speed << " Mbps" << std::endl;
	}

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
			std::vector<uint8_t> chunk_checksum = md5_handler->calcCheckSum(fileChunk.data);

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
		std::vector<uint8_t> file_checksum = md5_handler->calcCheckSumFile(file_name);
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

bool FileTransferClient::UploadDirectory(const fs::path& dir_path, size_t total_files)
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
	m_pb_manager->AddFile("Scan Directory");
	FileEntry file_entry{};
	size_t current_file_count = 0;

	for (const auto& entry : fs::recursive_directory_iterator(dir_path, fs::directory_options::skip_permission_denied, ec))
	{
		if (ec) // Nếu gặp lỗi trong quá trình quét thư mục
		{
			std::cerr << "Error scanning directory: " << ec.message() << std::endl;
			return false;
		}

		if (fs::is_regular_file(entry)) // Kiểm tra nếu là file
		{
			file_entry.absolute_path = entry.path().string();
			file_entry.relative_path = fs::relative(entry.path(), dir_path.parent_path()).string();
			file_entry.file_name = entry.path().filename().string();
			file_entry.file_size = fs::file_size(entry);

			std::vector<uint8_t> checksum = md5_handler->calcCheckSumFile(file_entry.absolute_path);

			memcpy(file_entry.checksum, checksum.data(), 16);

			fileEntries.emplace_back(file_entry);

		}

		current_file_count++;

		// Hiển thị tiến trình quét thư mục
		float progress = (static_cast<float>(current_file_count) / total_files) * 100.0f;
		m_pb_manager->UpdateProgress("Scan Directory", progress);
	}

	// Đảm bảo có file trong thư mục
	if (fileEntries.empty())
	{
		std::cerr << "No files found in the directory." << std::endl;
		return false;
	}

	// Sắp xếp file theo kích thước tăng dần
	std::stable_sort(fileEntries.begin(), fileEntries.end(),
		[](const FileEntry& a, const FileEntry& b) {
			return a.file_size < b.file_size;
		});

	is_uploading_directory = true;

	for (const auto& fileEntry : fileEntries)
	{
		try
		{
			UploadFile(fileEntry.absolute_path, fileEntry.relative_path);
		}
		catch (const std::exception& e)
		{
			continue;
		}
	}

	std::cout << "> All files uploaded successfully." << std::endl;

	is_uploading_directory = false;

	return true;
}

bool FileTransferClient::ResumeUpload(const fs::path& file_path)
{
	// Check if the file exists
	if (!fs::exists(file_path))
	{
		std::cerr << "File does not exist." << std::endl;
		return false;
	}

	// Get the file size
	std::error_code ec;
	std::streamsize fileSize = fs::file_size(file_path, ec);

	if (ec)
	{
		std::cerr << "Failed to get file size." << std::endl;
		return false;
	}

	// Get checkpoint file
	const std::string file_ckp = file_path.stem().string() + ".ckp";

	if (!fs::exists(file_ckp))
	{
		std::cerr << "Checkpoint file not found. The file cannot be resumed." << std::endl;
		std::cerr << "Please upload the file from the beginning." << std::endl;
		return false;
	}

	// Read the checkpoint file
	std::ifstream state_file(file_ckp, std::ios::binary);
	uint32_t file_id{};
	size_t chunk_size{};
	state_file.read(reinterpret_cast<char*>(&file_id), sizeof(file_id));
	state_file.read(reinterpret_cast<char*>(&chunk_size), sizeof(chunk_size));
	state_file.close();

	PacketResumeRequest resumeReq(file_id, 0, 0); // Resume position sẽ mặc định là 0, kết quả sẽ cập nhật theo của Server

	if (!m_connection->sendPacket(PacketType::RESUME_UPLOAD_REQUEST, resumeReq))
	{
		std::cerr << "Failed to send resume request." << std::endl;
		return false;
	}

	PacketHeader header;
	PacketResumeResponse resumeResp;

	if (!m_connection->recvPacket(PacketType::RESUME_RESPONSE, header, resumeResp))
	{
		std::cerr << "Failed to receive resume response." << std::endl;
		return false;
	}

	if (resumeResp.status == ResumeStatus::RESUME_SUPPORTED)
	{
		std::cout << "The server has allowed the resume." << std::endl;
		std::cout << "File ID of this upload: " << resumeResp.resume_allowed.file_id << std::endl;
		std::cout << "Remaining chunk count: " << resumeResp.resume_allowed.remaining_chunk_count << std::endl;
	}
	else
	{
		std::cerr << "The server has denied the resume." << std::endl;
		std::cerr << "Error message: " << resumeResp.resume_not_found.message << std::endl;
		return false;
	}

	// Upload the file
	size_t chunk_count = ((fileSize + chunk_size - 1) / chunk_size);
	size_t last_sent_chunk_index = chunk_count - resumeResp.resume_allowed.remaining_chunk_count - 1;

	// Open the file
	std::ifstream file(file_path, std::ios::binary);
	if (!file.is_open())
	{
		throw std::runtime_error("Failed to open file.");
	}

	// Seek to the last position
	file.seekg(resumeResp.resume_allowed.resume_position, std::ios::beg);

	std::cout << "Starting to resume the upload in " << chunk_count << " chunks." << std::endl;

	auto start_time = std::chrono::steady_clock::now();
	auto last_time = start_time;
	size_t last_sent = 0;
	double total_speed = 0.0;

	size_t total_sent = resumeResp.resume_allowed.resume_position;

	const int MAX_RETRIES = 3;
	const int BASE_TIMEOUT = 1000; // 1 second

	m_pb_manager->AddFile(file_path.filename().string());

	for (size_t i = last_sent_chunk_index + 1; i < chunk_count; i++)
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
					checksum = md5_handler->calcCheckSum(chunk_data);
				}

				// Prepare the file chunk packet
				PacketFileChunk fileChunk(
					resumeResp.resume_allowed.file_id,				// File ID
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
					ack.file_id != resumeResp.resume_allowed.file_id ||
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

				float progress = (static_cast<float>(resumeReq.resume_position + total_sent) / fileSize) * 100.0f;
				m_pb_manager->UpdateProgress(file_path.filename().string(), progress);

				// Lưu trạng thái upload 
				std::ofstream state_file(file_ckp, std::ios::binary | std::ios::trunc);
				state_file.write(reinterpret_cast<const char*>(&resumeResp.resume_allowed.file_id), sizeof(resumeResp.resume_allowed.file_id));
				state_file.write(reinterpret_cast<const char*>(&i), sizeof(i));
				state_file.close();
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

	// Đóng checkpoint file
	if (fs::exists(file_ckp))
	{
		fs::remove(file_ckp);
	}

	file.close(); // Đóng file sau khi upload xong

	auto end_time = std::chrono::steady_clock::now();
	std::chrono::duration<double> total_duration = end_time - start_time;

	std::cout << std::endl
			<< "File uploaded successfully : " << file_path.filename().string() << std::endl;
	std::cout << "Total time: " << std::fixed << std::setprecision(2) << total_duration.count() << " seconds" << std::endl;
	double avg_speed = (total_sent * 8.0) / (total_duration.count() * 1000000.0); // Mbps
	std::cout << "Average speed: " << std::fixed << std::setprecision(2) << avg_speed << " Mbps" << std::endl;
	
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

	m_pb_manager->Cleanup();

	m_connection->Disconnect();

	std::cout << "Connection closed." << std::endl;
}
