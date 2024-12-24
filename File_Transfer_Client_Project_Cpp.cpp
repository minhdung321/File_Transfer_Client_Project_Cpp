#include <file_transfer_client.h>
#include <cli.h>
#include <iostream>
#include <functional>
#include <progressbar_manager.h>
using namespace cli;

std::unique_ptr<FileTransferClient> ftClient;
cli::CLI cliClient{};

static void login_page(); // Forward declaration

static void transfer_page()
{
	int opt = cliClient.showTransferMenu();

	switch (opt)
	{
	case 1: // Upload file
	{
		cliClient.SetState(CLIState::UPLOAD);
		cliClient.showUploadFile(ftClient.get());
		break;
	}
	case 2: // Upload folder
	{
		cliClient.SetState(CLIState::UPLOAD_DIR);
		cliClient.showUploadFolder(ftClient.get());
		break;
	}
	case 4:
	{
		// Resume transfer
		cliClient.SetState(CLIState::RESUME);
		cliClient.showResume(ftClient.get());
		break;
	}
	case 5:
	{
		ftClient->CloseSession();
		cliClient.SetState(CLIState::MAIN_MENU);
		login_page(); // Move to login page
		break;
	}
	default:
	{
		ftClient->CloseSession();
		ftClient->GetSessionManager().ResetSession();
		cliClient.SetState(CLIState::EXIT);
		cliClient.exitApplication();
		break;
	}
	}

	ftClient->GetProgressBarManager().Cleanup();

	transfer_page(); // Continue to transfer page

	return;
}

static void login_page()
{
	ftClient->GetConnection().Disconnect();

	ftClient->GetSessionManager().ResetSession();

	ftClient->GetConnection().Connect("127.0.0.1", 27015);

	if (!ftClient->GetSessionManager().PerformHandshake())
	{
		throw std::runtime_error("Failed to connect to server. Please restart the application.");
	}

	cliClient.showWelcomeMessage();

	int opt = cliClient.showMainMenu();

	if (opt == 1)
	{
		auto authCallback = [](const std::string& username, const std::string& password) -> bool
			{
				return ftClient->GetSessionManager().PerformAuthentication(username, password);
			};

		cliClient.showAuthentication(authCallback);

		transfer_page(); // Move to transfer page
	}
	else // Exit
	{
		ftClient->CloseSession();

		cliClient.exitApplication();
	}
}

int main()
{
	try
	{
		ftClient = std::make_unique<FileTransferClient>();

		login_page(); // Show login page
	}
	catch (const std::exception& e)
	{
		std::cerr << "[X] Error: " << e.what() << std::endl;
	}

	return 0;
}
