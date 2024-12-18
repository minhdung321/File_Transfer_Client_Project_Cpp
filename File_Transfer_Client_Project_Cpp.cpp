#include <file_transfer_client.h>
#include <cli.h>
#include <iostream>
#include <functional>
using namespace cli;

std::unique_ptr<FileTransferClient> ftClient;
cli::CLI cliClient{};

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
	default:
	{
		ftClient->CloseSession();
		cliClient.exitApplication();
		break;
	}
	}
}

static void login_page()
{
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

		ftClient->GetConnection().Connect("127.0.0.1", 27015);

		if (!ftClient->GetSessionManager().PerformHandshake())
		{
			throw std::runtime_error("Failed to connect to server. Please try again.");
		}

		login_page(); // Show login page
	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
	}

	return 0;
}
