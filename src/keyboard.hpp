#ifndef KEYBOARD_HPP
#define KEYBOARD_HPP

#include "virtualInputDevice.hpp"

class Keyboard : public VirtualInputDevice
{
public:
	Keyboard();

	void sendKey(int uinputKeycode);
};

#endif //KEYBOARD_HPP
