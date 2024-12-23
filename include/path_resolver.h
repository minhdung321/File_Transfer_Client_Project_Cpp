#ifndef PATH_RESOLVER_H
#define PATH_RESOLVER_H

#include "system_utils/log_reporter.h"

#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
namespace fs = std::filesystem;

namespace utils
{
	constexpr auto DEFAULT_STORAGE_PATH = "./storage/";
	constexpr auto DEFAULT_CHECK_POINT_PATH = "./checkpoint/";

	class PathResolver
	{
	private:
		LogReporter& reporter = LogReporter::GetInstance();

	public:
		~PathResolver();

		bool CreateUserDirectory(const std::string& username);
		bool CreateCheckPointDirectory();

		void SplitPath(const std::string& fullPath, std::string& dirPath, std::string& fileName);

		bool CheckDirPathExist(const std::string& dirPath);
		bool CheckFileNameExist(const std::string& filePath);

		bool CreateSubdirectory(const std::string& dirPath);
		bool CreateFile(const std::string& fullPath);

		bool DeleteFile(const std::string& filename);
		bool DeleteDirectory(const std::string& dirPath);

		std::string GenerateNewFileName(const std::string& fileName);
	};
}

#endif // !PATH_RESOLVER_H

