#ifndef PROGRESSBAR_MANAGER_H
#define PROGRESSBAR_MANAGER_H

#include <iostream>
#include <iomanip>
#include <string>
#include <unordered_map>
#include <deque>
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
	std::deque<std::string> m_file_queue;
	HANDLE hConsole;
	CONSOLE_SCREEN_BUFFER_INFO csbi;

	COORD m_start_pos;
	bool m_initialized = false;

	static constexpr auto DEFAULT_MAX_FILE_NAME_LEN = 30;
	static constexpr int MAX_DISPLAY_BARS = 10; // Số lượng thanh tiến trình tối đa hiển thị

	// Biến để quản lý thanh tiến trình tổng thể
	std::string m_total_progress_name = "Total Progress";
	float m_total_progress = 0.0f;
	bool m_show_total_progress = false;

public:
	ProgressBarManager()
	{
		// Lấy handle của console và thông tin về buffer
		hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
		GetConsoleScreenBufferInfo(hConsole, &csbi);
	}

	void AddFile(const std::string& file_name)
	{
		std::string truncated_name = TruncateFileName(file_name);
		m_files_progress[file_name] = { truncated_name, 0.0f };
		m_file_queue.push_back(file_name);

		// Nếu số lượng thanh tiến trình vượt quá giới hạn, loại bỏ thanh cũ nhất
		if (m_file_queue.size() > MAX_DISPLAY_BARS)
		{
			std::string oldest_file = m_file_queue.front();
			m_file_queue.pop_front();
			m_files_progress.erase(oldest_file);
		}

		// Nếu chưa khởi tạo vị trí bắt đầu, lưu lại vị trí hiện tại
		if (!m_initialized)
		{
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

		// Cập nhật tiến trình tổng thể
		if (m_show_total_progress)
		{
			calculateTotalProgress();
			redrawProgressBars();
		}
	}

	void ShowTotalProgress(bool show)
	{
		m_show_total_progress = show;
		if (show)
		{
			// Đảm bảo thanh tiến trình tổng thể nằm ở vị trí đầu tiên
			if (!m_initialized)
			{
				CONSOLE_SCREEN_BUFFER_INFO csbi_current;
				GetConsoleScreenBufferInfo(hConsole, &csbi_current);
				m_start_pos = csbi_current.dwCursorPosition;
				m_initialized = true;
			}
		}
		redrawProgressBars();
	}

	void Cleanup()
	{
		// Xóa vùng thanh tiến trình
		if (m_initialized)
		{
			int total_bars = static_cast<int>(m_file_queue.size());
			if (m_show_total_progress)
			{
				total_bars += 1; // Thêm 1 cho thanh tiến trình tổng thể
			}

			// Di chuyển con trỏ đến vị trí bắt đầu
			SetConsoleCursorPosition(hConsole, m_start_pos);

			// Xóa từng dòng
			for (int i = 0; i < total_bars; ++i)
			{
				std::cout << std::string(csbi.dwSize.X, ' ') << "\n";
			}

			// Đặt lại vị trí con trỏ
			SetConsoleCursorPosition(hConsole, m_start_pos);
		}

		// Xóa dữ liệu
		m_files_progress.clear();
		m_file_queue.clear();
		m_initialized = false;
		m_total_progress = 0.0f;
		m_show_total_progress = false;
	}

private:
	void redrawProgressBars()
	{
		if (!m_initialized)
			return;

		// Lưu vị trí con trỏ hiện tại
		CONSOLE_SCREEN_BUFFER_INFO csbi_current;
		GetConsoleScreenBufferInfo(hConsole, &csbi_current);

		// Di chuyển con trỏ đến vị trí bắt đầu
		SetConsoleCursorPosition(hConsole, m_start_pos);

		// Vẽ thanh tiến trình tổng thể nếu có
		if (m_show_total_progress)
		{
			drawProgressBar(m_total_progress_name, m_total_progress);
		}

		// Vẽ các thanh tiến trình tệp tin
		for (const auto& file_name : m_file_queue)
		{
			const FileProgress& fp = m_files_progress[file_name];
			drawProgressBar(fp.file_name, fp.progress);
		}

		// Xóa các dòng thừa nếu có
		int lines_to_clear = csbi_current.dwCursorPosition.Y - (m_start_pos.Y + static_cast<SHORT>(m_file_queue.size()) + (m_show_total_progress ? 1 : 0));
		for (int i = 0; i < lines_to_clear; ++i)
		{
			std::cout << std::string(csbi.dwSize.X, ' ') << "\n";
		}

		// Đưa con trỏ trở lại vị trí sau cùng của thanh tiến trình
		COORD end_pos = m_start_pos;
		end_pos.Y += static_cast<SHORT>(m_file_queue.size()) + (m_show_total_progress ? 1 : 0);
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
				std::cout << (char)219; // Full block character
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
			// Không tìm thấy hoặc tên tệp tin bắt đầu bằng '.'
			return file_name.substr(0, DEFAULT_MAX_FILE_NAME_LEN - 3) + "...";
		}

		std::string name = file_name.substr(0, dot_pos);
		std::string extension = file_name.substr(dot_pos);

		size_t total_length = DEFAULT_MAX_FILE_NAME_LEN - extension.length() - 3; // 3 ký tự cho "..."

		name = name.substr(0, total_length);

		return name + "..." + extension;
	}

	void calculateTotalProgress()
	{
		if (m_files_progress.empty())
		{
			m_total_progress = 100.0f;
			return;
		}

		float sum_progress = 0.0f;
		for (const auto& entry : m_files_progress)
		{
			sum_progress += entry.second.progress;
		}

		m_total_progress = sum_progress / m_files_progress.size();
	}
};

#endif // !PROGRESSBAR_MANAGER_H