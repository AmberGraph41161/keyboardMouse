#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <sys/ioctl.h>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main()
{
	std::vector<std::filesystem::path> devicesPaths;
	for
	(
		std::filesystem::directory_iterator it("/dev/input/");
		it != std::filesystem::directory_iterator{};
		++it
	)
	{
		std::filesystem::directory_entry entry = *(it);
		devicesPaths.push_back(entry.path());
	}

	std::cout << "select device:" << std::endl;
	for(int x = 0; x < devicesPaths.size(); ++x)
	{
		int fileDescriptor = open(devicesPaths[x].c_str(), O_RDONLY);
		if(fileDescriptor == -1)
		{
			++x;
			continue;
		}
		char deviceName[256];
		if(ioctl(fileDescriptor, EVIOCGNAME(sizeof(deviceName)), deviceName) == -1)
		{
			++x;
			continue;
		}

		std::cout << x << ") " << deviceName << ' ' << devicesPaths[x] << std::endl;
	}
	std::cout << "> ";

	int chosenDeviceIndex;
	try
	{
		std::string getlinestring;
		std::getline(std::cin, getlinestring);
		chosenDeviceIndex = std::stoi(getlinestring);
	} catch(...)
	{
		chosenDeviceIndex = 0;
	}

	int keyboardFileDescriptor = open(devicesPaths[chosenDeviceIndex].c_str(), O_RDONLY);
	if(keyboardFileDescriptor == -1)
	{
		perror("Something went wrong while opening device...");
		exit(EXIT_FAILURE);
	}

	input_event inputEvent;
	while(true)
	{
		ssize_t readSize = read(keyboardFileDescriptor, &inputEvent, sizeof(inputEvent));
		if(readSize == (ssize_t)(-1))
		{
			perror("Error reading input device inputEvent!");
			break;
		}

		if(readSize == (ssize_t)(0))
		{
			std::cout << "nothing read. EOF maybe?" << std::endl;
			break;
		}

		if(inputEvent.type == EV_KEY)
		{
			if(inputEvent.value == 1)
			{
				std::cout << inputEvent.code << " pressed!" << std::endl;
				if(inputEvent.code == KEY_A)
				{
					break;
				}
			} else
			{
				std::cout << inputEvent.code << " released!" << std::endl;
			}
		}
	}

	close(keyboardFileDescriptor);
	return 0;
}
