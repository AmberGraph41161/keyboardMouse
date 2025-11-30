#include <iostream>
#include <filesystem>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>

int main()
{
	std::vector<std::filesystem::path> devicesPaths;
	for
	(
		std::filesystem::directory_iterator it("/dev/input/by-id");
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
		std::cout << x << ' ' << devicesPaths[x] << std::endl;
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

	std::cout << chosenDeviceIndex << std::endl;

	return 0;
}
