#ifndef MOUSE_HPP
#define MOUSE_HPP

#include "virtualInputDevice.hpp"

#include <linux/uinput.h>

class Mouse : public VirtualInputDevice
{
public:
	Mouse();
	Mouse
	(
		unsigned int absoluteMouseXMinimum,
		unsigned int absoluteMouseXMaximum,
		unsigned int absoluteMouseYMinimum,
		unsigned int absoluteMouseYMaximum
	);

	void leftClick();
	void leftDown();
	void leftUp();

	void rightClick();
	void rightDown();
	void rightUp();

	void moveRelative(int deltaX, int deltaY);
	void moveAbsolute(int absoluteX, int absoluteY);

private:
	static int minimumSleepMillisecondsBetweenClicks;
};

#endif //MOUSE_HPP
