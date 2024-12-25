#include <file_transfer_client.h>
#include <packet_helper.hpp>
#include <path_resolver.h>
using namespace utils;

#include <iostream>
#include <fstream>
#include <filesystem>
#include <future>
#include <thread>
#include <chrono>
#include <sstream>
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

	std::vector<uint8_t> checksum{};

	if (!is_uploading_directory)
	{
		m_pb_manager->AddFile("Calculating checksum");

		// Calculate the checksum
		checksum = md5_handler->calcCheckSumFile(file_path.string(), [this, &fileSize](size_t progress)
			{
				m_pb_manager->UpdateProgress("Calculating checksum", static_cast<float>(progress * 100.0f / fileSize));
			});

		m_pb_manager->Cleanup();
	}
	else
	{
		// Empty checksum for directory
	}

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
		if (!is_uploading_directory)
		{
			std::cerr << "The server has denied the upload." << std::endl;
			std::cerr << "Error message: " << uploadResp.out_of_space.message << std::endl;
		}
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

		if (!is_uploading_directory)
		{
			std::cout << "File uploaded successfully : " << remote_path << std::endl;
		}

		return true;
	}

	const std::string file_ckp = file_path.stem().string() + ".ckp"; // Checkpoint file
	std::ofstream state_file(file_ckp, std::ios::binary | std::ios::trunc);

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
				std::vector<uint8_t> checksum{};
				if (CHECKSUM_FLAG)
				{
					if (!is_uploading_directory)
					{
						checksum = md5_handler->calcCheckSum(chunk_data);
					}
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
				// (file_id, chunk_size, chunk_index)
				state_file.seekp(0);
				state_file.write(reinterpret_cast<const char*>(&uploadResp.upload_allowed.file_id), sizeof(uploadResp.upload_allowed.file_id));
				state_file.write(reinterpret_cast<const char*>(&chunk_size), sizeof(chunk_size));
				state_file.write(reinterpret_cast<const char*>(&i), sizeof(i));
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

	m_pb_manager->UpdateProgress(file_path.filename().string(), 100.0f);

	// Đóng checkpoint file
	if (fs::exists(file_ckp))
	{
		state_file.close();
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
	// Create check point folder
	PathResolver pathResolver;
	uint32_t file_id = 0;
	uint64_t resume_position = 0;
	uint32_t last_chunk_index = 0;

	fs::path path(file_name);
	std::string namePart = path.stem().string();
	std::string check_point_path = utils::DEFAULT_CHECK_POINT_PATH + namePart + ".ckp";

	if (!pathResolver.CheckDirPathExist(utils::DEFAULT_CHECK_POINT_PATH))
	{
		pathResolver.CreateCheckPointDirectory();
	}

	// Create check point file
	pathResolver.CreateFileWithName(check_point_path);


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

	// Check file name exist to generate new name
	std::string new_file_name = file_name;
	std::ifstream check(file_name, std::ios::binary);
	if (check.is_open())
	{
		new_file_name = pathResolver.GenerateNewFileName(file_name);
	}

	std::ofstream file(new_file_name, std::ios::binary);

	auto start_time = std::chrono::steady_clock::now();
	auto last_time = start_time;
	size_t last_received = 0;

	m_pb_manager->AddFile(new_file_name);

	std::unordered_map<uint32_t, int> retry_counts;

	if (!file.is_open())
	{
		std::cout << "Cannot open file to write." << std::endl;
		return false;
	}
	std::ofstream resume_out(check_point_path, std::ios::binary);
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
			const std::vector<uint8_t>& chunk_checksum = md5_handler->calcCheckSum(fileChunk.data);

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

			// Save info for resuming transfer
			size_t len = new_file_name.length();
			resume_out.seekp(0);
			resume_out.write(reinterpret_cast<const char*>(&len), sizeof(len));
			resume_out.write(reinterpret_cast<const char*>(new_file_name.data()), sizeof(char) * len);
			resume_out.write(reinterpret_cast<const char*>(&p_response.file_info.file_id), sizeof(p_response.file_info.file_id));
			resume_out.write(reinterpret_cast<const char*>(&total_received), sizeof(total_received));
			resume_out.write(reinterpret_cast<const char*>(&fileChunk.chunk_index), sizeof(fileChunk.chunk_index));
			resume_out.write(reinterpret_cast<const char*>(&file_size), sizeof(file_size));

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
			m_pb_manager->UpdateProgress(new_file_name, progress);
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

	resume_out.close();
	file.close();

	// Validate checksum
	if (CHECKSUM_FLAG)
	{
		const std::vector<uint8_t>& file_checksum = md5_handler->calcCheckSumFile(new_file_name);
		if (memcmp(file_checksum.data(), checksum.data(), 16) != 0)
		{
			std::cerr << "Checksum mismatch in the downloaded file." << std::endl;
			return false;
		}
	}

	auto end_time = std::chrono::steady_clock::now();
	std::chrono::duration<double> total_duration = end_time - start_time;

	std::cout << "Total time: " << std::fixed << std::setprecision(2) << total_duration.count() << " seconds" << std::endl;
	double avg_speed = (total_received * 8.0) / (total_duration.count() * 1'000'000.0); // Mbps
	std::cout << "Average speed: " << std::fixed << std::setprecision(2) << avg_speed << " Mbps" << std::endl;

	// If file download successful, then delete resume status of this file
	pathResolver.DeleteFileWithName(check_point_path);

	return true;
}

bool  FileTransferClient::ResumeDownload(const std::string& file_name)
{
	PathResolver pathResolver;
	uint32_t file_id_read{};
	uint64_t resume_position_read{};
	uint32_t last_chunk_index_read{};
	uint64_t file_size{};
	std::string temp;
	size_t len{};

	std::stringstream ss(file_name);

	fs::path path(file_name.data());
	std::string namePart = path.stem().string();
	std::string check_point_path = utils::DEFAULT_CHECK_POINT_PATH + namePart + ".ckp";
	std::ifstream resume_in(check_point_path, std::ios::binary);

	// Repair info resuming transfer
	if (resume_in.is_open())
	{
		resume_in.read(reinterpret_cast<char*>(&len), sizeof(len));
		resume_in.read(reinterpret_cast<char*>(temp.data()), sizeof(char) * len);
		resume_in.read(reinterpret_cast<char*>(&file_id_read), sizeof(file_id_read));
		resume_in.read(reinterpret_cast<char*>(&resume_position_read), sizeof(resume_position_read));
		resume_in.read(reinterpret_cast<char*>(&last_chunk_index_read), sizeof(last_chunk_index_read));
		resume_in.read(reinterpret_cast<char*>(&file_size), sizeof(file_size));
	}
	resume_in.close();

	// Prepare the download request
	PacketResumeRequest p_request(file_id_read, resume_position_read, last_chunk_index_read);

	if (!m_connection->sendPacket(PacketType::RESUME_DOWNLOAD_REQUEST, p_request))
	{
		throw std::runtime_error("Failed to send download request.");
	}

	PacketHeader header;
	PacketResumeResponse p_response;

	if (!m_connection->recvPacket(PacketType::RESUME_RESPONSE, header, p_response))
	{
		throw std::runtime_error("Failed to receive download response.");
	}

	if (p_response.status != ResumeStatus::RESUME_SUPPORTED)
	{
		std::cerr << "Server is not support resuming this file. Message: " << p_response.resume_not_found.message << std::endl;

		// If the resume state is invalid, then delete resume status of this file
		pathResolver.DeleteFileWithName(check_point_path);
		return false;
	}

	std::cout << "The server has allowed resuming download." << std::endl;
	std::cout << "File id: " << p_response.resume_allowed.file_id << std::endl;
	std::cout << "Resume position : " << p_response.resume_allowed.resume_position << std::endl;
	std::cout << "Remainning chunk : " << p_response.resume_allowed.remaining_chunk_count << std::endl;

	// Receive file chunks
	size_t total_received = resume_position_read;

	std::ofstream file(file_name, std::ios::binary | std::ios::app);

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
	std::ofstream resume_out(check_point_path, std::ios::binary);

	while (p_response.resume_allowed.remaining_chunk_count-- > 0)
	{
		PacketHeader header;
		PacketFileChunk fileChunk;

		if (!m_connection->recvPacket(PacketType::FILE_CHUNK, header, fileChunk))
		{
			throw std::runtime_error("Failed to receive file chunk.");
		}

		if (fileChunk.file_id != p_response.resume_allowed.file_id)
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

			resume_out.seekp(0);
			resume_out.write(reinterpret_cast<const char*>(&len), sizeof(len));
			resume_out.write(reinterpret_cast<const char*>(temp.data()), sizeof(char) * len);
			resume_out.write(reinterpret_cast<const char*>(&p_response.resume_allowed.file_id), sizeof(p_response.resume_allowed.file_id));
			resume_out.write(reinterpret_cast<const char*>(&total_received), sizeof(total_received));
			resume_out.write(reinterpret_cast<const char*>(&fileChunk.chunk_index), sizeof(fileChunk.chunk_index));
			resume_out.write(reinterpret_cast<const char*>(&file_size), sizeof(file_size));

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
	resume_out.close();
	file.close();

	// Validate checksum
	/*if (CHECKSUM_FLAG)
	{
		std::vector<uint8_t> file_checksum = md5Handler->calcCheckSumFile(filename);
		if (memcmp(file_checksum.data(), checksum.data(), 16) != 0)
		{
			std::cerr << "Checksum mismatch in the downloaded file." << std::endl;
			return false;
		}
	}*/

	auto end_time = std::chrono::steady_clock::now();
	std::chrono::duration<double> total_duration = end_time - start_time;

	std::cout << "Total time: " << std::fixed << std::setprecision(2) << total_duration.count() << " seconds" << std::endl;
	double avg_speed = (total_received * 8.0) / (total_duration.count() * 1'000'000.0); // Mbps
	std::cout << "Average speed: " << std::fixed << std::setprecision(2) << avg_speed << " Mbps" << std::endl;

	// If file download successful, then delete resume status of this file
	pathResolver.DeleteFileWithName(check_point_path);

	return true;
}

std::vector<FileTransferClient::FileEntry> FileTransferClient::ScanDirectory(const fs::path& dir_path, size_t total_files)
{
	// Quét thư mục và lưu các file vào danh sách file entries
	std::vector<FileEntry> fileEntries;
	std::error_code ec;

	// Kiểm tra xem thư mục có tồn tại không
	if (!fs::exists(dir_path) || !fs::is_directory(dir_path))
	{
		throw std::runtime_error("Directory does not exist or is not a valid directory: " + dir_path.string());
	}

	// Quét tất cả các file trong thư mục
	m_pb_manager->AddFile("Scan Directory");
	FileEntry file_entry{};
	size_t current_file_count = 0;

	for (const auto& entry : fs::recursive_directory_iterator(dir_path, fs::directory_options::skip_permission_denied, ec))
	{
		if (ec) // Nếu gặp lỗi trong quá trình quét thư mục
		{
			throw std::runtime_error("Error scanning directory: " + ec.message());
		}

		if (fs::is_regular_file(entry)) // Kiểm tra nếu là file
		{
			file_entry.absolute_path = entry.path().string();
			file_entry.relative_path = fs::relative(entry.path(), dir_path.parent_path()).string();
			file_entry.file_name = entry.path().filename().string();
			file_entry.file_size = fs::file_size(entry);

			fileEntries.emplace_back(file_entry);
		}

		current_file_count++;

		// Hiển thị tiến trình quét thư mục
		float progress = (static_cast<float>(current_file_count) / total_files) * 100.0f;
		m_pb_manager->UpdateProgress("Scan Directory", progress);
	}

	m_pb_manager->Cleanup();

	// Đảm bảo có file trong thư mục
	if (fileEntries.empty())
	{
		throw std::runtime_error("No files found in the directory.");
	}

	// Sắp xếp file theo kích thước giảm dần
	std::stable_sort(fileEntries.begin(), fileEntries.end(),
		[](const FileEntry& a, const FileEntry& b) {
			return a.file_size > b.file_size;
		});

	return fileEntries;
}

bool FileTransferClient::UploadDirectory(const fs::path& dir_path, size_t total_files)
{
	// Quét thư mục và lưu các file vào danh sách file entries
	std::vector<FileEntry> fileEntries;
	try
	{
		fileEntries = ScanDirectory(dir_path, total_files);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Failed to scan directory: " << e.what() << std::endl;
		return false;
	}

	is_uploading_directory = true;

	std::vector<std::string> failed_files;

	m_pb_manager->ShowTotalProgress(true, total_files);
	size_t current_file_count = 0;

	// Upload từng file trong danh sách
	for (const auto& fileEntry : fileEntries)
	{
		if (!UploadFile(fileEntry.absolute_path, fileEntry.relative_path))
		{
			failed_files.push_back(fileEntry.relative_path);
		}

		// Cập nhật tiến trình tổng
		m_pb_manager->UpdateTotalProgress(++current_file_count);

		std::this_thread::sleep_for(std::chrono::milliseconds(150)); // Delay 150ms
	}

	m_pb_manager->UpdateTotalProgress(total_files);

	is_uploading_directory = false;

	if (!failed_files.empty())
	{
		std::cerr << "\n\nFailed to upload the following files:" << std::endl;

		for (const auto& file : failed_files)
		{
			std::cerr << file << std::endl;
		}
	}

	return true;
}

bool FileTransferClient::UploadDirectoryParallel(const fs::path& dir_path, size_t total_files)
{
	// Quét thư mục và lưu các file vào danh sách file entries
	std::vector<FileEntry> fileEntries;
	try
	{
		fileEntries = ScanDirectory(dir_path, total_files);
	}
	catch (const std::exception& e)
	{
		std::cerr << "Failed to scan directory: " << e.what() << std::endl;
		return false;
	}

	const auto& server_info = m_connection->GetServerInfo();
	const auto& credential_info = m_session_manager->GetUserCredential();

	is_uploading_directory = true;
	const size_t num_clients = 4;

	// Distribute files evenly among clients
	std::vector<std::vector<FileEntry>> clientFileEntries(num_clients);

	size_t filesPerClient = fileEntries.size() / 4;
	size_t remainingFiles = fileEntries.size() % 4;
	size_t startIndex = 0;

	for (size_t i = 0; i < num_clients; ++i)
	{
		size_t endIndex = startIndex + filesPerClient;
		if (remainingFiles > 0)
		{
			++endIndex;
			--remainingFiles;
		}
		clientFileEntries[i].assign(fileEntries.begin() + startIndex, fileEntries.begin() + endIndex);
		startIndex = endIndex;
	}

	is_uploading_directory = true;

	std::vector<std::string> failed_files;
	std::mutex failed_files_mutex;

	auto start_time = std::chrono::steady_clock::now();

	// Start the threads
	std::vector<std::thread> threads;
	for (size_t i = 0; i < num_clients; ++i)
	{
		std::thread t(
			[this, i, &failed_files, &failed_files_mutex, &clientFileEntries, &server_info, &credential_info]()
			{
				try
				{
					std::unique_ptr<FileTransferClient> client = std::make_unique<FileTransferClient>();

					client->GetConnection().Disconnect();

					std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Delay 100ms

					client->GetSessionManager().ResetSession();

					client->GetConnection().Connect(server_info.first, server_info.second);

					std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Delay 100ms

					if (!client->GetConnection().IsConnected())
					{
						throw std::runtime_error("Failed to connect to the server.");
					}

					if (!client->GetSessionManager().PerformHandshake())
					{
						throw std::runtime_error("Failed to perform handshake.");
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Delay 500ms

					if (!client->GetSessionManager().PerformAuthentication(credential_info.first, credential_info.second))
					{
						throw std::runtime_error("Failed to authenticate.");
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Delay 500ms

					system("cls");

					for (const auto& fileEntry : clientFileEntries[i])
					{
						if (!client->UploadFile(fileEntry.absolute_path, fileEntry.relative_path))
						{
							std::unique_lock<std::mutex> lock(failed_files_mutex);
							failed_files.push_back(fileEntry.relative_path);
						}

						std::this_thread::sleep_for(std::chrono::milliseconds(150)); // Delay 150ms
					}

					std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Delay 1000ms

					client->CloseSession();

					client->GetSessionManager().ResetSession();

					client->GetConnection().Disconnect();
				}
				catch (const std::exception& e)
				{
					std::cerr << "Thread " << i << " failed: " << e.what() << std::endl;
				}
			});

		threads.push_back(std::move(t));
	}

	for (auto& t : threads)
	{
		if (t.joinable())
		{
			t.join();
		}
	}

	is_uploading_directory = false;

	auto end_time = std::chrono::steady_clock::now();

	if (!failed_files.empty())
	{
		std::cerr << "\n\nFailed to upload the following files:" << std::endl;

		for (const auto& file : failed_files)
		{
			std::cerr << "- " << file << std::endl;
		}
	}

	std::chrono::duration<double> total_duration = end_time - start_time;

	std::cout << "\n\nTotal time: " << std::fixed << std::setprecision(2) << total_duration.count() << " seconds" << std::endl;

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

bool FileTransferClient::GetServerFileList()
{

	return false;
}

void FileTransferClient::CloseSession()
{
	PacketCloseSession closeReq = PacketCloseSession();

	if (!m_connection->sendPacket(PacketType::CLOSE_SESSION, closeReq))
	{
		throw std::runtime_error("Failed to send close session request.");
	}

	m_pb_manager->Cleanup();

	m_connection->Disconnect();

	m_session_manager->ResetSession();
}
