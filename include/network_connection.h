#ifndef NETWORK_CONNECTION_H
#define NETWORK_CONNECTION_H

#include <packet_helper.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <Winsock2.h>
#include <WS2tcpip.h>

#pragma comment(lib, "Ws2_32.lib")

class NetworkConnection
{
public:
	NetworkConnection();
	~NetworkConnection();

	void Connect(const std::string& server_ip, uint16_t server_port);
	bool Send(const std::vector<uint8_t>& data) const;
	bool Receive(uint8_t* data, size_t size) const;
	void Disconnect();
	bool Reconnect();

	std::pair<std::string, uint16_t> GetServerInfo() const;

	bool IsConnected() const;

	template <typename T>
	bool sendPacket(const PacketType& type, const T& data, const uint8_t* sessionId = nullptr)
	{
		// Tạo gói tin đã tuần tự hoá
		std::vector<uint8_t> packet = PacketHelper::CreatePacket(type, data, sessionId);

		if (packet.empty())
		{
			std::cerr << "Failed to create packet." << std::endl;
			return false;
		}

		// Gửi gói tin
		if (!Send(packet))
		{
			std::cerr << "Failed to send packet." << std::endl;
			return false;
		}

		return true;
	}

	template <typename T>
	bool recvPacket(PacketType expectedType, PacketHeader& header, T& outData)
	{
		// Xử lý prefix và header
		PacketPrefix prefix{};
		if (!Receive(reinterpret_cast<uint8_t*>(&prefix), sizeof(PacketPrefix)))
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

		if (!Receive(encrypted_packet.data(), encrypted_packet.size()))
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

private:
	SOCKET m_socket;
	std::string m_server_ip;
	uint16_t m_server_port;
	bool m_is_connected;

	static constexpr auto MAX_ATTEMPTS = 3;
	static constexpr auto MAX_TIMEOUT = 300; // seconds
	static constexpr size_t MAX_PAYLOAD_SIZE = 1024 * 1024 * 32 + 1024 * 512; // 32 MB + 512KB
};

#endif // !NETWORK_CONNECTION_H