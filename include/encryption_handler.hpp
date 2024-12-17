#ifndef ENCRYPTION_HANDLER_H
#define ENCRYPTION_HANDLER_H

#include <climits>
#include <fstream>
#include <iostream>
#include <openssl/aes.h>
#include <openssl/evp.h>
#include <openssl/md5.h>
#include <openssl/rand.h>
#include <stdexcept>
#include <string>
#include <vector>

namespace security
{
	namespace datasecurity
	{
		namespace encryption
		{
			class AES128GCM
			{
				std::string key;

			public:
				AES128GCM(const std::string& key) : key(key)
				{
					if (key.size() != 16)
					{
						throw std::invalid_argument("Key size for AES-128 must be exactly 16 bytes.");
					}
				}

				~AES128GCM() = default;

				void generateRandomBytes(std::vector<uint8_t>& buffer) const
				{
					if (RAND_bytes(buffer.data(), static_cast<int>(buffer.size())) != 1)
					{
						throw std::runtime_error("Error: Could not generate random bytes.");
					}
				}

				bool encrypt(
					const std::vector<uint8_t>& plaintext,
					const std::vector<uint8_t>& iv,
					std::vector<uint8_t>& ciphertext,
					std::vector<uint8_t>& tag) const
				{
					EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
					if (!ctx)
					{
						throw std::runtime_error("Error: Could not create EVP_CIPHER_CTX.");
					}

					int len;

					if (EVP_EncryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: Could not initialize AES-128 GCM encryption.");
					}

					if (EVP_EncryptInit_ex(ctx, NULL, NULL, reinterpret_cast<const uint8_t*>(key.data()), iv.data()) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: Could not set key and IV for encryption.");
					}

					ciphertext.resize(plaintext.size());

					if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: AES-128 GCM encryption failed.");
					}
					int ciphertext_len = len;

					if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: AES-128 GCM encryption finalization failed.");
					}
					ciphertext_len += len;
					ciphertext.resize(ciphertext_len);

					tag.resize(16);
					if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(tag.size()), tag.data()) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: Failed to get GCM authentication tag.");
					}

					EVP_CIPHER_CTX_free(ctx);

					return true;
				}

				bool decrypt(
					const std::vector<uint8_t>& ciphertext,
					const std::vector<uint8_t>& iv,
					const std::vector<uint8_t>& tag,
					std::vector<uint8_t>& plaintext) const
				{
					EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
					if (!ctx)
					{
						throw std::runtime_error("Error: Could not create EVP_CIPHER_CTX.");
					}

					int len;
					int plaintext_len;

					if (EVP_DecryptInit_ex(ctx, EVP_aes_128_gcm(), NULL, NULL, NULL) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: Could not initialize AES-128 GCM decryption.");
					}

					if (EVP_DecryptInit_ex(ctx, NULL, NULL, reinterpret_cast<const uint8_t*>(key.data()), iv.data()) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: Could not set key and IV for decryption.");
					}

					plaintext.resize(ciphertext.size());

					if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: AES-128 GCM decryption failed.");
					}
					plaintext_len = len;

					if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(tag.size()), const_cast<uint8_t*>(tag.data())) != 1)
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: Failed to set GCM authentication tag.");
					}

					int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
					if (ret > 0)
					{
						plaintext_len += len;
						plaintext.resize(plaintext_len);
					}
					else
					{
						EVP_CIPHER_CTX_free(ctx);
						throw std::runtime_error("Error: AES-128 GCM decryption failed or authentication failed.");
					}

					EVP_CIPHER_CTX_free(ctx);

					return true;
				}
			};
		} // namespace encryption

		namespace integrity
		{
			class MD5Handler
			{
			public:
				// Tính MD5 checksum cho một mảng byte
				static std::vector<uint8_t> calcCheckSum(const std::vector<uint8_t>& data)
				{
					std::vector<uint8_t> md5_result(EVP_MD_size(EVP_md5()));

					EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
					if (!md_ctx)
					{
						throw std::runtime_error("Error: EVP_MD_CTX_new failed.");
					}

					if (EVP_DigestInit_ex(md_ctx, EVP_md5(), NULL) != 1)
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: EVP_DigestInit_ex failed.");
					}

					if (data.size() > static_cast<size_t>(INT_MAX))
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: Data size too large for MD5_Update.");
					}
					int data_size = static_cast<int>(data.size());

					if (EVP_DigestUpdate(md_ctx, data.data(), data_size) != 1)
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: EVP_DigestUpdate failed.");
					}

					unsigned int md_len = 0;
					if (EVP_DigestFinal_ex(md_ctx, md5_result.data(), &md_len) != 1)
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: EVP_DigestFinal_ex failed.");
					}

					md5_result.resize(md_len);
					EVP_MD_CTX_free(md_ctx);
					return md5_result;
				}

				// Tính MD5 checksum cho một chunk file
				static std::vector<uint8_t> calcCheckSumChunk(const std::string& file_path, size_t chunk_size, size_t offset)
				{
					std::ifstream file(file_path, std::ios::binary);
					if (!file.is_open())
					{
						throw std::runtime_error("Error: Could not open file to calculate chunk MD5 checksum.");
					}

					if (file.seekg(static_cast<std::streamoff>(offset), std::ios::beg).fail())
					{
						throw std::runtime_error("Error: Failed to seek to the specified offset.");
					}

					EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
					if (!md_ctx)
					{
						throw std::runtime_error("Error: EVP_MD_CTX_new failed.");
					}

					if (EVP_DigestInit_ex(md_ctx, EVP_md5(), NULL) != 1)
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: EVP_DigestInit_ex failed.");
					}

					const size_t buffer_size = 4ULL * 1024 * 1024; // 4MB
					std::vector<uint8_t> buffer(buffer_size);

					size_t total_read = 0;
					while (total_read < chunk_size && file.good())
					{
						size_t to_read = std::min<size_t>(buffer_size, chunk_size - total_read);
						file.read(reinterpret_cast<char*>(buffer.data()), to_read);
						std::streamsize bytes_read = file.gcount();

						if (bytes_read > 0)
						{
							if (EVP_DigestUpdate(md_ctx, buffer.data(), static_cast<size_t>(bytes_read)) != 1)
							{
								EVP_MD_CTX_free(md_ctx);
								throw std::runtime_error("Error: EVP_DigestUpdate failed.");
							}
							total_read += static_cast<size_t>(bytes_read);
						}
						else
						{
							break;
						}
					}

					if (total_read == 0)
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: No data read from file at the specified offset.");
					}

					std::vector<uint8_t> md5_result(EVP_MD_size(EVP_md5()));
					unsigned int md_len = 0;
					if (EVP_DigestFinal_ex(md_ctx, md5_result.data(), &md_len) != 1)
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: EVP_DigestFinal_ex failed.");
					}

					md5_result.resize(md_len);
					EVP_MD_CTX_free(md_ctx);
					return md5_result;
				}

				// Tính MD5 checksum cho toàn bộ file
				static std::vector<uint8_t> calcCheckSumFile(const std::string& file_path)
				{
					std::ifstream file(file_path, std::ios::binary);
					if (!file.is_open())
					{
						throw std::runtime_error("Error: Could not open file to calculate MD5 checksum.");
					}

					EVP_MD_CTX* md_ctx = EVP_MD_CTX_new();
					if (!md_ctx)
					{
						throw std::runtime_error("Error: EVP_MD_CTX_new failed.");
					}

					if (EVP_DigestInit_ex(md_ctx, EVP_md5(), NULL) != 1)
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: EVP_DigestInit_ex failed.");
					}

					const size_t buffer_size = 1024ULL * 1024 * 4;
					std::vector<uint8_t> buffer(buffer_size);

					while (file.good())
					{
						file.read(reinterpret_cast<char*>(buffer.data()), buffer.size());

						std::streamsize bytes_read = file.gcount();

						if (bytes_read > 0)
						{
							if (EVP_DigestUpdate(md_ctx, buffer.data(), static_cast<int>(bytes_read)) != 1)
							{
								EVP_MD_CTX_free(md_ctx);
								throw std::runtime_error("Error: EVP_DigestUpdate failed.");
							}
						}
					}

					if (file.bad())
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: Failed to read file for MD5 checksum calculation.");
					}

					std::vector<uint8_t> md5_result(EVP_MD_size(EVP_md5()));
					unsigned int md_len = 0;

					if (EVP_DigestFinal_ex(md_ctx, md5_result.data(), &md_len) != 1)
					{
						EVP_MD_CTX_free(md_ctx);
						throw std::runtime_error("Error: EVP_DigestFinal_ex failed.");
					}

					md5_result.resize(md_len);
					EVP_MD_CTX_free(md_ctx);
					return md5_result;
				}
			};
		} // namespace integrity
	} // namespace datasecurity
} // namespace security

#endif // ENCRYPTION_HANDLER_H
