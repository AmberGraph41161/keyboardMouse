#ifndef ABSOLUTEPOINTER_HPP
#define ABSOLUTEPOINTER_HPP

#include "virtualInputDevice.hpp"

#include <linux/uinput.h>

class AbsolutePointer : public VirtualInputDevice
{
public:
	AbsolutePointer
	(
		unsigned int absolutePointerXMinimum,
		unsigned int absolutePointerXMaximum,
		unsigned int absolutePointerYMinimum,
		unsigned int absolutePointerYMaximum
	);

	void moveAbsolute(int absoluteX, int absoluteY);
};

#endif //ABSOLUTEPOINTER_HPP
