#include "system_utils/path_resolver.h"
#include "path_resolver.h"


utils::PathResolver::~PathResolver()
{
}

bool utils::PathResolver::CreateUserDirectory(const std::string& username)
{
	// Tạo thư mục người dùng 
	std::string userDirPath = DEFAULT_STORAGE_PATH + username;

	if (CreateSubdirectory(userDirPath))
	{
		return true;
	}
	else
	{
		reporter.LogMessage("[Path Resolver] Can not create user directory: " + userDirPath);
		return false;
	}
}

bool utils::PathResolver::CreateCheckPointDirectory()
{
	std::string checkPointDirPath = DEFAULT_CHECK_POINT_PATH;

	if (CreateSubdirectory(checkPointDirPath))
	{
		return true;
	}
	else
	{
		reporter.LogMessage("[Path Resolver] Can not create user directory: " + checkPointDirPath);
		return false;
	}
}

void utils::PathResolver::SplitPath(const std::string& fullPath, std::string& dirPath, std::string& fileName)
{
	fs::path path(fullPath);
	dirPath = path.parent_path().string();
	fileName = path.filename().string();
}

bool utils::PathResolver::CheckDirPathExist(const std::string& dirPath)
{
	return fs::exists(dirPath) && fs::is_directory(dirPath);
}

bool utils::PathResolver::CheckFileNameExist(const std::string& filePath)
{
	return fs::exists(filePath) && fs::is_regular_file(filePath);
}

bool utils::PathResolver::CreateSubdirectory(const std::string& dirPath)
{
	try
	{
		if (std::filesystem::create_directories(dirPath))
		{
			reporter.LogMessage("[Path Resolver] Create subdirectory successfully: " + dirPath);
			return true;
		}
		else
		{
			reporter.LogMessage("[Path Resolver] Subdirectory already exists: " + dirPath);
			return false;
		}
	}
	catch (const std::filesystem::filesystem_error& e)
	{
		reporter.LogMessage("[Path Resolver] Can not create subdirectory: " + dirPath + ". Error: " + e.what());
		return false;
	}
}

bool utils::PathResolver::CreateFile(const std::string& fullPath)
{
	std::string dirPath, fileName;
	SplitPath(fullPath, dirPath, fileName);

	if (!CheckDirPathExist(dirPath))
	{
		CreateSubdirectory(dirPath);
	}

	try
	{
		std::ofstream file(fullPath);

		if (file.is_open())
		{
			reporter.LogMessage("[Path Resolver] Create file successfully : " + fullPath);
			file.close();
			return true;
		}
		else
		{
			reporter.LogMessage("[Path Resolver] Can not create file : " + fullPath);
			return false;
		}
	}
	catch (const std::exception& e)
	{
		reporter.LogMessage("[Path Resolver] Can not create file : " + fullPath + ". Error: " + e.what());
		return false;
	}
}

bool utils::PathResolver::DeleteFile(const std::string& filename)
{
	return false;
}

bool utils::PathResolver::DeleteDirectory(const std::string& dirPath)
{
	try {
		return fs::remove_all(dirPath) > 0;
	}
	catch (const fs::filesystem_error& e) {
		return false;
	}
}

std::string utils::PathResolver::GenerateNewFileName(const std::string& fileName)
{
	fs::path path(fileName);
	std::string namePart = path.stem().string();
	std::string extension = path.extension().string();

	std::string newFileName = fileName;

	if (CheckFileNameExist(fileName))
	{
		// Lấy timestamp hiện tại
		auto now = std::chrono::system_clock::now();
		auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
		std::stringstream timestamp;
		timestamp << "_" << now_ms;

		// Thêm timestamp vào tên tệp
		newFileName = namePart + timestamp.str() + extension;
	}

	return newFileName;
}


