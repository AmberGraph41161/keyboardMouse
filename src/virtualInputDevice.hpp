#ifndef VIRTUALINPUTDEVICE_HPP
#define VIRTUALINPUTDEVICE_HPP

#include <linux/uinput.h>

class VirtualInputDevice
{
protected:
	VirtualInputDevice();
	~VirtualInputDevice();

protected:
	void emit(int fileDescriptor, int type, int code, int val);

public:
	int getRecommendedMillisecondsSleepBetweenActions();
	void overwriteRecommendedMillisecondsSleepBetweenActions(int millisecondsSleepBetweenActions);

protected:
	int recommendedMillisecondsSleepBetweenActions;
	uinput_setup uinputSetup;
	int fileDescriptor;
	static int productID;
};

#endif //VIRTUALINPUTDEVICE_HPP
