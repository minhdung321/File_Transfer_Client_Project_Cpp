#ifndef PROGRESSBAR_MANAGER_H
#define PROGRESSBAR_MANAGER_H

#include <iostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <vector>
#include <Windows.h>

struct FileProgress
{
	std::string file_name;
	float progress;
};

class ProgressBarManager
{
private:
	std::unordered_map<std::string, FileProgress> m_files_progress;
	std::vector<std::string> m_file_list;
	HANDLE hConsole;
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	COORD m_start_pos;
	bool m_initialized = false;
	int m_last_num_files = 0;

	static constexpr auto DEFAULT_MAX_FILE_NAME_LEN = 30;

public:
	ProgressBarManager()
	{
		hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		GetConsoleScreenBufferInfo(hConsole, &csbi);
	}

	void AddFile(const std::string& file_name)
	{
		std::basic_string truncated_name = TruncateFileName(file_name);
		m_files_progress[file_name] = { truncated_name, 0.0f };
		m_file_list.push_back(file_name);

		// Nếu chưa khởi tạo thì lưu lại vị trí bắt đầu
		if (!m_initialized)
		{
			// Lưu lại vị trí bắt đầu cho vùng thanh tiến trình
			CONSOLE_SCREEN_BUFFER_INFO csbi_current;
			GetConsoleScreenBufferInfo(hConsole, &csbi_current);
			m_start_pos = csbi_current.dwCursorPosition;
			m_initialized = true;
		}

		redrawProgressBars();
	};

	void UpdateProgress(const std::string& file_name, float progress)
	{
		if (m_files_progress.find(file_name) != m_files_progress.end())
		{
			m_files_progress[file_name].progress = progress;
			redrawProgressBars();
		}

		if (progress >= 100.0f)
		{
			m_files_progress.erase(file_name);
			m_file_list.erase(std::remove(m_file_list.begin(), m_file_list.end(), file_name), m_file_list.end());
			redrawProgressBars();
		}
	}

	void Cleanup()
	{
		// Xóa vùng thanh tiến trình
		COORD end_pos = m_start_pos;
		end_pos.Y += static_cast<SHORT>(m_file_list.size());
		SetConsoleCursorPosition(hConsole, end_pos);
		std::cout << std::string(csbi.dwSize.X, ' ');

		m_files_progress.clear();
	}

private:
	void redrawProgressBars()
	{
		// Lưu vị trí con trỏ hiện tại
		CONSOLE_SCREEN_BUFFER_INFO csbi_current;
		GetConsoleScreenBufferInfo(hConsole, &csbi_current);

		// Di chuyển con trỏ đến vị trí bắt đầu của thanh tiến trình
		SetConsoleCursorPosition(hConsole, m_start_pos);

		// Vẽ lại các thanh tiến trình
		for (const auto& file_name : m_file_list) {
			const FileProgress& fp = m_files_progress[file_name];
			drawProgressBar(fp.file_name, fp.progress);
		}

		// Di chuyển con trỏ trở lại vị trí sau vùng thanh tiến trình
		COORD end_pos = m_start_pos;
		end_pos.Y += static_cast<SHORT>(m_file_list.size());
		SetConsoleCursorPosition(hConsole, end_pos);
	}

	void drawProgressBar(const std::string& file_name, float progress, int barWidth = 30)
	{
		// Di chuyển con trỏ đến đầu dòng
		std::cout << "\r";

		// Xóa nội dung dòng hiện tại
		std::cout << std::string(csbi.dwSize.X, ' ');

		// Di chuyển con trỏ về đầu dòng lần nữa
		std::cout << "\r";

		// Chuẩn bị tên tệp tin (tối đa 30 ký tự)
		std::string display_name = file_name;
		if (display_name.length() > DEFAULT_MAX_FILE_NAME_LEN)
		{
			display_name = display_name.substr(0, DEFAULT_MAX_FILE_NAME_LEN - 3) + "...";
		}
		else
		{
			display_name.resize(DEFAULT_MAX_FILE_NAME_LEN, ' ');
		}

		std::cout << display_name << " [";

		int pos = static_cast<int>(barWidth * progress / 100.0f);
		for (int i = 0; i < barWidth; ++i) {
			if (i < pos)
				std::cout << (char)254; // Full block character
			else
				std::cout << " ";
		}

		std::cout << "] " << std::fixed << std::setprecision(2) << progress << "%";

		// Đảm bảo con trỏ ở dòng tiếp theo
		std::cout << std::endl;
	}

	std::string TruncateFileName(const std::string& file_name)
	{
		if (file_name.length() <= DEFAULT_MAX_FILE_NAME_LEN) {
			return file_name;
		}

		size_t dot_pos = file_name.find_last_of('.');
		if (dot_pos == std::string::npos || dot_pos == 0) {
			// No extension found or filename starts with '.'
			return file_name.substr(0, DEFAULT_MAX_FILE_NAME_LEN);
		}

		std::string name = file_name.substr(0, dot_pos);
		std::string extension = file_name.substr(dot_pos);

		size_t total_length = extension.length() >= DEFAULT_MAX_FILE_NAME_LEN ? 0 : DEFAULT_MAX_FILE_NAME_LEN - extension.length();
		name = name.substr(0, total_length);

		return name + extension;
	}
};

#endif // !PROGRESSBAR_MANAGER_H