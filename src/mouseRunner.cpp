#include <string>
#include <thread>
#include <chrono>
#include "mouse.hpp"

int main(int argc, char** argv)
{
	if(argc < 3)
	{
		return 1;
	}

	int x = 0;
	int y = 0;
	try
	{
		x = std::stoi(argv[0]);
		y = std::stoi(argv[1]);
	} catch(...)
	{
		x = 0;
		y = 0;
	}

	Mouse mouse(0, 1920, 0, 1080);
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	mouse.moveAbsolute(x, y);
	mouse.moveRelative(x, y);
	mouse.leftClick();
	mouse.leftClick();
	mouse.leftClick();
	mouse.leftClick();
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	return 0;
}
