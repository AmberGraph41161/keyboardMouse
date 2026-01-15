#include "virtualInputDevice.hpp"

#include <linux/uinput.h>
#include <unistd.h>
#include <fcntl.h>

int VirtualInputDevice::productID = 0x0000;

VirtualInputDevice::VirtualInputDevice() : recommendedMillisecondsSleepBetweenActions(25)
{
}

VirtualInputDevice::~VirtualInputDevice()
{
	ioctl(fileDescriptor, UI_DEV_DESTROY);
	close(fileDescriptor);
}

void VirtualInputDevice::emit(int fileDescriptor, int type, int code, int val)
{
	input_event ie;

	ie.type = type;
	ie.code = code;
	ie.value = val;
	/* timestamp values below are ignored */
	ie.time.tv_sec = 0;
	ie.time.tv_usec = 0;

	write(fileDescriptor, &ie, sizeof(ie));
}

int VirtualInputDevice::getRecommendedMillisecondsSleepBetweenActions()
{
	return recommendedMillisecondsSleepBetweenActions;
}

void VirtualInputDevice::overwriteRecommendedMillisecondsSleepBetweenActions(int millisecondsSleepBetweenActions)
{
	recommendedMillisecondsSleepBetweenActions = millisecondsSleepBetweenActions;
}
