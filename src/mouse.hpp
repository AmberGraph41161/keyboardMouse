#ifndef MOUSE_HPP
#define MOUSE_HPP

#include "virtualInputDevice.hpp"

#include <linux/uinput.h>

class Mouse : public VirtualInputDevice
{
public:
	Mouse();

	void leftClick();
	void leftDown();
	void leftUp();

	void rightClick();
	void rightDown();
	void rightUp();

	void middleClick();
	void middleDown();
	void middleUp();

	void moveRelative(int deltaX, int deltaY);
};

#endif //MOUSE_HPP
