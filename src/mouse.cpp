#include "mouse.hpp"

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

Mouse::Mouse
(
	unsigned int absoluteMouseXMinimum,
	unsigned int absoluteMouseXMaximum,
	unsigned int absoluteMouseYMinimum,
	unsigned int absoluteMouseYMaximum
)
{
	fileDescriptor = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	ioctl(fileDescriptor, UI_SET_EVBIT, EV_KEY);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_RIGHT);

	ioctl(fileDescriptor, UI_SET_EVBIT, EV_REL);
	ioctl(fileDescriptor, UI_SET_RELBIT, REL_X);
	ioctl(fileDescriptor, UI_SET_RELBIT, REL_Y);

	ioctl(fileDescriptor, UI_SET_EVBIT, EV_ABS);
	uinput_abs_setup uinputAbsoluteXSetup;
	uinputAbsoluteXSetup.code = ABS_X;
	uinputAbsoluteXSetup.absinfo.minimum = 0;
	uinputAbsoluteXSetup.absinfo.maximum = 1920;
	uinputAbsoluteXSetup.absinfo.fuzz = 0;
	uinputAbsoluteXSetup.absinfo.flat = 0;
	uinputAbsoluteXSetup.absinfo.resolution = 1;
	ioctl(fileDescriptor, UI_SET_ABSBIT, &uinputAbsoluteXSetup);
	uinput_abs_setup uinputAbsoluteYSetup;
	uinputAbsoluteYSetup.code = ABS_Y;
	uinputAbsoluteYSetup.absinfo.minimum = 0;
	uinputAbsoluteYSetup.absinfo.maximum = 1080;
	uinputAbsoluteYSetup.absinfo.fuzz = 0;
	uinputAbsoluteYSetup.absinfo.flat = 0;
	uinputAbsoluteYSetup.absinfo.resolution = 1;
	ioctl(fileDescriptor, UI_SET_ABSBIT, &uinputAbsoluteYSetup);

	memset(&uinputSetup, 0, sizeof(uinputSetup));
	uinputSetup.id.bustype = BUS_USB;
	uinputSetup.id.vendor = 0x1234;
	uinputSetup.id.product = productID++;
	strcpy(uinputSetup.name, "AutoClicker Mouse Device");

	ioctl(fileDescriptor, UI_DEV_SETUP, &uinputSetup);
	ioctl(fileDescriptor, UI_DEV_CREATE);
}

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

void Mouse::moveRelative(int deltaX, int deltaY)
{
	emit(fileDescriptor, EV_REL, REL_X, deltaX);
	emit(fileDescriptor, EV_REL, REL_Y, deltaY);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
}
void Mouse::moveAbsolute(int absoluteX, int absoluteY)
{
	emit(fileDescriptor, EV_ABS, ABS_X, absoluteX);
	emit(fileDescriptor, EV_ABS, ABS_Y, absoluteY);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
}
