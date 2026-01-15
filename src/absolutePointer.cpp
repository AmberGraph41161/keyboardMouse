#include "absolutePointer.hpp"

#include <linux/input-event-codes.h>
#include <linux/uinput.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>

AbsolutePointer::AbsolutePointer
(
	unsigned int absoluteAbsolutePointerXMinimum,
	unsigned int absoluteAbsolutePointerXMaximum,
	unsigned int absoluteAbsolutePointerYMinimum,
	unsigned int absoluteAbsolutePointerYMaximum
)
{
	fileDescriptor = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

	ioctl(fileDescriptor, UI_SET_EVBIT, EV_KEY);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_TOOL_PEN);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl(fileDescriptor, UI_SET_KEYBIT, BTN_MIDDLE);

	ioctl(fileDescriptor, UI_SET_EVBIT, EV_ABS);
	ioctl(fileDescriptor, UI_SET_ABSBIT, ABS_X);
	ioctl(fileDescriptor, UI_SET_ABSBIT, ABS_Y);
	uinput_abs_setup uinputAbsoluteXSetup;
	uinputAbsoluteXSetup.code = ABS_X;
	uinputAbsoluteXSetup.absinfo.minimum = absoluteAbsolutePointerXMinimum;
	uinputAbsoluteXSetup.absinfo.maximum = absoluteAbsolutePointerXMaximum;
	uinputAbsoluteXSetup.absinfo.fuzz = 0;
	uinputAbsoluteXSetup.absinfo.flat = 0;
	uinputAbsoluteXSetup.absinfo.resolution = 1;
	ioctl(fileDescriptor, UI_ABS_SETUP, &uinputAbsoluteXSetup);
	uinput_abs_setup uinputAbsoluteYSetup;
	uinputAbsoluteYSetup.code = ABS_Y;
	uinputAbsoluteYSetup.absinfo.minimum = absoluteAbsolutePointerYMinimum;
	uinputAbsoluteYSetup.absinfo.maximum = absoluteAbsolutePointerYMaximum;
	uinputAbsoluteYSetup.absinfo.fuzz = 0;
	uinputAbsoluteYSetup.absinfo.flat = 0;
	uinputAbsoluteYSetup.absinfo.resolution = 1;
	ioctl(fileDescriptor, UI_ABS_SETUP, &uinputAbsoluteYSetup);

	memset(&uinputSetup, 0, sizeof(uinputSetup));
	uinputSetup.id.bustype = BUS_USB;
	uinputSetup.id.vendor = 0x1234;
	uinputSetup.id.product = productID++;
	strcpy(uinputSetup.name, "virtualInputDevice AbsolutePointer");

	ioctl(fileDescriptor, UI_DEV_SETUP, &uinputSetup);
	ioctl(fileDescriptor, UI_DEV_CREATE);
}

void AbsolutePointer::moveAbsolute(int absoluteX, int absoluteY)
{
	emit(fileDescriptor, EV_KEY, BTN_TOOL_PEN, 1);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);

	emit(fileDescriptor, EV_ABS, ABS_X, absoluteX);
	emit(fileDescriptor, EV_ABS, ABS_Y, absoluteY);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);

	emit(fileDescriptor, EV_KEY, BTN_TOOL_PEN, 0);
	emit(fileDescriptor, EV_SYN, SYN_REPORT, 0);
}
