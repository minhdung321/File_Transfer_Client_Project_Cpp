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

	static constexpr auto DEFAULT_MAX_FILE_NAME_LEN = 20;

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
	};

	void UpdateProgress(const std::string& file_name, float progress)
	{
		if (m_files_progress.find(file_name) != m_files_progress.end())
		{
			m_files_progress[file_name].progress = progress;
			redrawProgressBars();
		}
	}

private:
	void redrawProgressBars()
	{
		// Save the current cursor position
		CONSOLE_SCREEN_BUFFER_INFO csbi_current;
		GetConsoleScreenBufferInfo(hConsole, &csbi_current);

		// Move cursor to the start of progress bars
		COORD start_pos = csbi.dwCursorPosition;
		start_pos.Y -= static_cast<SHORT>(m_file_list.size());
		SetConsoleCursorPosition(hConsole, start_pos);

		// Redraw each progress bar
		for (const auto& file_name : m_file_list) {
			const FileProgress& fp = m_files_progress[file_name];
			drawProgressBar(fp.file_name, fp.progress);
		}

		// Restore cursor position
		SetConsoleCursorPosition(hConsole, csbi_current.dwCursorPosition);
	}

	void drawProgressBar(const std::string& file_name, float progress, int barWidth = 30)
	{
		std::cout << "\r";

		// Prepare the file name column (max 20 characters)
		std::string display_name = file_name;
		display_name.resize(20, ' ');

		std::cout << display_name << " [";

		int pos = static_cast<int>(barWidth * progress / 100.0f);
		for (int i = 0; i < barWidth; ++i) {
			if (i < pos)
				std::cout << (char)254; // Square character
			else if (i == pos)
				std::cout << ">";
			else
				std::cout << " ";
		}

		std::cout << "] " << std::fixed << std::setprecision(2) << progress << "%   \n";
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