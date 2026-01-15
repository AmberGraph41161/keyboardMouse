#ifndef ABSOLUTEPOINTER_HPP
#define ABSOLUTEPOINTER_HPP

#include "virtualInputDevice.hpp"

#include <linux/uinput.h>

class AbsolutePointer : public VirtualInputDevice
{
public:
	AbsolutePointer
	(
		unsigned int absoluteMouseXMinimum,
		unsigned int absoluteMouseXMaximum,
		unsigned int absoluteMouseYMinimum,
		unsigned int absoluteMouseYMaximum
	);

	void moveAbsolute(int absoluteX, int absoluteY);
};

#endif //ABSOLUTEPOINTER_HPP
