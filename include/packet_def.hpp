#ifndef PACKET_DEF_H
#define PACKET_DEF_H

#include <chrono>
#include <cstdint>
#include <cstring> // For memcpy
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Platform-specific headers
constexpr auto PACKET_MAGIC_NUMBER = 0x5A57;   // Magic number to validate the packet - 0x5A57 (23127)
constexpr auto MAX_USERNAME_LENGTH = 64;	   // Maximum username length
constexpr auto MAX_PASSWORD_LENGTH = 64;	   // Maximum password length;
constexpr auto MAX_FILE_NAME_LENGTH = 512;	   // Maximum file name length;
constexpr auto MAX_DIR_PATH_LENGTH = 512;	   // Maximum directory name length
constexpr auto MAX_MESSAGE_LENGTH = 256;	   // Maximum message length
constexpr auto MAX_ERROR_MESSAGE_LENGTH = 512; // Maximum error message length
// These limitations are used to prevet	buffer overflow attacks

enum class PacketType : uint8_t
{
	HANDSHAKE_REQUEST,	// Handshake request
	HANDSHAKE_RESPONSE, // Handshake response

	AUTHENTICATION_REQUEST,	 // Authentication request
	AUTHENTICATION_RESPONSE, // Authentication response

	CREATE_DIR_REQUEST,	 // Create a directory
	CREATE_DIR_RESPONSE, // Create directory response

	VIEW_CLOUD_REQUEST,	 // View cloud request
	VIEW_CLOUD_RESPONSE, // View cloud response

	UPLOAD_REQUEST,		// Upload a file
	UPLOAD_DIR_REQUEST, // Upload a directory
	UPLOAD_RESPONSE,	// Upload response

	DOWNLOAD_REQUEST,  // Download a file
	DOWNLOAD_RESPONSE, // Download response

	RESUME_UPLOAD_REQUEST,	 // Resume upload request
	RESUME_RESPONSE, // Resume upload response

	FILE_CHUNK,		// File chunk
	FILE_CHUNK_ACK, // File chunk acknowledgment

	CLOSE_SESSION, // Close the session
	ERR_PACKET	   // Error packet
};

struct PacketPrefix
{
	uint32_t encrypted_packet_length; // Encrypted packet length (4 bytes)
};

struct PacketHeader
{
	uint16_t magic_number;	 // To validate the packet (2 bytes)
	uint8_t version = 1;	 // Packet version (1 byte)
	PacketType packet_type;	 // Packet type (1 byte)
	uint8_t session_id[16];	 // Session ID (unique identifier) (16 bytes)
	uint32_t payload_length; // Payload length (4 bytes)
	// Total size: 24 bytes

	// Default constructor
	PacketHeader() : magic_number(PACKET_MAGIC_NUMBER),
		version(1),
		packet_type(PacketType::ERR_PACKET),
		session_id{ 0 },
		payload_length(0)
	{
	}

	// Constructor with packet type and payload length
	PacketHeader(PacketType type, uint32_t length) : magic_number(PACKET_MAGIC_NUMBER),
		version(1),
		packet_type(type),
		session_id{ 0 },
		payload_length(length)
	{
	}

	// Constructor with packet type, session ID, and payload length
	PacketHeader(PacketType type, const uint8_t* session_id, uint32_t length)
		: magic_number(PACKET_MAGIC_NUMBER),
		version(1),
		packet_type(type),
		session_id{ 0 },
		payload_length(length)
	{
		if (session_id)
		{
			memcpy(this->session_id, session_id, 16);
		}
	}

	std::vector<uint8_t> serialize() const
	{
		std::vector<uint8_t> buffer(sizeof(PacketHeader));
		memcpy(buffer.data(), this, sizeof(PacketHeader));
		return buffer;
	}

	static PacketHeader deserialize(const uint8_t* data, size_t size)
	{
		if (size < sizeof(PacketHeader))
			throw std::runtime_error("Insufficient data for PacketHeader deserialization");

		PacketHeader header{};
		memcpy(&header, data, sizeof(PacketHeader));
		return header;
	}

	bool IsValid() const
	{
		return (magic_number == PACKET_MAGIC_NUMBER) && (version == 1);
	}

	void SetSessionID(const uint8_t* new_session_id)
	{
		if (new_session_id)
		{
			memcpy(session_id, new_session_id, 16);
		}
	}

	uint8_t* GetSessionID() const
	{
		return const_cast<uint8_t*>(session_id);
	}

	bool ValidateSessionID(const uint8_t* session_id_) const
	{
		return memcmp(this->session_id, session_id_, 16) == 0;
	}
};

struct PacketHandshakeRequest
{
	uint8_t client_version; // Client version (1 byte)

	std::vector<uint8_t> serialize() const
	{
		std::vector<uint8_t> buffer(sizeof(PacketHandshakeRequest));
		memcpy(buffer.data(), this, sizeof(PacketHandshakeRequest));
		return buffer;
	}

	static PacketHandshakeRequest deserialize(const uint8_t* data, size_t size)
	{
		if (size < sizeof(PacketHandshakeRequest))
			throw std::runtime_error("Insufficient data for PacketHandshakeRequest deserialization");

		PacketHandshakeRequest request{};
		memcpy(&request, data, sizeof(PacketHandshakeRequest));
		return request;
	}
};

struct PacketHandshakeResponse
{
	uint8_t server_version; // Server version (1 byte)
	// Additional data can be added here if needed
	// For example, the server can send new version information to the client
	// The client can then decide to update or not
	uint16_t message_length; // Message length (2 bytes)
	std::string message;	 // Message (e.g., "Handshake successful")
	// This message can be used for debugging purposes

	PacketHandshakeResponse()
		: server_version(1),
		message_length(0),
		message()
	{
	}

	PacketHandshakeResponse(uint8_t version, const std::string& msg)
		: server_version(version),
		message_length(static_cast<uint16_t>(msg.length())),
		message(msg)
	{
	}

	std::vector<uint8_t> serialize() const
	{
		if (message.length() > UINT16_MAX)
			throw std::runtime_error("Message length exceeds the maximum value");

		size_t total_size = sizeof(server_version) + sizeof(message_length) + message.length();
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.push_back(server_version);

		// Serialize message length
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&message_length),
			reinterpret_cast<const uint8_t*>(&message_length) + sizeof(message_length));

		// Serialize variable-size fields
		buffer.insert(buffer.end(), message.begin(), message.end());

		return buffer;
	}

	static PacketHandshakeResponse deserialize(const uint8_t* data, size_t size)
	{
		if (size < sizeof(server_version) + sizeof(message_length))
			throw std::runtime_error("Insufficient data for PacketHandshakeResponse deserialization");

		PacketHandshakeResponse response{};
		size_t offset = 0;

		// Deserialize fixed-size fields
		response.server_version = static_cast<uint8_t>(data[offset]);
		offset += sizeof(response.server_version);

		// Deserialize message length
		memcpy(&response.message_length, data + offset, sizeof(response.message_length));
		offset += sizeof(response.message_length);

		if (size < offset + response.message_length)
			throw std::runtime_error("Insufficient data for PacketHandshakeResponse deserialization");

		// Deserialize variable-size fields
		response.message.assign(reinterpret_cast<const char*>(data + offset), response.message_length);

		return response;
	}
};

struct PacketAuthenticationRequest
{
	char username[MAX_USERNAME_LENGTH];
	char password[MAX_PASSWORD_LENGTH];

	PacketAuthenticationRequest() : username(), password() {}

	PacketAuthenticationRequest(const std::string& user, const std::string& pass)
	{
		if (user.length() > MAX_USERNAME_LENGTH)
			throw std::runtime_error("Username length exceeds the maximum value");

		if (pass.length() > MAX_PASSWORD_LENGTH)
			throw std::runtime_error("Password length exceeds the maximum value");

		strncpy_s(username, user.c_str(), MAX_USERNAME_LENGTH);
		strncpy_s(password, pass.c_str(), MAX_PASSWORD_LENGTH);
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(PacketAuthenticationRequest);
		std::vector<uint8_t> buffer(total_size);
		memcpy(buffer.data(), this, total_size);
		return buffer;
	}

	static PacketAuthenticationRequest deserialize(const uint8_t* data, size_t size)
	{
		if (size < sizeof(PacketAuthenticationRequest))
			throw std::runtime_error("Insufficient data for PacketAuthenticationRequest deserialization");

		PacketAuthenticationRequest request{};
		memcpy(&request, data, sizeof(PacketAuthenticationRequest));
		return request;
	}
};

struct PacketAuthenticationResponse
{
	bool authenticated;		 // Authentication status (true if successful) - (1 byte)
	uint8_t session_id[16];	 // Server will generate a session ID for the client and send it back - (16 bytes)
	uint16_t message_length; // Message length (2 bytes)
	std::string message;	 // Message (e.g., "Authentication successful")
	// This message can be used for debugging purposes

	PacketAuthenticationResponse()
		: authenticated(false),
		session_id{ 0 },
		message_length(0),
		message()
	{
	}

	PacketAuthenticationResponse(bool auth, const uint8_t* session_id, const std::string& msg)
		: authenticated(auth),
		session_id{ 0 },
		message_length(static_cast<uint16_t>(msg.length())),
		message(msg)
	{
		if (session_id)
		{
			memcpy(this->session_id, session_id, 16);
		}
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(authenticated) + sizeof(session_id) + sizeof(message_length) + message.length();
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		buffer.push_back(authenticated);
		buffer.insert(buffer.end(), session_id, session_id + 16);

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&message_length),
			reinterpret_cast<const uint8_t*>(&message_length) + sizeof(message_length));

		buffer.insert(buffer.end(), message.begin(), message.end());

		return buffer;
	}

	static PacketAuthenticationResponse deserialize(const uint8_t* data, size_t size)
	{
		if (size < sizeof(authenticated) + sizeof(session_id))
			throw std::runtime_error("Insufficient data for PacketAuthenticationResponse deserialization");

		PacketAuthenticationResponse response{};
		size_t offset = 0;

		response.authenticated = static_cast<bool>(data[offset]);
		offset += sizeof(response.authenticated);

		memcpy(response.session_id, data + offset, 16);
		offset += 16;

		if (size < offset + sizeof(response.message_length))
			throw std::runtime_error("Insufficient data for PacketAuthenticationResponse deserialization");

		response.message.assign(reinterpret_cast<const char*>(data + offset), response.message_length);

		return response;
	}
};

struct FileEntryDTO
{
	uint64_t file_size;		   // File size (in bytes) - (8 bytes)
	uint8_t is_dir;			   // Is directory (1 byte)
	uint16_t file_path_length; // File path length - (2 bytes)
	uint16_t file_name_length; // File name length - (2 bytes)

	std::string file_path; // File path
	std::string file_name; // File name

	// Total size: 23 bytes (fixed-size fields) + variable-size fields

	FileEntryDTO() : file_size(0),
		is_dir(0),
		file_path_length(0),
		file_name_length(0),
		file_path(),
		file_name()
	{
	}

	FileEntryDTO(const std::string& path, const std::string& name,
		uint64_t size, bool is_directory)
		: file_size(size),
		is_dir(is_directory ? 1 : 0),
		file_path_length(static_cast<uint16_t>(path.length())),
		file_name_length(static_cast<uint16_t>(name.length())),
		file_path(path),
		file_name(name)
	{
	}

	size_t GetSize() const
	{
		return sizeof(file_size) + sizeof(is_dir) +
			sizeof(file_path_length) + sizeof(file_name_length) +
			file_path.size() + file_name.size();
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = GetSize();
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_size),
			reinterpret_cast<const uint8_t*>(&file_size) + sizeof(file_size));

		buffer.push_back(is_dir);

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_path_length),
			reinterpret_cast<const uint8_t*>(&file_path_length) + sizeof(file_path_length));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_name_length),
			reinterpret_cast<const uint8_t*>(&file_name_length) + sizeof(file_name_length));

		// Serialize variable-size fields
		buffer.insert(buffer.end(), file_path.begin(), file_path.end());
		buffer.insert(buffer.end(), file_name.begin(), file_name.end());

		return buffer;
	}

	static FileEntryDTO deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(uint64_t) + sizeof(uint64_t) + sizeof(uint8_t) +
			sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for FileEntry deserialization");

		FileEntryDTO entry{};

		// Deserialize fixed-size fields
		memcpy(&entry.file_size, data + offset, sizeof(entry.file_size));
		offset += sizeof(entry.file_size);

		entry.is_dir = data[offset++];

		memcpy(&entry.file_path_length, data + offset, sizeof(entry.file_path_length));
		offset += sizeof(entry.file_path_length);

		memcpy(&entry.file_name_length, data + offset, sizeof(entry.file_name_length));
		offset += sizeof(entry.file_name_length);

		// Calculate expected size
		size_t expected_size = fixed_size + entry.file_path_length + entry.file_name_length;

		if (size < expected_size)
			throw std::runtime_error("Insufficient data for FileEntry deserialization");

		// Deserialize variable-size fields
		entry.file_path.assign(data + offset, data + offset + entry.file_path_length);
		offset += entry.file_path_length;

		entry.file_name.assign(data + offset, data + offset + entry.file_name_length);
		// No need to increment offset because this is the last field

		return entry;
	}

	std::string GetFilePath() const { return file_path; }
	std::string GetFileName() const { return file_name; }
	bool IsDirectory() const { return is_dir != 0; }
};

// No need to define PacketViewCloudRequest because it does not have any payload

struct PacketViewCloudResponse
{
	uint32_t file_count; // Số lượng file trong không gian lưu trữ của User - 8 bytes
	uint64_t total_size; // Tổng kích thước của không gian lưu trữ - 8 bytes

	std::vector<uint8_t> serialize(const std::vector<FileEntryDTO>& file_entries)
	{
		total_size = 0;
		file_count = static_cast<uint32_t>(file_entries.size());

		size_t entries_size = 0;
		for (const auto& entry : file_entries)
		{
			entries_size += entry.GetSize();
			total_size += entry.file_size;
		}

		size_t buffer_size = sizeof(file_count) + sizeof(total_size) + entries_size;

		std::vector<uint8_t> buffer;
		buffer.reserve(buffer_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_count),
			reinterpret_cast<const uint8_t*>(&file_count) + sizeof(file_count));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&total_size),
			reinterpret_cast<const uint8_t*>(&total_size) + sizeof(total_size));

		// Serialize variable-size fields
		for (const auto& entry : file_entries)
		{
			std::vector<uint8_t> entry_data = entry.serialize();
			buffer.insert(buffer.end(), entry_data.begin(), entry_data.end());
		}

		return buffer;
	}

	static std::pair<PacketViewCloudResponse, std::vector<FileEntryDTO>> deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(uint32_t) + sizeof(uint64_t);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketViewCloudRespose deserialization");

		PacketViewCloudResponse response{};

		// Deserialize fixed-size fields
		memcpy(&response.file_count, data + offset, sizeof(response.file_count));
		offset += sizeof(response.file_count);

		memcpy(&response.total_size, data + offset, sizeof(response.total_size));
		offset += sizeof(response.total_size);

		// Deserialize variable-size fields
		std::vector<FileEntryDTO> entries;

		for (uint32_t i = 0; i < response.file_count; ++i)
		{
			if (offset >= size)
				throw std::runtime_error("Unexpected end of data for FileEntry deserialization");

			FileEntryDTO entry = FileEntryDTO::deserialize(data + offset, size - offset);
			entries.emplace_back(entry);
			offset += entry.GetSize();
		}

		return { response, entries };
	}
};

struct PacketCreateDirRequest
{
	uint16_t dir_path_length; // Directory path length - (2 bytes)
	std::string dir_path;	  // Directory path
	// Total size: 2 bytes (fixed-size fields) + variable-size fields

	PacketCreateDirRequest() : dir_path_length(0), dir_path() {}

	PacketCreateDirRequest(const std::string& path)
		: dir_path_length(static_cast<uint16_t>(path.length())),
		dir_path(path)
	{
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(dir_path_length) + dir_path.length();
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&dir_path_length),
			reinterpret_cast<const uint8_t*>(&dir_path_length) + sizeof(dir_path_length));

		// Serialize variable-size fields
		buffer.insert(buffer.end(), dir_path.begin(), dir_path.end());

		return buffer;
	}

	static PacketCreateDirRequest deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(uint16_t);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketCreateDirRequest deserialization");

		PacketCreateDirRequest request{};

		// Deserialize fixed-size fields
		memcpy(&request.dir_path_length, data + offset, sizeof(request.dir_path_length));
		offset += sizeof(request.dir_path_length);

		// Calculate expected size
		size_t expected_size = fixed_size + request.dir_path_length;

		if (size < expected_size)
			throw std::runtime_error("Insufficient data for PacketCreateDirRequest deserialization");

		// Deserialize variable-size fields
		request.dir_path.assign(reinterpret_cast<const char*>(data + offset), request.dir_path_length);

		return request;
	}
};

struct PacketCreateDirResponse
{
	bool created;			 // Directory creation status (true if successful) - (1 byte)
	uint16_t message_length; // Message length (2 bytes)
	std::string message;	 // Message (e.g., "Directory created successfully")
	// This message can be used for debugging purposes

	PacketCreateDirResponse() : created(true), message_length(0), message() {}

	PacketCreateDirResponse(bool success, const std::string& msg)
		: created(success),
		message_length(static_cast<uint16_t>(msg.length())),
		message(msg)
	{
	}

	std::vector<uint8_t> serialize() const
	{
		if (message.length() > UINT16_MAX)
			throw std::runtime_error("Message length exceeds the maximum value");

		size_t total_size = sizeof(created) + sizeof(message_length) + message.length();
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.push_back(created);

		// Serialize message length
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&message_length),
			reinterpret_cast<const uint8_t*>(&message_length) + sizeof(message_length));

		// Serialize variable-size fields
		buffer.insert(buffer.end(), message.begin(), message.end());

		return buffer;
	}

	static PacketCreateDirResponse deserialize(const uint8_t* data, size_t size)
	{
		if (size < sizeof(created) + sizeof(message_length))
			throw std::runtime_error("Insufficient data for PacketCreateDirResponse deserialization");

		PacketCreateDirResponse response{};
		size_t offset = 0;

		// Deserialize fixed-size fields
		response.created = static_cast<bool>(data[offset]);
		offset += sizeof(response.created);

		// Deserialize message length
		memcpy(&response.message_length, data + offset, sizeof(response.message_length));
		offset += sizeof(response.message_length);

		if (size < offset + response.message_length)
			throw std::runtime_error("Insufficient data for PacketCreateDirResponse deserialization");

		// Deserialize variable-size fields
		response.message.assign(reinterpret_cast<const char*>(data + offset), response.message_length);

		return response;
	}
};

struct PacketUploadRequest
{
	uint64_t file_size;		   // File size (in bytes) - (8 bytes)
	uint8_t checksum[16];	   // Checksum of the file - (16 bytes)
	uint16_t file_name_length; // File name length - (2 bytes)
	uint16_t file_type_length; // File type length - (2 bytes)

	std::string file_name; // File name
	std::string file_type; // File type

	// Total size: 28 bytes (fixed-size fields) + variable-size fields

	PacketUploadRequest()
		: file_size(0),
		checksum{ 0 },
		file_name_length(0),
		file_type_length(0),
		file_name(),
		file_type()
	{
	}

	PacketUploadRequest(const std::string& name, const std::string& type,
		uint64_t size, const uint8_t* checksum)
		: file_size(size),
		checksum{ 0 },
		file_name_length(static_cast<uint16_t>(name.length())),
		file_type_length(static_cast<uint16_t>(type.length())),
		file_name(name),
		file_type(type)
	{
		if (checksum)
		{
			memcpy(this->checksum, checksum, 16);
		}
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(file_size) + sizeof(checksum) +
			sizeof(file_name_length) + sizeof(file_type_length) +
			file_name.length() + file_type.length();

		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_size),
			reinterpret_cast<const uint8_t*>(&file_size) + sizeof(file_size));

		buffer.insert(buffer.end(), checksum, checksum + 16);

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_name_length),
			reinterpret_cast<const uint8_t*>(&file_name_length) + sizeof(file_name_length));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_type_length),
			reinterpret_cast<const uint8_t*>(&file_type_length) + sizeof(file_type_length));

		// Serialize variable-size fields
		buffer.insert(buffer.end(), file_name.begin(), file_name.end());
		buffer.insert(buffer.end(), file_type.begin(), file_type.end());

		return buffer;
	}

	static PacketUploadRequest deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(file_size) + sizeof(checksum) +
			sizeof(file_name_length) + sizeof(file_type_length);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketUploadRequest deserialization");

		PacketUploadRequest request{};

		// Deserialize fixed-size fields
		memcpy(&request.file_size, data + offset, sizeof(request.file_size));
		offset += sizeof(request.file_size);

		memcpy(request.checksum, data + offset, sizeof(request.checksum));
		offset += sizeof(request.checksum); // 16 bytes

		memcpy(&request.file_name_length, data + offset, sizeof(request.file_name_length));
		offset += sizeof(request.file_name_length);

		memcpy(&request.file_type_length, data + offset, sizeof(request.file_type_length));
		offset += sizeof(request.file_type_length);

		// Calculate expected size
		size_t expected_size = fixed_size + request.file_name_length + request.file_type_length;

		if (size < expected_size)
			throw std::runtime_error("Insufficient data for PacketUploadRequest deserialization");

		// Deserialize variable-size fields
		request.file_name.assign(reinterpret_cast<const char*>(data + offset), request.file_name_length);
		offset += request.file_name_length;

		request.file_type.assign(reinterpret_cast<const char*>(data + offset), request.file_type_length);
		// No need to increment offset because this is the last field

		return request;
	}
};

struct PacketUploadDirRequest
{
	uint32_t file_count = 0;	  // Số lượng file trong thư mục - 4 bytes (cũng là số lượng FileEntry)
	uint64_t total_size = 0;	  // Tổng kích thước của thư mục - 8 bytes
	uint8_t checksum_flag = 0;	  // Cờ kiểm tra checksum - 1 byte (0: không kiểm tra, 1: kiểm tra)
	uint16_t dir_path_length = 0; // Độ dài của đường dẫn thư mục - 2 bytes
	std::string dir_path;		  // Đường dẫn thư mục

	// Total size: 15 bytes (fixed-size fields) + variable-size fields

	PacketUploadDirRequest() : file_count(0), total_size(0), checksum_flag(0), dir_path_length(0), dir_path() {}

	PacketUploadDirRequest(const std::string& path, uint32_t count, uint64_t size, uint8_t flag)
		: file_count(count),
		total_size(size),
		checksum_flag(flag),
		dir_path_length(static_cast<uint16_t>(path.length())),
		dir_path(path)
	{
	}

	std::vector<uint8_t> serialize() const
	{
		size_t l_total_size = sizeof(file_count) + sizeof(l_total_size) +
			sizeof(checksum_flag) + sizeof(dir_path_length) +
			dir_path.length();

		std::vector<uint8_t> buffer;
		buffer.reserve(l_total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_count),
			reinterpret_cast<const uint8_t*>(&file_count) + sizeof(file_count));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&l_total_size),
			reinterpret_cast<const uint8_t*>(&l_total_size) + sizeof(l_total_size));

		buffer.push_back(checksum_flag);

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&dir_path_length),
			reinterpret_cast<const uint8_t*>(&dir_path_length) + sizeof(dir_path_length));

		// Serialize variable-size fields
		buffer.insert(buffer.end(), dir_path.begin(), dir_path.end());

		return buffer;
	}

	static PacketUploadDirRequest deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(file_count) + sizeof(total_size) +
			sizeof(checksum_flag) + sizeof(dir_path_length);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketUploadDirRequest deserialization");

		PacketUploadDirRequest request{};

		// Deserialize fixed-size fields
		memcpy(&request.file_count, data + offset, sizeof(request.file_count));
		offset += sizeof(request.file_count);

		memcpy(&request.total_size, data + offset, sizeof(request.total_size));
		offset += sizeof(request.total_size);

		request.checksum_flag = data[offset++];

		memcpy(&request.dir_path_length, data + offset, sizeof(request.dir_path_length));
		offset += sizeof(request.dir_path_length);

		// Calculate expected size
		size_t expected_size = fixed_size + request.dir_path_length;

		if (size < expected_size)
			throw std::runtime_error("Insufficient data for PacketUploadDirRequest deserialization");

		// Deserialize variable-size fields
		request.dir_path.assign(reinterpret_cast<const char*>(data + offset), request.dir_path_length);

		return request;
	}
};

enum class UploadStatus : uint8_t
{
	UPLOAD_ALLOWED, // Upload is allowed
	OUT_OF_SPACE	// Server is out of space
};

struct PacketUploadResponse
{
	UploadStatus status; // Upload status (1 byte)

	union
	{
		// Case UploadStatus::UPLOAD_ALLOWED
		struct UploadAllowed
		{
			uint32_t file_id;	 // File ID (unique identifier) - (4 bytes)
			uint32_t chunk_size; // Chunk size (in bytes) - (4 bytes)
			// Total size: 9 bytes
		} upload_allowed;

		// Case UploadStatus::OUT_OF_SPACE
		struct OutOfSpace
		{
			char message[MAX_MESSAGE_LENGTH]; // Message - (256 bytes)
			// Total size: 256 bytes
		} out_of_space;
	};

	PacketUploadResponse() : status(UploadStatus::UPLOAD_ALLOWED), upload_allowed{ 0, 0 } {}

	PacketUploadResponse(UploadStatus status, uint32_t file_id, uint32_t chunk_size)
		: status(status)
	{
		if (status == UploadStatus::UPLOAD_ALLOWED)
		{
			upload_allowed.file_id = file_id;
			upload_allowed.chunk_size = chunk_size;
		}
	}

	PacketUploadResponse(UploadStatus status, const std::string& msg)
		: status(status)
	{
		if (status == UploadStatus::OUT_OF_SPACE)
		{
			strncpy_s(out_of_space.message, msg.c_str(), MAX_MESSAGE_LENGTH);
		}
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = 1; // status

		if (status == UploadStatus::UPLOAD_ALLOWED)
		{
			total_size += sizeof(upload_allowed);
		}
		else if (status == UploadStatus::OUT_OF_SPACE)
		{
			total_size += sizeof(out_of_space);
		}

		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.push_back(static_cast<uint8_t>(status));

		if (status == UploadStatus::UPLOAD_ALLOWED)
		{
			buffer.insert(buffer.end(),
				reinterpret_cast<const uint8_t*>(&upload_allowed),
				reinterpret_cast<const uint8_t*>(&upload_allowed) + sizeof(upload_allowed));
		}
		else if (status == UploadStatus::OUT_OF_SPACE)
		{
			buffer.insert(buffer.end(),
				reinterpret_cast<const uint8_t*>(&out_of_space),
				reinterpret_cast<const uint8_t*>(&out_of_space) + sizeof(out_of_space));
		}

		return buffer;
	}

	static PacketUploadResponse deserialize(const uint8_t* data, size_t size)
	{
		if (size < 1)
			throw std::runtime_error("Insufficient data for PacketUploadResponse deserialization");

		PacketUploadResponse response{};
		size_t offset = 0;

		response.status = static_cast<UploadStatus>(data[offset++]);

		if (response.status == UploadStatus::UPLOAD_ALLOWED)
		{
			if (size < offset + sizeof(response.upload_allowed))
				throw std::runtime_error("Insufficient data for PacketUploadResponse deserialization");

			memcpy(&response.upload_allowed, data + offset, sizeof(response.upload_allowed));
		}
		else if (response.status == UploadStatus::OUT_OF_SPACE)
		{
			if (size < offset + sizeof(response.out_of_space))
				throw std::runtime_error("Insufficient data for PacketUploadResponse deserialization");

			memcpy(&response.out_of_space, data + offset, sizeof(response.out_of_space));
		}

		return response;
	}
};

struct PacketDownloadRequest
{
	uint16_t file_name_length; // File name length - (2 bytes)
	std::string file_name;	   // File name
	// Total size: 2 bytes (fixed-size fields) + variable-size fields

	PacketDownloadRequest() : file_name_length(0), file_name() {}

	PacketDownloadRequest(const std::string& name)
		: file_name_length(static_cast<uint16_t>(name.length())),
		file_name(name)
	{
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(file_name_length) + file_name.length();
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_name_length),
			reinterpret_cast<const uint8_t*>(&file_name_length) + sizeof(file_name_length));

		// Serialize variable-size fields
		buffer.insert(buffer.end(), file_name.begin(), file_name.end());

		return buffer;
	}

	static PacketDownloadRequest deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(file_name_length);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketDownloadRequest deserialization");

		PacketDownloadRequest request{};

		// Deserialize fixed-size fields
		memcpy(&request.file_name_length, data + offset, sizeof(request.file_name_length));
		offset += sizeof(request.file_name_length);

		// Calculate expected size
		size_t expected_size = fixed_size + request.file_name_length;

		if (size < expected_size)
			throw std::runtime_error("Insufficient data for PacketDownloadRequest deserialization");

		// Deserialize variable-size fields
		request.file_name.assign(reinterpret_cast<const char*>(data + offset), request.file_name_length);

		return request;
	}
};

enum class DownloadStatus : uint8_t
{
	FILE_FOUND,		   // File found
	FILE_NOT_FOUND,	   // File not found
	FILE_ACCESS_DENIED // File access denied
};

struct PacketDownloadResponse
{
	DownloadStatus status; // Download status (1 byte)

	union
	{
		// Case DownloadStatus::FILE_FOUND
		struct
		{
			uint32_t file_id;	  // File ID (unique identifier) - (4 bytes)
			uint64_t file_size;	  // File size (in bytes) - (8 bytes)
			uint32_t chunk_size;  // Chunk size (in bytes) - (4 bytes)
			uint8_t checksum[16]; // Checksum of the file - (16 bytes)
			// Total size: 32 bytes
		} file_info;

		// Case DownloadStatus::FILE_NOT_FOUND or DownloadStatus::FILE_ACCESS_DENIED
		struct
		{
			char message[MAX_MESSAGE_LENGTH]; // Message - (256 bytes)
			// Total size: 256 bytes
		} error_info;
	};

	PacketDownloadResponse() : status(DownloadStatus::FILE_FOUND), file_info{ 0, 0, 0 } {}

	PacketDownloadResponse(DownloadStatus status,
		uint32_t file_id, uint64_t size,
		uint32_t chunk_size, const uint8_t* checksum)
		: status(status)
	{
		if (status == DownloadStatus::FILE_FOUND)
		{
			file_info.file_id = file_id;
			file_info.file_size = size;
			file_info.chunk_size = chunk_size;

			if (checksum)
			{
				memcpy(file_info.checksum, checksum, 16);
			}
		}
	}

	PacketDownloadResponse(DownloadStatus status, const std::string& msg)
		: status(status)
	{
		if (status == DownloadStatus::FILE_NOT_FOUND || status == DownloadStatus::FILE_ACCESS_DENIED)
		{
			strncpy_s(error_info.message, msg.c_str(), MAX_MESSAGE_LENGTH);
		}
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = 1; // status

		if (status == DownloadStatus::FILE_FOUND)
		{
			total_size += sizeof(file_info);
		}
		else if (status == DownloadStatus::FILE_NOT_FOUND || status == DownloadStatus::FILE_ACCESS_DENIED)
		{
			total_size += sizeof(error_info);
		}

		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.push_back(static_cast<uint8_t>(status));

		if (status == DownloadStatus::FILE_FOUND)
		{
			buffer.insert(buffer.end(),
				reinterpret_cast<const uint8_t*>(&file_info),
				reinterpret_cast<const uint8_t*>(&file_info) + sizeof(file_info));
		}
		else if (status == DownloadStatus::FILE_NOT_FOUND || status == DownloadStatus::FILE_ACCESS_DENIED)
		{
			buffer.insert(buffer.end(),
				reinterpret_cast<const uint8_t*>(&error_info),
				reinterpret_cast<const uint8_t*>(&error_info) + sizeof(error_info));
		}

		return buffer;
	}

	static PacketDownloadResponse deserialize(const uint8_t* data, size_t size)
	{
		if (size < 1)
			throw std::runtime_error("Insufficient data for PacketDownloadResponse deserialization");

		PacketDownloadResponse response{};
		size_t offset = 0;

		response.status = static_cast<DownloadStatus>(data[offset++]);

		if (response.status == DownloadStatus::FILE_FOUND)
		{
			if (size < offset + sizeof(response.file_info))
				throw std::runtime_error("Insufficient data for PacketDownloadResponse deserialization");

			memcpy(&response.file_info, data + offset, sizeof(response.file_info));
		}
		else if (response.status == DownloadStatus::FILE_NOT_FOUND || response.status == DownloadStatus::FILE_ACCESS_DENIED)
		{
			if (size < offset + sizeof(response.error_info))
				throw std::runtime_error("Insufficient data for PacketDownloadResponse deserialization");

			memcpy(&response.error_info, data + offset, sizeof(response.error_info));
		}

		return response;
	}
};

struct PacketResumeRequest
{
	uint32_t file_id;		   // File ID (unique identifier) - (4 bytes)
	uint64_t resume_position;  // Resume position (in bytes) - (8 bytes)
	uint32_t last_chunk_index; // Last chunk index (0-based) - (4 bytes)
	// Total size: 16 bytes

	PacketResumeRequest() : file_id(0), resume_position(0), last_chunk_index(0) {}

	PacketResumeRequest(uint32_t id, uint64_t position, uint32_t index)
		: file_id(id),
		resume_position(position),
		last_chunk_index(index)
	{
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(file_id) + sizeof(resume_position) + sizeof(last_chunk_index);
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_id),
			reinterpret_cast<const uint8_t*>(&file_id) + sizeof(file_id));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&resume_position),
			reinterpret_cast<const uint8_t*>(&resume_position) + sizeof(resume_position));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&last_chunk_index),
			reinterpret_cast<const uint8_t*>(&last_chunk_index) + sizeof(last_chunk_index));

		return buffer;
	}

	static PacketResumeRequest deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(file_id) + sizeof(resume_position) + sizeof(last_chunk_index);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketResumeRequest deserialization");

		PacketResumeRequest request{};

		// Deserialize fixed-size fields
		memcpy(&request.file_id, data + offset, sizeof(request.file_id));
		offset += sizeof(request.file_id);

		memcpy(&request.resume_position, data + offset, sizeof(request.resume_position));
		offset += sizeof(request.resume_position);

		memcpy(&request.last_chunk_index, data + offset, sizeof(request.last_chunk_index));
		// No need to increment offset because this is the last field

		return request;
	}
};

enum class ResumeStatus : uint8_t
{
	RESUME_SUPPORTED, // Resume is supported
	RESUME_NOT_FOUND  // File not found
};

struct PacketResumeResponse
{
	ResumeStatus status; // Resume status (1 byte)

	union
	{
		// Case ResumeStatus::RESUME_SUPPORTED
		struct
		{
			uint32_t file_id;				// File ID (unique identifier) - (4 bytes)
			uint64_t resume_position;		// Resume position (in bytes) - (8 bytes)
			uint32_t remaining_chunk_count; // Remaining chunk count - (4 bytes)
			// Total size: 16 bytes
		} resume_allowed;

		// Case ResumeStatus::RESUME_NOT_FOUND
		struct
		{
			char message[MAX_MESSAGE_LENGTH]; // Message - (256 bytes)
			// Total size: 256 bytes
		} resume_not_found;
	};

	PacketResumeResponse() : status(ResumeStatus::RESUME_SUPPORTED), resume_allowed{ 0, 0, 0 } {}

	PacketResumeResponse(ResumeStatus status, uint32_t file_id, uint64_t position, uint32_t count)
		: status(status)
	{
		if (status == ResumeStatus::RESUME_SUPPORTED)
		{
			resume_allowed.file_id = file_id;
			resume_allowed.resume_position = position;
			resume_allowed.remaining_chunk_count = count;
		}
	}

	PacketResumeResponse(ResumeStatus status, const std::string& msg)
		: status(status)
	{
		if (status == ResumeStatus::RESUME_NOT_FOUND)
		{
			strncpy_s(resume_not_found.message, msg.c_str(), MAX_MESSAGE_LENGTH);
		}
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = 1; // status

		if (status == ResumeStatus::RESUME_SUPPORTED)
		{
			total_size += sizeof(resume_allowed);
		}
		else if (status == ResumeStatus::RESUME_NOT_FOUND)
		{
			total_size += sizeof(resume_not_found);
		}

		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.push_back(static_cast<uint8_t>(status));

		if (status == ResumeStatus::RESUME_SUPPORTED)
		{
			buffer.insert(buffer.end(),
				reinterpret_cast<const uint8_t*>(&resume_allowed),
				reinterpret_cast<const uint8_t*>(&resume_allowed) + sizeof(resume_allowed));
		}
		else if (status == ResumeStatus::RESUME_NOT_FOUND)
		{
			buffer.insert(buffer.end(),
				reinterpret_cast<const uint8_t*>(&resume_not_found),
				reinterpret_cast<const uint8_t*>(&resume_not_found) + sizeof(resume_not_found));
		}

		return buffer;
	}

	static PacketResumeResponse deserialize(const uint8_t* data, size_t size)
	{
		if (size < 1)
			throw std::runtime_error("Insufficient data for PacketResumeResponse deserialization");

		PacketResumeResponse response{};
		size_t offset = 0;

		response.status = static_cast<ResumeStatus>(data[offset++]);

		if (response.status == ResumeStatus::RESUME_SUPPORTED)
		{
			if (size < offset + sizeof(response.resume_allowed))
				throw std::runtime_error("Insufficient data for PacketResumeResponse deserialization");

			memcpy(&response.resume_allowed, data + offset, sizeof(response.resume_allowed));
		}
		else if (response.status == ResumeStatus::RESUME_NOT_FOUND)
		{
			if (size < offset + sizeof(response.resume_not_found))
				throw std::runtime_error("Insufficient data for PacketResumeResponse deserialization");

			memcpy(&response.resume_not_found, data + offset, sizeof(response.resume_not_found));
		}

		return response;
	}
};

struct PacketFileChunk
{
	uint32_t file_id;	  // File ID (unique identifier) - (4 bytes)
	uint32_t chunk_index; // Chunk index (0-based) - (4 bytes)
	uint32_t chunk_size;  // Chunk size (in bytes) - (4 bytes)
	uint8_t checksum[16]; // Checksum of the chunk - (16 bytes)

	std::vector<uint8_t> data; // Chunk data
	// Total size: 28 bytes (minimum) + chunk_size

	PacketFileChunk() : file_id(0), chunk_index(0), chunk_size(0), checksum{ 0 }, data() {}

	PacketFileChunk(uint32_t id, uint32_t index, uint32_t size, const uint8_t* checksum, const uint8_t* chunk_data)
		: file_id(id),
		chunk_index(index),
		chunk_size(size)
	{
		if (checksum)
		{
			memcpy(this->checksum, checksum, 16);
		}

		if (chunk_data)
		{
			data.assign(chunk_data, chunk_data + size);
		}
	}

	static size_t GetSizeMetadata()
	{
		return sizeof(file_id) + sizeof(chunk_index) + sizeof(chunk_size) + sizeof(checksum);
	}

	size_t GetSize() const
	{
		return GetSizeMetadata() + data.size();
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(file_id) + sizeof(chunk_index) +
			sizeof(chunk_size) + sizeof(checksum) +
			data.size();

		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_id),
			reinterpret_cast<const uint8_t*>(&file_id) + sizeof(file_id));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&chunk_index),
			reinterpret_cast<const uint8_t*>(&chunk_index) + sizeof(chunk_index));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&chunk_size),
			reinterpret_cast<const uint8_t*>(&chunk_size) + sizeof(chunk_size));

		buffer.insert(buffer.end(), checksum, checksum + 16);

		// Serialize variable-size fields
		buffer.insert(buffer.end(), data.begin(), data.end());

		return buffer;
	}

	static PacketFileChunk deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(file_id) + sizeof(chunk_index) +
			sizeof(chunk_size) + sizeof(checksum);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketFileChunk deserialization");

		PacketFileChunk chunk{};

		// Deserialize fixed-size fields
		memcpy(&chunk.file_id, data + offset, sizeof(chunk.file_id));
		offset += sizeof(chunk.file_id);

		memcpy(&chunk.chunk_index, data + offset, sizeof(chunk.chunk_index));
		offset += sizeof(chunk.chunk_index);

		memcpy(&chunk.chunk_size, data + offset, sizeof(chunk.chunk_size));
		offset += sizeof(chunk.chunk_size);

		memcpy(chunk.checksum, data + offset, sizeof(chunk.checksum));
		offset += sizeof(chunk.checksum);

		// Calculate expected size
		size_t expected_size = fixed_size + chunk.chunk_size;

		if (size < expected_size)
			throw std::runtime_error("Insufficient data for PacketFileChunk deserialization");

		// Deserialize variable-size fields
		chunk.data.assign(reinterpret_cast<const uint8_t*>(data + offset),
			reinterpret_cast<const uint8_t*>(data + offset) + chunk.chunk_size);

		return chunk;
	}
};

struct PacketFileChunkACK
{
	uint32_t file_id;	  // File ID (unique identifier) - (4 bytes)
	uint32_t chunk_index; // Chunk index (0-based) - (4 bytes)
	bool success;		  // Chunk acknowledgment status (true if successful) - (1 byte)
	// Total size: 9 bytes

	PacketFileChunkACK() : file_id(0), chunk_index(0), success(false) {}

	PacketFileChunkACK(uint32_t id, uint32_t index, bool ack)
		: file_id(id),
		chunk_index(index),
		success(ack)
	{
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(file_id) + sizeof(chunk_index) + sizeof(success);

		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&file_id),
			reinterpret_cast<const uint8_t*>(&file_id) + sizeof(file_id));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&chunk_index),
			reinterpret_cast<const uint8_t*>(&chunk_index) + sizeof(chunk_index));

		buffer.push_back(success);

		return buffer;
	}

	static PacketFileChunkACK deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(file_id) + sizeof(chunk_index) + sizeof(success);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketFileChunkACK deserialization");

		PacketFileChunkACK ack{};

		// Deserialize fixed-size fields
		memcpy(&ack.file_id, data + offset, sizeof(ack.file_id));
		offset += sizeof(ack.file_id);

		memcpy(&ack.chunk_index, data + offset, sizeof(ack.chunk_index));
		offset += sizeof(ack.chunk_index);

		ack.success = static_cast<bool>(data[offset]);
		// No need to increment offset because this is the last field

		return ack;
	}
};

// Packet to close the session, for client
struct PacketCloseSession
{
	uint64_t timestamp; // Timestamp when the session is closed (8 bytes)
	// Purpose: To notify the server that the client is closing the session
	// Timestamp will be used to log the session time on the server side

	PacketCloseSession()
		: timestamp(std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::system_clock::now().time_since_epoch())
			.count())
	{
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(timestamp);
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&timestamp),
			reinterpret_cast<const uint8_t*>(&timestamp) + sizeof(timestamp));

		return buffer;
	}

	static PacketCloseSession deserialize(const uint8_t* data, size_t size)
	{
		if (size < sizeof(uint64_t))
			throw std::runtime_error("Insufficient data for PacketCloseSession deserialization");

		PacketCloseSession packet{};
		memcpy(&packet.timestamp, data, sizeof(packet.timestamp));
		return packet;
	}
};

struct PacketError
{
	uint32_t error_code;	   // Error code (4 bytes)
	uint16_t message_length;   // Error message length (2 bytes)
	std::string error_message; // Error message
	// Total size: 6 bytes (fixed-size fields) + variable-size fields

	PacketError() : error_code(0), message_length(0), error_message() {}

	PacketError(uint32_t code, const std::string& msg)
		: error_code(code),
		message_length(static_cast<uint16_t>(msg.length())),
		error_message(msg)
	{
	}

	std::vector<uint8_t> serialize() const
	{
		size_t total_size = sizeof(error_code) + sizeof(message_length) + error_message.length();
		std::vector<uint8_t> buffer;
		buffer.reserve(total_size);

		// Serialize fixed-size fields
		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&error_code),
			reinterpret_cast<const uint8_t*>(&error_code) + sizeof(error_code));

		buffer.insert(buffer.end(),
			reinterpret_cast<const uint8_t*>(&message_length),
			reinterpret_cast<const uint8_t*>(&message_length) + sizeof(message_length));

		// Serialize variable-size fields
		buffer.insert(buffer.end(), error_message.begin(), error_message.end());

		return buffer;
	}

	static PacketError deserialize(const uint8_t* data, size_t size)
	{
		size_t offset = 0;
		size_t fixed_size = sizeof(error_code) + sizeof(message_length);

		if (size < fixed_size)
			throw std::runtime_error("Insufficient data for PacketError deserialization");

		PacketError error{};

		// Deserialize fixed-size fields
		memcpy(&error.error_code, data + offset, sizeof(error.error_code));
		offset += sizeof(error.error_code);

		memcpy(&error.message_length, data + offset, sizeof(error.message_length));
		offset += sizeof(error.message_length);

		// Calculate expected size
		size_t expected_size = fixed_size + error.message_length;

		if (size < expected_size)
			throw std::runtime_error("Insufficient data for PacketError deserialization");

		// Deserialize variable-size fields
		error.error_message.assign(reinterpret_cast<const char*>(data + offset), error.message_length);

		return error;
	}
};

#endif // !PACKET_DEF_H