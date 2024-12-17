#ifndef PACKET_HELPER_H
#define PACKET_HELPER_H

#include "packet_def.hpp"
#include "encryption_handler.hpp"
#include <iostream>

// Packet helper namespace
namespace PacketHelper
{
	static std::string secret_key = "84bba3a644f7eb97";
	static security::datasecurity::encryption::AES128GCM aes(secret_key);

	// Tạo một gói tin bao gồm header và payload
	template <typename T>
	std::vector<uint8_t> CreatePacket(PacketType type, const T& data, const uint8_t* session_id = nullptr)
	{
		try
		{
			const auto& payload = data.serialize();

			PacketHeader header(type, session_id, static_cast<uint32_t>(payload.size()));
			std::vector<uint8_t> plain_packet = header.serialize();

			plain_packet.insert(plain_packet.end(), payload.begin(), payload.end());

			// Mã hóa gói tin
			std::vector<uint8_t> iv(12, 0);
			aes.generateRandomBytes(iv);

			std::vector<uint8_t> tag(16, 0);
			std::vector<uint8_t> encrypted_packet;
			aes.encrypt(plain_packet, iv, encrypted_packet, tag);

			// Tạo prefix cho gói tin
			PacketPrefix prefix{};
			prefix.encrypted_packet_length = static_cast<uint32_t>(iv.size() + tag.size() + encrypted_packet.size());

			// Tạo gói tin cuối cùng
			std::vector<uint8_t> final_packet{};
			final_packet.reserve(sizeof(PacketPrefix) + prefix.encrypted_packet_length);

			const uint8_t* prefix_ptr = reinterpret_cast<const uint8_t*>(&prefix);
			final_packet.insert(final_packet.end(), prefix_ptr, prefix_ptr + sizeof(PacketPrefix));

			final_packet.insert(final_packet.end(), iv.begin(), iv.end());

			final_packet.insert(final_packet.end(), tag.begin(), tag.end());

			final_packet.insert(final_packet.end(), encrypted_packet.begin(), encrypted_packet.end());

			return final_packet;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[PacketHelper::CreatePacket] Error: " << e.what() << std::endl;
			return std::vector<uint8_t>();
		}
	}

	static std::vector<uint8_t> DecryptPacket(const uint8_t* data, size_t length)
	{
		try
		{
			// Trich xuất prefix từ buffer để biết kích thước gói tin cần giải mã
			PacketPrefix prefix{};
			memcpy(&prefix, data, sizeof(PacketPrefix));

			if (length < sizeof(PacketPrefix) + prefix.encrypted_packet_length)
			{
				throw std::runtime_error("Insufficient data for packet decryption. Length: " +
					std::to_string(length) + ", Expected: " + std::to_string(sizeof(PacketPrefix) + prefix.encrypted_packet_length));
			}

			// Lấy IV từ buffer
			std::vector<uint8_t> iv(12, 0);
			memcpy(iv.data(), data + sizeof(PacketPrefix), iv.size());

			// Lấy Tag từ buffer
			std::vector<uint8_t> tag(16, 0);
			memcpy(tag.data(), data + sizeof(PacketPrefix) + iv.size(), tag.size());

			// Lấy dữ liệu mã hóa từ buffer
			std::vector<uint8_t> encrypted_packet(prefix.encrypted_packet_length - iv.size() - tag.size(), 0);
			memcpy(encrypted_packet.data(),
				data + sizeof(PacketPrefix) + iv.size() + tag.size(),
				encrypted_packet.size());

			// Giải mã gói tin
			std::vector<uint8_t> decrypted_packet;
			aes.decrypt(encrypted_packet, iv, tag, decrypted_packet);

			return decrypted_packet;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[PacketHelper::DecryptPacket] Error: " << e.what() << std::endl;
			return std::vector<uint8_t>();
		}
	}

	// Phân giải header từ buffer
	static bool DeserializeHeader(const uint8_t* data, size_t length, PacketHeader& out_header)
	{
		if (length < sizeof(PacketHeader))
			return false;

		try
		{
			// Giải mã gói tin
			out_header = PacketHeader::deserialize(data, sizeof(PacketHeader));

			return out_header.IsValid();
		}
		catch (const std::exception& e)
		{
			std::cerr << "[PacketHelper::DeserializeHeader] Error: " << e.what() << std::endl;
			return false;
		}
	}

	/*
	 * @brief Phân giải Payload từ Buffer
	 * @brief Điều kiện tiên quyết phải Phân giải Header trước khi gọi hàm này
	 */
	template <typename T>
	static bool DeserializePayload(const uint8_t* data, size_t length, const PacketHeader& header, T& out_data)
	{
		try
		{
			if (length < header.payload_length)
			{
				throw std::runtime_error("Insufficient data for payload deserialization. Length: " +
					std::to_string(length) + ", Expected: " + std::to_string(header.payload_length));
				return false;
			}

			out_data = T::deserialize(data + sizeof(PacketHeader), header.payload_length);

			return true;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[PacketHelper::DeserializePayload] Error: " << e.what() << std::endl;
			return false;
		}
	}

	// Phân giải gói tin từ buffer
	template <typename T>
	static bool Deserialize(const uint8_t* data, size_t length, PacketHeader& out_header, T& out_data)
	{
		try
		{
			if (!DeserializeHeader(data, length, out_header))
				return false;

			out_data = T::deserialize(data + sizeof(PacketHeader), out_header.payload_length);

			return true;
		}
		catch (const std::exception& e)
		{
			std::cerr << "[PacketHelper::Deserialize] Error: " << e.what() << std::endl;
			return false;
		}
	}

	template <typename T>
	T safe_cast(void* ptr)
	{
		static_assert(std::is_pointer_v<T>, "T must be a pointer type");
		static_assert(
			std::is_same_v<std::remove_pointer_t<T>, char> ||
			std::is_same_v<std::remove_pointer_t<T>, uint8_t>,
			"T must be a pointer to char, uint8_t, or CHAR");
		return reinterpret_cast<T>(ptr);
	}
} // namespace PacketHelper

#endif // !PACKET_HELPER_H