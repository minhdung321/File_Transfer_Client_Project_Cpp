#ifndef CLI_H
#define CLI_H

#define UNICODE
#define _UNICODE

#include <iostream>
#include <string>
#include <iomanip>
#include <sstream>
#include <limits>
#include <functional>
#include <ShObjIdl.h>
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <file_transfer_client.h>
using namespace std;
namespace fs = std::filesystem;

#pragma comment(lib, "Comdlg32.lib")
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shell32.lib")

namespace cli
{
	enum class CLIState : int
	{
		MAIN_MENU,
		AUTHENTICATION,
		SESSION,
		UPLOAD,
		UPLOAD_DIR,
		DOWNLOAD,
		RESUME,
		CLOSE_SESSION,
		EXIT
	};

	class CLI
	{
		CLIState state;
	public:
		CLI() = default;
		~CLI() = default;

		void SetState(CLIState state)
		{
			this->state = state;
		}

		void waitForEnter()
		{
			cout << "> Press Enter to continue...";
			// cin.ignore((numeric_limits<streamsize>::max)(), '\n');
			cin.get();
		}

		bool confirmAction(const string& message)
		{
			cout << message << " (Y/N): ";
			char choice;
			cin >> choice;
			cin.ignore((numeric_limits<streamsize>::max)(), '\n');
			return (choice == 'Y' || choice == 'y');
		}

		void showWelcomeMessage()
		{
			system("cls"); // Clear screen

			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||          File Transfer Application        ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";
			cout << "Welcome to the File Transfer Application!\n";
			waitForEnter();

			state = CLIState::MAIN_MENU;
		}

		int showMainMenu()
		{
			if (state != CLIState::MAIN_MENU)
			{
				throw std::runtime_error("Invalid state for showing main menu");
			}

			system("cls"); // Clear screen

			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||                 MAIN MENU                 ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";
			cout << "1. Login\n";
			cout << "2. Exit\n";

			int choice = 0;
			cout << "Enter your choice: ";
			cin >> choice;
			cin.ignore((numeric_limits<streamsize>::max)(), '\n');

			if (choice < 1 || choice > 2)
			{
				cout << "Invalid choice. Please try again.\n";
				waitForEnter();

				return showMainMenu();
			}

			if (choice == 2)
			{
				state = CLIState::EXIT;
			}
			else
			{
				state = CLIState::AUTHENTICATION;
			}

			return choice;
		}

		void showAuthentication(function<bool(const string, const string)> authCallback)
		{
			if (state != CLIState::AUTHENTICATION)
			{
				throw std::runtime_error("Invalid state for showing authentication");
			}

			system("cls"); // Clear screen

			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||              AUTHENTICATION               ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";
			string username;
			cout << "> Enter your username: ";
			std::getline(cin, username);

			string password;
			cout << "> Enter your password: ";
			std::getline(cin, password);

			cout << "Authenticating...\n";

			if (!authCallback(username, password))
			{
				cout << "Authentication failed. Please try again.\n";
				waitForEnter();
				return showAuthentication(authCallback);
			}

			cout << "Authentication successful.\n";
			waitForEnter();

			state = CLIState::SESSION;
		}

		void exitApplication() const
		{
			if (state != CLIState::EXIT)
			{
				throw std::runtime_error("Invalid state for exiting application");
			}

			system("cls"); // Clear screen
			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||                 GOODBYE!                  ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";
			cout << "Thank you for using the File Transfer Application!\n";
			cout << "Exiting application...\n";

			exit(0);
		}

		int showTransferMenu()
		{
			if (state != CLIState::SESSION)
			{
				// throw std::runtime_error("Invalid state for showing transfer menu");
				return 5;
			}

			system("cls"); // Clear screen

			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||              FILE TRANSFER                ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";
			cout << "1. Upload file\n";
			cout << "2. Upload folder\n";
			cout << "3. Download file\n";
			cout << "4. Resume transfer\n";
			cout << "5. Logout\n";

			int choice = 0;
			cout << "Enter your choice: ";
			cin >> choice;
			cin.ignore((numeric_limits<streamsize>::max)(), '\n');

			if (choice < 1 || choice > 5)
			{
				cout << "Invalid choice. Please try again.\n";
				waitForEnter();

				return showTransferMenu();
			}

			return choice;
		}

		// {file_path, file_name, file_type, file_size }
		std::tuple<fs::path, wstring, wstring, size_t> OpenFileDialog()
		{
			// Initialize COM
			HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

			if (FAILED(hr))
			{
				throw std::runtime_error("Failed to initialize COM.");
			}

			// Create the File Open Dialog object
			IFileOpenDialog* pFileOpen{};

			hr = CoCreateInstance(
				CLSID_FileOpenDialog,
				NULL,
				CLSCTX_ALL,
				IID_IFileOpenDialog,
				reinterpret_cast<void**>(&pFileOpen));

			if (FAILED(hr))
			{
				CoUninitialize();
				throw std::runtime_error("Failed to create File Open Dialog object.");
			}

			// Show the File Open Dialog
			hr = pFileOpen->Show(NULL);

			if (FAILED(hr))
			{
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to show File Open Dialog.");
			}

			// Get the file name from the File Open Dialog
			IShellItem* pItem{};
			hr = pFileOpen->GetResult(&pItem);

			if (FAILED(hr))
			{
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to get the file name from File Open Dialog.");
			}

			PWSTR pszFilePath{};
			hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFilePath);

			if (FAILED(hr))
			{
				pItem->Release();
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to get the file path.");
			}

			// Get the file path as std::filesystem::path
			std::filesystem::path filePath{ pszFilePath };
			CoTaskMemFree(pszFilePath);

			// Get the file name
			PWSTR pszFileName{};
			hr = pItem->GetDisplayName(SIGDN_NORMALDISPLAY, &pszFileName);

			if (FAILED(hr))
			{
				pItem->Release();
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to get the file name.");
			}

			std::wstring fileName{ pszFileName };
			CoTaskMemFree(pszFileName);

			// Get the file type
			SHFILEINFOW sfi{};
			SHGetFileInfoW(fileName.c_str(), FILE_ATTRIBUTE_NORMAL, &sfi, sizeof(sfi), SHGFI_TYPENAME | SHGFI_USEFILEATTRIBUTES);

			std::wstring fileType{ sfi.szTypeName };

			// Get the file size
			WIN32_FILE_ATTRIBUTE_DATA fileAttr{};
			if (!GetFileAttributesExW(filePath.c_str(), GetFileExInfoStandard, &fileAttr))
			{
				pItem->Release();
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to get the file size.");
			}

			size_t fileSize = (static_cast<uint64_t>(fileAttr.nFileSizeHigh) << 32) | fileAttr.nFileSizeLow;

			// Release resources
			pItem->Release();
			pFileOpen->Release();
			CoUninitialize();

			return { filePath, fileName, fileType, fileSize };
		}

		void showUploadFile(FileTransferClient* client)
		{
			if (state != CLIState::UPLOAD)
			{
				throw std::runtime_error("Invalid state for showing upload file");
			}

			system("cls"); // Clear screen
			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||              UPLOAD FILE                  ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";

			fs::path filePath;
			wstring fileName, fileType;
			size_t fileSize;

			try
			{
				tie(filePath, fileName, fileType, fileSize) = OpenFileDialog();

				wcout << L"Selected file: " << fileName << L"\n";
				wcout << L"File path: " << filePath << L"\n";
				wcout << L"File type: " << fileType << L"\n";
				wcout << L"File size: " << fileSize << L" bytes (" << (fileSize / 1024) << L" KB)\n";


				if (!confirmAction("Do you want to upload this file?"))
				{
					state = CLIState::SESSION;

					showTransferMenu(); // Return to transfer menu

					return;
				}

				system("cls"); // Clear screen

				cout << "\n===============================================\n";
				cout << "> Uploading file...\n\n";

				// Upload file
				if (client->UploadFile(filePath, filePath.filename().string()))
				{
					cout << "File uploaded successfully.\n";
				}
				else
				{
					cerr << "Failed to upload file.\n";
				}
			}
			catch (const std::exception& e)
			{
				cerr << "Error: " << e.what() << endl;
			}

			waitForEnter();

			state = CLIState::SESSION;
		}


		void showDownloadFile(FileTransferClient* client)
		{
			if (state != CLIState::DOWNLOAD)
			{
				throw std::runtime_error("Invalid state for showing download file");
			}

			system("cls"); // Clear screen
			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||              DOWNLOAD FILE                ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";

			try
			{

				string filename;
				cout << "Enter file name you want to download : ";
				getline(cin, filename);

				if (!confirmAction("Do you want to download this file?"))
				{
					showTransferMenu(); // Return to transfer menu
				}

				// 
				if (client->DownloadFile(filename))
				{
					cout << endl << "File downloaded successfully." << endl;
				}
				else
				{
					cerr << "Failed to download file.\n";
				}
			}
			catch (const std::exception& e)
			{
				cerr << "Error: " << e.what() << endl;
			}

			waitForEnter();
			showTransferMenu(); // Return to transfer menu
		}
		// {folderPath, totalItems, totalSize}
		std::tuple<fs::path, size_t, size_t> OpenDirectoryDialog()
		{
			// Initialize COM
			HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

			if (FAILED(hr))
			{
				throw std::runtime_error("Failed to initialize COM.");
			}

			// Create the File Open Dialog object
			IFileOpenDialog* pFileOpen = nullptr;

			hr = CoCreateInstance(
				CLSID_FileOpenDialog,
				NULL,
				CLSCTX_ALL,
				IID_IFileOpenDialog,
				reinterpret_cast<void**>(&pFileOpen));

			if (FAILED(hr))
			{
				CoUninitialize();
				throw std::runtime_error("Failed to create File Open Dialog object.");
			}

			// Set options for the File Open Dialog
			DWORD dwOptions;
			hr = pFileOpen->GetOptions(&dwOptions);
			if (FAILED(hr))
			{
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to get dialog options.");
			}

			hr = pFileOpen->SetOptions(dwOptions | FOS_PICKFOLDERS);
			if (FAILED(hr))
			{
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to set dialog options.");
			}

			// Show the File Open Dialog
			hr = pFileOpen->Show(NULL);

			if (FAILED(hr))
			{
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to show File Open Dialog.");
			}

			// Get folder path from the File Open Dialog
			IShellItem* pItem = nullptr;
			hr = pFileOpen->GetResult(&pItem);
			if (FAILED(hr))
			{
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to get the folder from File Open Dialog.");
			}

			PWSTR pszFolderPath = nullptr;
			hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszFolderPath);
			if (FAILED(hr))
			{
				pItem->Release();
				pFileOpen->Release();
				CoUninitialize();
				throw std::runtime_error("Failed to get the folder path.");
			}

			fs::path folderPath{ pszFolderPath };
			CoTaskMemFree(pszFolderPath);

			// Free memory
			pItem->Release();
			pFileOpen->Release();
			CoUninitialize();

			// Calculate the total size of the folder, item counting
			size_t totalItems = 0;
			size_t totalSize = 0;

			try
			{
				for (const auto& entry : fs::recursive_directory_iterator(folderPath))
				{
					if (fs::is_regular_file(entry.status()))
					{
						totalItems++;
						totalSize += fs::file_size(entry.path());
					}
					else if (fs::is_directory(entry.status()))
					{
						totalItems++;
					}
				}
			}
			catch (const fs::filesystem_error& e)
			{
				throw std::runtime_error(std::string("Error accessing folder: ") + e.what());
			}

			return { folderPath, totalItems, totalSize };
		}

		void showUploadFolder(FileTransferClient* client)
		{
			if (state != CLIState::UPLOAD_DIR)
			{
				throw std::runtime_error("Invalid state for showing upload folder");
			}

			system("cls"); // Clear screen

			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||             UPLOAD FOLDER                 ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";

			fs::path folderPath;
			size_t totalItems = 0;
			size_t totalSize = 0;

			try
			{
				tie(folderPath, totalItems, totalSize) = OpenDirectoryDialog();

				wcout << L"Selected folder: " << folderPath.wstring() << L"\n";
				wcout << L"Total items: " << totalItems << L"\n";
				wcout << L"Total size: " << totalSize << L" bytes (" << (totalSize / 1024) << L" KB)\n";

				if (!confirmAction("Do you want to upload this folder?"))
				{
					showTransferMenu(); // Return to transfer menu
				}

				cout << "Which type of folder upload do you want to use?\n";
				cout << "1. Sequential upload\n";
				cout << "2. Parallel upload\n";

				int choice = 0;
				cout << "Enter your choice: ";
				cin >> choice;
				cin.ignore((numeric_limits<streamsize>::max)(), '\n');

				if (choice < 1 || choice > 2)
				{
					cout << "Invalid choice. Please try again.\n";
					waitForEnter();
					return showUploadFolder(client);
				}

				system("cls"); // Clear screen

				cout << "\n===============================================\n";
				cout << "> Uploading folder...\n\n";

				if (choice == 1)
				{
					// Upload folder
					if (client->UploadDirectory(folderPath, totalItems))
					{
						cout << "\nFolder uploaded successfully.\n";
					}
					else
					{
						cerr << "Failed to upload folder.\n";
					}
				}
				else if (choice == 2)
				{
					// Upload folder
					if (client->UploadDirectoryParallel(folderPath, totalItems))
					{
						cout << "\nFolder uploaded successfully.\n";
					}
					else
					{
						cerr << "Failed to upload folder.\n";
					}
				}
			}
			catch (const std::exception& e)
			{
				cerr << "Error: " << e.what() << endl;
			}

			waitForEnter();

			client->GetProgressBarManager().Cleanup();

			state = CLIState::SESSION;
		}

		void showResumeUpload(FileTransferClient* client)
		{
			if (!client || state != CLIState::RESUME)
			{
				throw std::runtime_error("Invalid client instance.");
			}

			system("cls");
			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||             RESUME UPLOAD FILE            ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";
			cout << "> Select a file to resume upload...\n\n";

			fs::path filePath;
			wstring fileName, fileType;
			size_t fileSize;

			try {
				tie(filePath, fileName, fileType, fileSize) = OpenFileDialog();

				wcout << L"Selected file: " << fileName << L"\n";
				wcout << L"File path: " << filePath << L"\n";
				wcout << L"File type: " << fileType << L"\n";
				wcout << L"File size: " << fileSize << L" bytes (" << (fileSize / 1024) << L" KB)\n";

				if (!confirmAction("Do you want to resume upload this file?"))
				{
					return;
				}

				system("cls");
				cout << "\n===============================================\n";
				cout << "> Resuming upload file...\n\n";

				if (client->ResumeUpload(filePath.string()))
				{
					cout << "File uploaded successfully.\n";
				}
				else
				{
					cerr << "Failed to upload file.\n";
				}
			}
			catch (const std::exception& e) {
				cerr << "Error: " << e.what() << endl;
			}

			waitForEnter();

			state = CLIState::SESSION;
		}

		void showResumeDownload(FileTransferClient* client)
		{
			if (!client || state != CLIState::RESUME)
			{
				throw std::runtime_error("Invalid client instance.");
			}

			system("cls");
			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||           RESUME DONWLOAD FILE            ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";
			cout << "> Select a file to resume download...\n\n";

			std::string path = "./checkpoint/";
			std::vector<fs::path> files;

			try {
				if (fs::exists(path) && fs::is_directory(path)) {
					int i = 0;
					std::cout << "list of files was downloading:\n";

					for (const auto& entry : fs::directory_iterator(path)) {
						if (fs::is_regular_file(entry.path())) {
							files.push_back(entry.path());

							std::string file_name;
							uint32_t file_id_read;
							uint64_t resume_position_read;
							uint32_t last_chunk_index_read;
							uint64_t file_size;
							size_t len;

							/*fs::path path(file_name);
							std::string namePart = path.stem().string();
							std::string check_point_path = utils::DEFAULT_CHECK_POINT_PATH + namePart + ".ckp";*/

							std::ifstream resume_in(entry.path(), std::ios::binary);
							// Repair info resuming transfer
							if (resume_in.is_open())
							{
								resume_in.read(reinterpret_cast<char*>(&len), sizeof(len));
								resume_in.read(reinterpret_cast<char*>(file_name.data()), sizeof(char) * len);
								resume_in.read(reinterpret_cast<char*>(&file_id_read), sizeof(file_id_read));
								resume_in.read(reinterpret_cast<char*>(&resume_position_read), sizeof(resume_position_read));
								resume_in.read(reinterpret_cast<char*>(&last_chunk_index_read), sizeof(last_chunk_index_read));
								resume_in.read(reinterpret_cast<char*>(&file_size), sizeof(file_size));
							}
							resume_in.close();

							std::cout << ++i << ". " << file_name.data() << '\n';
						}
					}

					if (files.empty()) {
						std::cout << "List is empty.\n";
						return;
					}

					int choice;
					std::cout << "Enter the file number you want to resume download: ";
					std::cin >> choice;
					cin.ignore();

					if (choice >= 0 && choice <= static_cast<int>(files.size()))
					{
						if (!confirmAction("Do you want to resume download this file?"))
						{
							return;
						}

						std::string resume_file_name;
						uint32_t file_id_read;
						uint64_t resume_position_read;
						uint32_t last_chunk_index_read;
						uint64_t file_size;
						size_t len;

						std::ifstream resume_in("./checkpoint/" + files[choice - 1].filename().string(), std::ios::binary);
						// Get the file name
						if (resume_in.is_open())
						{
							resume_in.read(reinterpret_cast<char*>(&len), sizeof(len));
							resume_in.read(reinterpret_cast<char*>(resume_file_name.data()), sizeof(char) * len);
							resume_in.read(reinterpret_cast<char*>(&file_id_read), sizeof(file_id_read));
							resume_in.read(reinterpret_cast<char*>(&resume_position_read), sizeof(resume_position_read));
							resume_in.read(reinterpret_cast<char*>(&last_chunk_index_read), sizeof(last_chunk_index_read));
							resume_in.read(reinterpret_cast<char*>(&file_size), sizeof(file_size));
						}
						resume_in.close();

						if (client->ResumeDownload(resume_file_name))
						{
							cout << "File downloaded successfully.\n";
						}
						else
						{
							cerr << "Failed to download file.\n";
						}
					}
					else
					{
						std::cerr << "Invalid option.\n";
					}
				}
				else 
				{
					std::cerr << "Invalid directory.\n";
				}
			}
			catch (const fs::filesystem_error& e) {
				std::cerr << "Lỗi: " << e.what() << '\n';
			}

			

			waitForEnter();

			state = CLIState::SESSION;
		}

		void showResume(FileTransferClient* client) 
		{
			if (!client || state != CLIState::RESUME)
			{
				throw std::runtime_error("Invalid client instance.");
			}

			system("cls");

			cout << "===============================================\n";
			cout << "||                                           ||\n";
			cout << "||             RESUME TRANSFER               ||\n";
			cout << "||                                           ||\n";
			cout << "===============================================\n";

			cout << "1. Resume upload file\n";
			cout << "2. Resume download file\n";
			cout << "3. Back\n";

			int choice = 0;

			cout << "Enter your choice: ";
			cin >> choice;
			cin.ignore((numeric_limits<streamsize>::max)(), '\n');

			if (choice < 1 || choice > 3)
			{
				cout << "Invalid choice. Please try again.\n";
				waitForEnter();

				return showResume(client);
			}

			switch (choice)
			{
			case 1:
				state = CLIState::RESUME;
				showResumeUpload(client);
				break;
			case 2:
				state = CLIState::RESUME;
				showResumeDownload(client); 
				state = CLIState::SESSION;
				break;
			case 3:
				state = CLIState::SESSION;
				break;
			}

			return;
		}
	}; // class CLI
} // namespace cli

#endif // !CLI_H