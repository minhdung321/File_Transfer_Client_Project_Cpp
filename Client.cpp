#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <sstream>
#include <thread>
#include <WinSock2.h>
#include <WS2tcpip.h>
namespace fs = std::filesystem;

#include "packet_helper.hpp"
#include "encryption_handler.hpp"
#include "progressbar_manager.h"

#pragma comment(lib, "ws2_32.lib")

constexpr auto CHECKSUM_FLAG = true;

class NetworkClient
{
private:
	SOCKET clientSocket;
	bool isConnected;
	uint8_t sessionId[16];
	std::unique_ptr<security::datasecurity::integrity::MD5Handler> md5Handler;
	std::unique_ptr<ProgressBarManager> pbManager;

	static constexpr size_t MAX_PAYLOAD_SIZE = 1024 * 1024 * 32 + 1024 * 512; // 32 MB + 512KB

	template <typename T>
	bool sendPacket(const PacketType& type, const T& data)
	{
		// Tạo gói tin đã tuần tự hoá
		std::vector<uint8_t> packet = PacketHelper::CreatePacket(type, data, sessionId);

		if (packet.empty())
		{
			std::cerr << "Failed to create packet." << std::endl;
			return false;
		}

		// Gửi gói tin
		size_t total_sent = 0;
		size_t packet_size = packet.size();

		while (total_sent < packet_size)
		{
			int bytes_to_send = static_cast<int>(std::min<size_t>(packet_size - total_sent, static_cast<size_t>(INT32_MAX)));
			int bytes = send(clientSocket, reinterpret_cast<const char*>(packet.data() + total_sent), bytes_to_send, 0);

			if (bytes == SOCKET_ERROR)
			{
				handleError("Send failed");
				return false;
			}
			else if (bytes == 0)
			{
				std::cerr << "Connection closed by peer." << std::endl;
				return false;
			}

			total_sent += bytes;
		}

		return true;
	}

	template <typename T>
	bool recvPacket(PacketType expectedType, PacketHeader& header, T& outData)
	{
		// Xử lý prefix và header
		PacketPrefix prefix{};
		if (!recvData(reinterpret_cast<uint8_t*>(&prefix), sizeof(PacketPrefix)))
		{
			std::cerr << "Failed to receive packet prefix." << std::endl;
			return false;
		}

		if (prefix.encrypted_packet_length > MAX_PAYLOAD_SIZE)
		{
			std::cerr << "Invalid encrypted packet length: " << prefix.encrypted_packet_length << std::endl;
			return false;
		}

		std::vector<uint8_t> encrypted_packet(prefix.encrypted_packet_length);

		if (!recvData(encrypted_packet.data(), encrypted_packet.size()))
		{
			std::cerr << "Failed to receive encrypted packet." << std::endl;
			return false;
		}

		// Chèn PacketPrefix vào đầu encrypted packet
		encrypted_packet.insert(encrypted_packet.begin(),
			reinterpret_cast<uint8_t*>(&prefix),
			reinterpret_cast<uint8_t*>(&prefix) + sizeof(PacketPrefix));

		std::vector<uint8_t> decrypted_packet = PacketHelper::DecryptPacket(encrypted_packet.data(), encrypted_packet.size());

		if (decrypted_packet.empty())
		{
			std::cerr << "Failed to decrypt packet." << std::endl;
			return false;
		}

		// Phân giải header từ decrypted packet
		if (!PacketHelper::DeserializeHeader(decrypted_packet.data(), decrypted_packet.size(), header))
		{
			std::cerr << "Failed to deserialize packet header." << std::endl;
			return false;
		}

		// Kiểm tra xem header có phải là gói tin lỗi không
		if (header.packet_type == PacketType::ERR_PACKET)
		{
			PacketError error;
			if (!PacketHelper::DeserializePayload(decrypted_packet.data(), decrypted_packet.size(), header, error))
			{
				std::cerr << "Failed to deserialize error packet." << std::endl;
				return false;
			}

			std::cerr << "== ERROR ==" << std::endl;
			std::cerr << "Error code: " << error.error_code << std::endl;
			std::cerr << "Error message: " << error.error_message << std::endl;
			return false;
		}

		// Xác định loại gói tin
		if (header.packet_type != expectedType)
		{
			std::cerr << "Invalid packet header or unexpected packet type." << std::endl;
			return false;
		}

		// Phân giải payload từ decrypted packet
		if (!PacketHelper::DeserializePayload(decrypted_packet.data(), decrypted_packet.size(), header, outData))
		{
			std::cerr << "Failed to deserialize packet payload." << std::endl;
			return false;
		}

		return true;
	}

	bool recvData(uint8_t* data, size_t size)
	{
		size_t total_received = 0;

		while (total_received < size)
		{
			int bytes = recv(clientSocket,
				reinterpret_cast<char*>(data + total_received),
				static_cast<int>(size - total_received), 0);

			if (bytes <= 0)
			{
				handleError("Receive failed");
				return false;
			}

			total_received += bytes;
		}

		return true;
	}

	bool validateHeader(const PacketHeader& header)
	{
		if (header.magic_number != 0x5A57)
		{
			std::cerr << "Invalid magic number: " << header.magic_number << std::endl;
			return false;
		}

		if (header.version != 1)
		{
			std::cerr << "Invalid version: " << static_cast<int>(header.version) << std::endl;
			return false;
		}

		if (header.payload_length > MAX_PAYLOAD_SIZE)
		{
			std::cerr << "Invalid payload length: " << header.payload_length << std::endl;
			return false;
		}
		return true;
	}

	void handleError(const char* message)
	{
		std::cerr << message << ": " << WSAGetLastError() << std::endl;
	}

public:
	NetworkClient() :
		clientSocket(INVALID_SOCKET),
		isConnected(false),
		md5Handler(std::make_unique<security::datasecurity::integrity::MD5Handler>()),
		pbManager(std::make_unique<ProgressBarManager>())
	{
		WSADATA wsaData;
		if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
		{
			throw std::runtime_error("Failed to initialize Winsock.");
		}

		std::cout << "Winsock initialized." << std::endl;

		memset(sessionId, 0, sizeof(sessionId));
	}

	~NetworkClient()
	{
		disconnect();
		WSACleanup();
	}

	uint8_t* getSessionId() const
	{
		return const_cast<uint8_t*>(sessionId);
	}

	void connect(const char* serverIP, int serverPort)
	{
		clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (clientSocket == INVALID_SOCKET)
		{
			throw std::runtime_error("Failed to create socket.");
		}

		sockaddr_in addr = {};
		addr.sin_family = AF_INET;
		addr.sin_port = htons(serverPort);
		inet_pton(AF_INET, serverIP, &addr.sin_addr);

		if (::connect(clientSocket, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
		{
			closesocket(clientSocket);
			throw std::runtime_error("Failed to connect to server.");
		}

		isConnected = true;
		std::cout << "Connected to server at " << serverIP << ":" << serverPort << std::endl;
	}

	void disconnect()
	{
		if (clientSocket != INVALID_SOCKET)
		{
			closesocket(clientSocket);
			clientSocket = INVALID_SOCKET;
		}
		isConnected = false;
	}

	bool performHandshake()
	{
		PacketHandshakeRequest handshakeReq = { 1 };

		if (!sendPacket(PacketType::HANDSHAKE_REQUEST, handshakeReq))
		{
			throw std::runtime_error("Failed to send handshake request.");
		}

		PacketHeader responseHeader{};
		PacketHandshakeResponse response{};

		if (!recvPacket(PacketType::HANDSHAKE_RESPONSE, responseHeader, response))
		{
			throw std::runtime_error("Failed to receive handshake response.");
		}

		std::cout << "Handshake successful. Server version: " << static_cast<int>(response.server_version) << std::endl;
		std::cout << "Server message: " << response.message << std::endl;

		return true;
	}

	bool authenticate(const std::string& username, const std::string& password)
	{
		PacketAuthenticationRequest authReq;
		strncpy_s(authReq.username, username.c_str(), MAX_USERNAME_LENGTH);
		strncpy_s(authReq.password, password.c_str(), MAX_PASSWORD_LENGTH);

		if (!sendPacket(PacketType::AUTHENTICATION_REQUEST, authReq))
		{
			throw std::runtime_error("Failed to send authentication request.");
		}

		PacketHeader header;
		PacketAuthenticationResponse authResp;

		if (!recvPacket(PacketType::AUTHENTICATION_RESPONSE, header, authResp))
		{
			throw std::runtime_error("Failed to receive authentication response.");
		}

		if (authResp.authenticated)
		{
			memcpy(sessionId, authResp.session_id, 16);
			std::cout << "Authentication successful." << std::endl;
			std::cout << "Server message: " << authResp.message << std::endl;
			std::cout << "Session ID: ";
			for (int i = 0; i < 16; i++)
			{
				printf("%02X", sessionId[i]);
			}
			std::cout << std::endl;
		}
		else
		{
			std::cerr << "Server message: " << authResp.message << std::endl;
		}

		return authResp.authenticated;
	}

	bool uploadFile(const std::string& fileName)
	{
		// Get the file size
		std::error_code ec;
		std::streamsize fileSize = fs::file_size(fileName, ec);

		if (ec)
		{
			throw std::runtime_error("Failed to get file size.");
		}

		std::cout << "File size: " << fileSize << " bytes (" << (fileSize / 1024) << " KB)" << std::endl;

		// Open the file and move to the end to get the size
		std::ifstream file(fileName, std::ios::binary);
		if (!file.is_open())
		{
			throw std::runtime_error("Failed to open file.");
		}

		// Calculate the checksum
		std::cout << "Calculating MD5 checksum..." << std::endl;
		std::vector<uint8_t> checksum = md5Handler->calcCheckSumFile(fileName);

		// Show MD5 checksum
		std::cout << "MD5 checksum: ";
		for (int i = 0; i < 16; i++)
		{
			printf("%02x", checksum[i]);
		}

		std::cout << std::endl;

		// Prepare the upload request
		PacketUploadRequest uploadReq(fileName, "File", fileSize, checksum.data());

		if (!sendPacket(PacketType::UPLOAD_REQUEST, uploadReq))
		{
			throw std::runtime_error("Failed to send upload request.");
		}

		PacketHeader header;
		PacketUploadResponse uploadResp;

		if (!recvPacket(PacketType::UPLOAD_RESPONSE, header, uploadResp))
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

		pbManager->AddFile(fileName);

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

					if (!sendPacket(PacketType::FILE_CHUNK, fileChunk))
					{
						throw std::runtime_error("Failed to send file chunk.");
					}

					PacketHeader ackHeader;
					PacketFileChunkACK ack;

					if (!recvPacket(PacketType::FILE_CHUNK_ACK, ackHeader, ack))
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
					pbManager->UpdateProgress(fileName, progress);
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
	bool uploadFolder(const std::string& dir_path) {
		// Kiểm tra thư mục hợp lệ
		if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
			std::cerr << "Invalid directory path." << std::endl;
			return false;
		}

		std::vector<std::string> filesToUpload;

		// Duyệt qua thư mục và lấy danh sách các file
		for (const auto& entry : fs::recursive_directory_iterator(dir_path)) {
			if (fs::is_regular_file(entry)) {
				filesToUpload.push_back(entry.path().string());
			}
		}

		// Gửi yêu cầu upload thư mục (chỉ gửi 1 lần)
		PacketUploadRequest uploadDirRequest(dir_path, "", 0, nullptr);
		if (!sendPacket(PacketType::UPLOAD_REQUEST, uploadDirRequest)) {
			std::cerr << "Failed to send upload directory request." << std::endl;
			return false;
		}

		// Nhận phản hồi từ server
		PacketHeader header;
		PacketUploadResponse uploadResp;
		if (!recvPacket(PacketType::UPLOAD_RESPONSE, header, uploadResp)) {
			std::cerr << "Failed to receive upload response." << std::endl;
			return false;
		}

		// Kiểm tra phản hồi từ server
		if (uploadResp.status != UploadStatus::UPLOAD_ALLOWED) {
			std::cerr << "Server denied the upload." << std::endl;
			return false;
		}

		// Upload từng file trong thư mục
		for (const auto& filePath : filesToUpload) {
			// Gọi hàm uploadFile cho từng file
			std::cout << "Uploading file: " << filePath << std::endl;
			if (!uploadFile(filePath)) {
				std::cerr << "Failed to upload file: " << filePath << std::endl;
				return false;
			}
		}

		std::cout << "Folder upload completed successfully." << std::endl;
		return true;
	}


	bool downloadFile(const std::string& filename)
	{
		// Prepare the download request
		PacketDownloadRequest p_request(filename);

		if (!sendPacket(PacketType::DOWNLOAD_REQUEST, p_request))
		{
			throw std::runtime_error("Failed to send download request.");
		}

		PacketHeader header;
		PacketDownloadResponse p_response;

		if (!recvPacket(PacketType::DOWNLOAD_RESPONSE, header, p_response))
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

		std::ofstream file(filename, std::ios::binary);

		auto start_time = std::chrono::steady_clock::now();
		auto last_time = start_time;
		size_t last_received = 0;

		pbManager->AddFile(filename);

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

			if (!recvPacket(PacketType::FILE_CHUNK, header, fileChunk))
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

			if (!sendPacket(PacketType::FILE_CHUNK_ACK, chunkACK))
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
				pbManager->UpdateProgress(filename, progress);
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
			std::vector<uint8_t> file_checksum = md5Handler->calcCheckSumFile(filename);
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

	bool closeSession()
	{
		PacketCloseSession closeReq = PacketCloseSession();

		if (!sendPacket(PacketType::CLOSE_SESSION, closeReq))
		{
			throw std::runtime_error("Failed to send close session request.");
		}

		std::cout << "Session closed successfully." << std::endl;

		return true;
	}
};

int main()
{
	try
	{
		NetworkClient client;
		client.connect("127.0.0.1", 27015);

		if (client.performHandshake())
		{
			while (true)
			{
				if (client.getSessionId()[0] == 0 ||
					client.getSessionId()[1] == 0 ||
					client.getSessionId()[2] == 0)
				{
					std::string username, password;

					std::cout << "Enter username: ";
					std::getline(std::cin, username);
					std::cout << "Enter password: ";
					std::getline(std::cin, password);

					if (!client.authenticate(username, password))
					{
						std::cerr << "Authentication failed." << std::endl;
						continue;
					}
				}
				else
				{
					std::cout << "Session ID: ";
					for (int i = 0; i < 16; i++)
					{
						printf("%02X", client.getSessionId()[i]);
					}
					std::cout << std::endl;
				}

				int choice;
				std::string filename;

				std::cout << "1. Upload file" << std::endl;
				std::cout << "2. Download file" << std::endl;
				std::cout << "Enter your choice: ";
				std::cin >> choice;
				std::cin.ignore();

				if (choice != 1 && choice != 2)
				{
					std::cerr << "Invalid choice." << std::endl
						<< std::endl;
					continue;
				}

				try
				{
					if (choice == 1)
					{
						std::cout << "Enter the file name to upload: ";
						std::getline(std::cin, filename);

						client.uploadFile(filename);
					}
					else if (choice == 2)
					{
						std::cout << "Enter the file name to download: ";
						std::getline(std::cin, filename);

						client.downloadFile(filename);
					}
				}
				catch (const std::exception& e)
				{
					std::cerr << "Error: " << e.what() << std::endl;
				}

				std::cout << "Do you want to close the session? (Y/N): ";
				char ch;
				std::cin >> ch;
				std::cin.ignore();

				if (ch == 'Y' || ch == 'y')
				{
					client.closeSession();
					break;
				}
				else
				{
					std::cout << "Session will remain open." << std::endl;
				}
			}
		}
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
		return -1;
	}

	return 0;
}