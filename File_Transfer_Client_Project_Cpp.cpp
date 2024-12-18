#include <file_transfer_client.h>
#include <iostream>

int main()
{
	try
	{
		FileTransferClient ftClient;

		ftClient.GetConnection().Connect("127.0.0.1", 27015);

		if (!ftClient.GetSessionManager().PerformHandshake())
		{
			std::cerr << "Handshake failed." << std::endl;
			return -1;
		}



	}
	catch (const std::exception& e)
	{
		std::cerr << "Error: " << e.what() << std::endl;
	}
	return 0;
}
