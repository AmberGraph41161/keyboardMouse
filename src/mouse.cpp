#include "mouse.hpp"

#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

#include <thread>
#include <chrono>

Mouse::Mouse()
{
	fileDescriptor = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	ioctl(fileDescriptor, UI_SET_EVBIT, EV_KEY);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_MIDDLE);

	ioctl(fileDescriptor, UI_SET_EVBIT, EV_REL);
	ioctl(fileDescriptor, UI_SET_RELBIT, REL_X);
	ioctl(fileDescriptor, UI_SET_RELBIT, REL_Y);

	memset(&uinputSetup, 0, sizeof(uinputSetup));
	uinputSetup.id.bustype = BUS_USB;
	uinputSetup.id.vendor = 0x1234;
	uinputSetup.id.product = productID++;
	strcpy(uinputSetup.name, "virtualInputDevice Mouse");

	ioctl(fileDescriptor, UI_DEV_SETUP, &uinputSetup);
	ioctl(fileDescriptor, UI_DEV_CREATE);
};

void Mouse::leftClick()
{
	emit(fileDescriptor, EV_KEY, BTN_LEFT, 1);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
	emit(fileDescriptor, EV_KEY, BTN_LEFT, 0);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}
void Mouse::leftDown()
{
	emit(fileDescriptor, EV_KEY, BTN_LEFT, 1);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}
void Mouse::leftUp()
{
	emit(fileDescriptor, EV_KEY, BTN_LEFT, 0);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}

void Mouse::rightClick()
{
	emit(fileDescriptor, EV_KEY, BTN_RIGHT, 1);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
	emit(fileDescriptor, EV_KEY, BTN_RIGHT, 0);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}
void Mouse::rightDown()
{
	emit(fileDescriptor, EV_KEY, BTN_RIGHT, 1);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}
void Mouse::rightUp()
{
	emit(fileDescriptor, EV_KEY, BTN_RIGHT, 0);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}

void Mouse::middleClick()
{
	emit(fileDescriptor, EV_KEY, BTN_MIDDLE, 1);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
	emit(fileDescriptor, EV_KEY, BTN_MIDDLE, 0);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}
void Mouse::middleDown()
{
	emit(fileDescriptor, EV_KEY, BTN_MIDDLE, 1);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}
void Mouse::middleUp()
{
	emit(fileDescriptor, EV_KEY, BTN_MIDDLE, 0);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
	std::this_thread::sleep_for(std::chrono::milliseconds(recommendedMillisecondsSleepBetweenActions));
}

void Mouse::moveRelative(int deltaX, int deltaY)
{
	emit(fileDescriptor, EV_REL, REL_X, deltaX);
	emit(fileDescriptor, EV_REL, REL_Y, deltaY);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
}
