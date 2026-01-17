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


void VirtualInputDevice::emit(int fileDescriptor, input_event inputEvent)
{
	write(fileDescriptor, &inputEvent, sizeof(inputEvent));
}

void VirtualInputDevice::emit(int fileDescriptor, int type, int code, int val)
{
	input_event inputEvent;

	inputEvent.type = type;
	inputEvent.code = code;
	inputEvent.value = val;
	/* timestamp values below are ignored */
	inputEvent.time.tv_sec = 0;
	inputEvent.time.tv_usec = 0;

	write(fileDescriptor, &inputEvent, sizeof(inputEvent));
}

int VirtualInputDevice::getRecommendedMillisecondsSleepBetweenActions()
{
	return recommendedMillisecondsSleepBetweenActions;
}

void VirtualInputDevice::overwriteRecommendedMillisecondsSleepBetweenActions(int millisecondsSleepBetweenActions)
{
	recommendedMillisecondsSleepBetweenActions = millisecondsSleepBetweenActions;
}
