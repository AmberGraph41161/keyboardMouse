#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>

#include <opencv4/opencv2/core/types.hpp>
#include <opencv4/opencv2/core.hpp>
#include <opencv4/opencv2/imgproc.hpp>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1.h"
#include "wlr-screencopy-unstable-v1.h"
#include "wlr-virtual-pointer-unstable-v1.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

void randname(char* buf)
{
	timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	long r = ts.tv_nsec;
	for (int i = 0; i < 6; ++i)
	{
		buf[i] = 'A'+(r&15)+(r&16)*2;
		r >>= 5;
	}
}

int create_shm_file()
{
	int retries = 100;
	do
	{
	char name[] = "/wl_shm-XXXXXX";
		randname(name + sizeof(name) - 7);
		--retries;
		int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
		if (fd >= 0)
		{
			shm_unlink(name);
			return fd;
		}
	} while (retries > 0 && errno == EEXIST);
	return -1;
}

int allocate_shm_file(size_t size)
{
	int fd = create_shm_file();
	if (fd < 0)
	{
		return -1;
	}
	int ret;
	do
	{
		ret = ftruncate(fd, size);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0)
	{
		close(fd);
		return -1;
	}
	return fd;
}

struct Dimension
{
	Dimension(uint32_t width, uint32_t height) : width(width), height(height) {}
	uint32_t width = 0;
	uint32_t height = 0;
};

struct WaylandState
{
	wl_display* display;
	wl_registry* registry;
	wl_compositor* compositor;
	
	std::vector<wl_output*> outputs;
	std::vector<std::string> outputNames;
	std::vector<Dimension> outputsDimensions;
	std::vector<int32_t> outputsFallbackX;
	std::vector<int32_t> outputsFallbackY;
	std::vector<int32_t> outputsTransform;
	std::vector<int32_t> outputsScale;
	int selectedOutputIndex = 0;

	wl_surface* surface;
	wl_buffer* buffer;

	uint32_t sharedMemoryWidth;
	uint32_t sharedMemoryHeight;
	uint32_t sharedMemoryStride;

	wl_shm* sharedMemory;
	int sharedMemoryPoolFileDescriptor;
	uint8_t *sharedMemoryPoolData;
	wl_shm_pool *sharedMemoryPool;
	int sharedMemoryPoolSize;

	//int layerSurfacePaddingTop = -21;
	int layerSurfacePaddingTop = 0;
	int layerSurfacePaddingRight = 0;
	int layerSurfacePaddingBottom = 0;
	int layerSurfacePaddingLeft = 0;
	zwlr_layer_shell_v1* layerShell;
	zwlr_layer_surface_v1* layerSurface;
	bool layerSurfaceShouldClose;

	wl_seat* seat;
	wl_keyboard* keyboard;
	wl_region* region;

	zwlr_screencopy_manager_v1* screenCopyManager;
	zwlr_screencopy_frame_v1* screenCopyFrame;
	uint32_t screenCopyFlags;
	bool screenCopyBufferWasHandled;

	zwlr_virtual_pointer_manager_v1* virtualPointerManager;
	zwlr_virtual_pointer_v1* virtualPointer;
	uint32_t virtualPointerTime = 0;
	unsigned int buttonDownDelayMilliseconds = 20;
	bool shouldClickAndExit = false;

	int openCVBoundingRectCentersCursor = 0;
	std::vector<cv::Point> openCVBoundingRectCenters;
	cv::Scalar openCVRectangleScalar{0, 0, 255}; //BGR
	int openCVRectangleThickness = 1;
	double openCVThreshold = 140;
	double openCVMaxval = 255;
};

void waylandBufferRelease(void* data, wl_buffer* buffer)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	wl_buffer_destroy(waylandState->buffer);
}

const wl_buffer_listener waylandBufferListener =
{
	.release = waylandBufferRelease
};

void screenCopyFrameHandleBuffer
(
	void* data,
	zwlr_screencopy_frame_v1* frame,
	uint32_t format,
	uint32_t width,
	uint32_t height,
	uint32_t stride
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);

	zwlr_screencopy_frame_v1_copy(frame, waylandState->buffer);
}

void screenCopyFrameHandleFlags(void* data, zwlr_screencopy_frame_v1* frame, uint32_t flags)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->screenCopyFlags = flags;
}

void screenCopyFrameHandleReady
(
	void* data,
	zwlr_screencopy_frame_v1* frame,
	uint32_t tv_sec_hi,
	uint32_t tv_sec_lo,
	uint32_t tv_nsec
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->screenCopyBufferWasHandled = true;
}

void screenCopyFrameHandleFailed(void* data, zwlr_screencopy_frame_v1* frame)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	fprintf(stderr, "failed to copy output %s\n", waylandState->outputNames[waylandState->selectedOutputIndex].c_str());
	exit(EXIT_FAILURE);
}

const zwlr_screencopy_frame_v1_listener screenCopyFrameListener =
{
	.buffer = screenCopyFrameHandleBuffer,
	.flags = screenCopyFrameHandleFlags,
	.ready = screenCopyFrameHandleReady,
	.failed = screenCopyFrameHandleFailed
};

void outputHandleGeometry
(
	void* data,
	wl_output* wl_output,
	int32_t x,
	int32_t y,
	int32_t physical_width,
	int32_t physical_height,
	int32_t subpixel,
	const char* make,
	const char* model,
	int32_t transform
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->outputsFallbackX.emplace_back(x);
	waylandState->outputsFallbackY.emplace_back(y);
	waylandState->outputsTransform.emplace_back(transform);

	std::cout << "output geometry handle!" << std::endl;
	std::cout << "wl_output " << wl_output << '\n' <<
	"int32_t x " << x << '\n' <<
	"int32_t y " << y << '\n' <<
	"int32_t physical_width " << physical_width << '\n' <<
	"int32_t physical_height " << physical_height << '\n' <<
	"int32_t subpixel " << subpixel << '\n' <<
	"const char* make " << make << '\n' <<
	"const char* model " << model << '\n' <<
	"int32_t transform " << transform << '\n' << std::endl;
}

void outputHandleMode
(
	void* data,
	wl_output* wl_output,
	uint32_t flags,
	int32_t width,
	int32_t height,
	int32_t refresh
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);

	if ((flags & WL_OUTPUT_MODE_CURRENT) != 0) {
		waylandState->outputsDimensions.emplace_back(Dimension(width, height));
	}

	std::cout << "output mode handle!" << std::endl;
	std::cout << "wl_output* wl_output " << wl_output << '\n' <<
	"uint32_t flags " << flags << '\n' <<
	"int32_t width " << width << '\n' <<
	"int32_t height " << height << '\n' <<
	"int32_t refresh " << refresh << '\n' << std::endl;
}

void outputHandleDone(void* data, wl_output* wl_output) {
	// No-op
}

void outputHandleScale(void* data, wl_output* wl_output, int32_t factor)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->outputsScale.emplace_back(factor);

	std::cout << "output scale handle!" << '\n' <<
	"void* data " << data << '\n' <<
	"wl_output* wl_output " << wl_output << '\n' <<
	"int32_t factor " << factor << '\n' << std::endl;
}

void outputHandleName(void* data, wl_output* wl_output, const char* name)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->outputNames.emplace_back(std::string(name));

	std::cout << "output name handle!" << '\n' <<
	"const char* name " << name << '\n' << std::endl;
}

void outputHandleDescription(void* data, wl_output* wl_output, const char* description)
{
	// No-op
	std::cout << "output description handle!" << '\n' <<
	"const char* description " << description << '\n' << std::endl;
}

const struct wl_output_listener outputListener = {
	.geometry = outputHandleGeometry,
	.mode = outputHandleMode,
	.done = outputHandleDone,
	.scale = outputHandleScale,
	.name = outputHandleName,
	.description = outputHandleDescription,
};

void drawFrame(WaylandState* waylandState)
{
	waylandState->screenCopyBufferWasHandled = false;

	waylandState->screenCopyFrame = zwlr_screencopy_manager_v1_capture_output
	(
		waylandState->screenCopyManager,
		false,
		waylandState->outputs[waylandState->selectedOutputIndex]
	);
	zwlr_screencopy_frame_v1_add_listener(waylandState->screenCopyFrame, &screenCopyFrameListener, waylandState);
	while(!waylandState->screenCopyBufferWasHandled)
	{
		wl_display_dispatch(waylandState->display);
	}

	//note:
	//WL_SHM_FORMAT_XRGB8888 == CV_8UC4
	//cv::Scalar is in BGR format, NOT RGB
	cv::Mat originalMat
	(
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryWidth,
		CV_8UC4,
		waylandState->sharedMemoryPoolData,
		waylandState->sharedMemoryStride
	);
	cv::Mat grayMat(originalMat);
	cv::cvtColor(grayMat, grayMat, cv::COLOR_BGR2GRAY);
	cv::threshold(grayMat, grayMat, waylandState->openCVThreshold, waylandState->openCVMaxval, cv::THRESH_BINARY);

	std::vector<std::vector<cv::Point>> contours;
	cv::findContours(grayMat, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
	for(size_t x = 0; x < contours.size(); ++x)
	{
		cv::Rect boundingRect = cv::boundingRect(contours[x]);
		std::cout << "boundingRect.tl().x " << boundingRect.tl().x << std::endl;
		std::cout << "boundingRect.tl().y " << boundingRect.tl().y << std::endl;

		waylandState->openCVBoundingRectCenters.emplace_back
		(
			cv::Point(boundingRect.tl())
			/*
			cv::Point
			(
				boundingRect.br().x - (boundingRect.tl().x / 2),
				boundingRect.br().y - (boundingRect.tl().y / 2)
			)
			*/
		);
		cv::rectangle
		(
			originalMat,
			boundingRect.tl(),
			boundingRect.br(),
			waylandState->openCVRectangleScalar,
			waylandState->openCVRectangleThickness
		);
	}
}

void layerSurfaceCallback(void* data, wl_callback* callback, uint32_t time); //header needed
const wl_callback_listener layerSurfaceCallbackListener =
{
	.done = layerSurfaceCallback
};

void layerSurfaceCallback(void* data, wl_callback* callback, uint32_t time) //body here also needed
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	wl_callback_destroy(callback);

	callback = wl_surface_frame(waylandState->surface);
	wl_callback_add_listener(callback, &layerSurfaceCallbackListener, waylandState);

	//drawFrame(waylandState);
	//wl_surface_attach(waylandState->surface, waylandState->buffer, 0, 0);
	wl_surface_damage_buffer(waylandState->surface, 0, 0, waylandState->sharedMemoryWidth, waylandState->sharedMemoryHeight);
	wl_surface_commit(waylandState->surface);
}

void layerSurfaceConfigure
(
	void* data,
	zwlr_layer_surface_v1* layerSurface,
	uint32_t serial,
	uint32_t width,
	uint32_t height
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	zwlr_layer_surface_v1_ack_configure(layerSurface, serial);

	waylandState->sharedMemoryWidth = width;
	waylandState->sharedMemoryHeight = height;
	waylandState->sharedMemoryStride = width * 4;

	waylandState->sharedMemoryPoolSize = waylandState->sharedMemoryHeight * waylandState->sharedMemoryStride;

	waylandState->sharedMemoryPoolFileDescriptor = allocate_shm_file(waylandState->sharedMemoryPoolSize);
	if(waylandState->sharedMemoryPoolFileDescriptor == -1)
	{
		exit(EXIT_FAILURE);
	}

	waylandState->sharedMemoryPoolData = static_cast<uint8_t*>
	(
		mmap
		(
			NULL,
			waylandState->sharedMemoryPoolSize,
			PROT_READ | PROT_WRITE,
			MAP_SHARED,
			waylandState->sharedMemoryPoolFileDescriptor,
			0
		)
	);

	if(waylandState->sharedMemoryPoolData == MAP_FAILED)
	{
		close(waylandState->sharedMemoryPoolFileDescriptor);
		exit(EXIT_FAILURE);
	}

	waylandState->sharedMemoryPool = wl_shm_create_pool
	(
		waylandState->sharedMemory,
		waylandState->sharedMemoryPoolFileDescriptor,
		waylandState->sharedMemoryPoolSize
	);

	int index = 0;
	int offset = waylandState->sharedMemoryHeight * waylandState->sharedMemoryStride * index;
	waylandState->buffer = wl_shm_pool_create_buffer
	(
		waylandState->sharedMemoryPool,
		offset,
		waylandState->sharedMemoryWidth,
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryStride,
		WL_SHM_FORMAT_XRGB8888
	);
	wl_buffer_add_listener(waylandState->buffer, &waylandBufferListener, waylandState);

	drawFrame(waylandState);
	wl_surface_attach(waylandState->surface, waylandState->buffer, 0, 0);
	wl_surface_commit(waylandState->surface);
}

void layerSurfaceClosed(void* data, zwlr_layer_surface_v1* layerSurface)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->layerSurfaceShouldClose = true;
}

const zwlr_layer_surface_v1_listener layerSurfaceListener =
{
	.configure = layerSurfaceConfigure,
	.closed = layerSurfaceClosed
};

void waylandKeyboardHandleKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int fd, uint32_t size)
{
}
void waylandKeyboardHandleEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys)
{
}
void waylandKeyboardHandleLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface)
{
}

void waylandKeyboardHandleKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
	//note: key matches up with evdev keycodes
	WaylandState* waylandState = static_cast<WaylandState*>(data);

	std::string command;
	if(state == WL_KEYBOARD_KEY_STATE_PRESSED || state == WL_KEYBOARD_KEY_STATE_REPEATED)
	{
		switch(key)
		{
			case KEY_ESC:
				waylandState->layerSurfaceShouldClose = true;
				break;

			case KEY_L:
			{
				++waylandState->openCVBoundingRectCentersCursor;
				if(waylandState->openCVBoundingRectCentersCursor >= waylandState->openCVBoundingRectCenters.size())
				{
					waylandState->openCVBoundingRectCentersCursor = 0;
				}

				zwlr_virtual_pointer_v1_motion_absolute
				(
					waylandState->virtualPointer,
					waylandState->virtualPointerTime,
					waylandState->openCVBoundingRectCenters[waylandState->openCVBoundingRectCentersCursor].x,
					waylandState->openCVBoundingRectCenters[waylandState->openCVBoundingRectCentersCursor].y,
					waylandState->outputsDimensions[waylandState->selectedOutputIndex].width,
					waylandState->outputsDimensions[waylandState->selectedOutputIndex].height
				);
				++waylandState->virtualPointerTime;
				zwlr_virtual_pointer_v1_frame(waylandState->virtualPointer);
				wl_display_flush(waylandState->display);
				break;
			}

			case KEY_H:
			{
				--waylandState->openCVBoundingRectCentersCursor;
				if(waylandState->openCVBoundingRectCentersCursor < 0)
				{
					waylandState->openCVBoundingRectCentersCursor = waylandState->openCVBoundingRectCenters.size() - 1;
				}
				zwlr_virtual_pointer_v1_motion_absolute
				(
					waylandState->virtualPointer,
					waylandState->virtualPointerTime,
					waylandState->openCVBoundingRectCenters[waylandState->openCVBoundingRectCentersCursor].x,
					waylandState->openCVBoundingRectCenters[waylandState->openCVBoundingRectCentersCursor].y,
					waylandState->outputsDimensions[waylandState->selectedOutputIndex].width,
					waylandState->outputsDimensions[waylandState->selectedOutputIndex].height
				);
				++waylandState->virtualPointerTime;
				zwlr_virtual_pointer_v1_frame(waylandState->virtualPointer);
				wl_display_flush(waylandState->display);
				break;
			}

			case KEY_SPACE:
			{
				waylandState->shouldClickAndExit = true;
				break;
			}
		}
	} else if(state == WL_KEYBOARD_KEY_STATE_RELEASED)
	{
	}
}

void waylandKeyboardHandleModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
}
void waylandKeyboardHandleRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay)
{
}

const wl_keyboard_listener waylandKeyboardListener =
{
	.keymap = waylandKeyboardHandleKeymap,
	.enter = waylandKeyboardHandleEnter,
	.leave = waylandKeyboardHandleLeave,
	.key = waylandKeyboardHandleKey,
	.modifiers = waylandKeyboardHandleModifiers,
	.repeat_info = waylandKeyboardHandleRepeatInfo
};

void waylandRegistryHandleGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	if(strcmp(interface, wl_compositor_interface.name) == 0)
	{
		waylandState->compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, version));
	} else if(strcmp(interface, wl_shm_interface.name) == 0)
	{
		waylandState->sharedMemory = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, version));
	} else if(strcmp(interface, wl_output_interface.name) == 0)
	{
		waylandState->outputs.emplace_back(static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, version)));
		wl_output_add_listener(waylandState->outputs.back(), &outputListener, waylandState);
	} else if(strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
	{
		waylandState->layerShell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, version));
	} else if(strcmp(interface, wl_seat_interface.name) == 0)
	{
		waylandState->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, version));
		waylandState->keyboard = wl_seat_get_keyboard(waylandState->seat);
		wl_keyboard_add_listener(waylandState->keyboard, &waylandKeyboardListener, waylandState);
	} else if(strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0)
	{
		waylandState->screenCopyManager = static_cast<zwlr_screencopy_manager_v1*>(wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1));
	} else if(strcmp(interface, zwlr_virtual_pointer_manager_v1_interface.name) == 0)
	{
		waylandState->virtualPointerManager = static_cast<zwlr_virtual_pointer_manager_v1*>(wl_registry_bind(registry, name, &zwlr_virtual_pointer_manager_v1_interface, version));
	}
}

void waylandRegistryHandleGlobalRemove(void* data, wl_registry* registry, uint32_t name)
{
	//"intentionally left blank"
}

int main()
{
	WaylandState waylandState;

	waylandState.display = wl_display_connect(getenv("WAYLAND_DISPLAY"));
	if(!waylandState.display)
	{
		std::cerr << "failed to connect to waylandDisplay" << std::endl;
		exit(EXIT_FAILURE);
	}
	std::cout << "connected to waylandDisplay" << std::endl;

	waylandState.registry = wl_display_get_registry(waylandState.display);
	const wl_registry_listener waylandRegistryListener
	{
		.global = waylandRegistryHandleGlobal,
		.global_remove = waylandRegistryHandleGlobalRemove
	};
	wl_registry_add_listener(waylandState.registry, &waylandRegistryListener, &waylandState);
	wl_display_roundtrip(waylandState.display); //fetch globals
	wl_display_roundtrip(waylandState.display); //process globals

	waylandState.virtualPointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(waylandState.virtualPointerManager, waylandState.seat);
	if(waylandState.virtualPointer == nullptr)
	{
		std::cout << "null!" << std::endl;
	}

	const std::string targetMonitorName = "eDP-1";
	for(size_t x = 0; x < waylandState.outputNames.size(); ++x)
	{
		if(waylandState.outputNames[x] == targetMonitorName)
		{
			waylandState.selectedOutputIndex = x;
			break;
		}
	}

	waylandState.layerSurfaceShouldClose = false;
	waylandState.surface = wl_compositor_create_surface(waylandState.compositor);
	waylandState.layerSurface = zwlr_layer_shell_v1_get_layer_surface
	(
		waylandState.layerShell,
		waylandState.surface,
		waylandState.outputs[waylandState.selectedOutputIndex],
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		"keyboardMouseOverlay"
	);
	zwlr_layer_surface_v1_set_keyboard_interactivity(waylandState.layerSurface, true);
	zwlr_layer_surface_v1_set_exclusive_zone(waylandState.layerSurface, -1); //allows surface to sit on top of waybar
	zwlr_layer_surface_v1_add_listener(waylandState.layerSurface, &layerSurfaceListener, &waylandState);
	zwlr_layer_surface_v1_set_size
	(
		waylandState.layerSurface,
		waylandState.outputsDimensions[waylandState.selectedOutputIndex].width,
		waylandState.outputsDimensions[waylandState.selectedOutputIndex].height
	);
	zwlr_layer_surface_v1_set_anchor(waylandState.layerSurface, ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
	zwlr_layer_surface_v1_set_margin
	(
		waylandState.layerSurface,
		waylandState.layerSurfacePaddingTop,
		waylandState.layerSurfacePaddingRight,
		waylandState.layerSurfacePaddingBottom,
		waylandState.layerSurfacePaddingLeft
	);

	wl_callback* callback = wl_surface_frame(waylandState.surface);
	wl_callback_add_listener(callback, &layerSurfaceCallbackListener, &waylandState);

	waylandState.region = wl_compositor_create_region(waylandState.compositor);
	wl_surface_set_input_region(waylandState.surface, waylandState.region);

	wl_surface_commit(waylandState.surface);
	wl_display_flush(waylandState.display);

	while
	(
		!waylandState.layerSurfaceShouldClose &&
		!waylandState.shouldClickAndExit &&
		wl_display_dispatch(waylandState.display) != -1
	)
	{
		//std::cout << "running!" << std::endl;
	}

	/*
	std::vector<std::filesystem::path> devicesPaths;
	for
	(
		std::filesystem::directory_iterator it("/dev/input/");
		it != std::filesystem::directory_iterator{};
		++it
	)
	{
		std::filesystem::directory_entry entry = *(it);
		devicesPaths.push_back(entry.path());
	}

	std::cout << "select device:" << std::endl;
	for(int x = 0; x < devicesPaths.size(); ++x)
	{
		int fileDescriptor = open(devicesPaths[x].c_str(), O_RDONLY);
		if(fileDescriptor == -1)
		{
			++x;
			continue;
		}
		char deviceName[256];
		if(ioctl(fileDescriptor, EVIOCGNAME(sizeof(deviceName)), deviceName) == -1)
		{
			++x;
			continue;
		}

		std::cout << x << ") " << deviceName << ' ' << devicesPaths[x] << std::endl;
	}
	std::cout << "> ";

	int chosenDeviceIndex;
	try
	{
		std::string getlinestring;
		std::getline(std::cin, getlinestring);
		chosenDeviceIndex = std::stoi(getlinestring);
	} catch(...)
	{
		chosenDeviceIndex = 0;
	}

	int keyboardFileDescriptor = open(devicesPaths[chosenDeviceIndex].c_str(), O_RDONLY);
	if(keyboardFileDescriptor == -1)
	{
		perror("Something went wrong while opening device...");
		exit(EXIT_FAILURE);
	}

	input_event inputEvent;
	while(true)
	{
		ssize_t readSize = read(keyboardFileDescriptor, &inputEvent, sizeof(inputEvent));
		if(readSize == (ssize_t)(-1))
		{
			perror("Error reading input device inputEvent!");
			break;
		}

		if(readSize == (ssize_t)(0))
		{
			std::cout << "nothing read. EOF maybe?" << std::endl;
			break;
		}

		if(inputEvent.type == EV_KEY)
		{
			if(inputEvent.value == 1)
			{
				std::cout << inputEvent.code << " pressed!" << std::endl;
				if(inputEvent.code == KEY_A)
				{
					break;
				}
			} else
			{
				std::cout << inputEvent.code << " released!" << std::endl;
			}
		}
	}

	close(keyboardFileDescriptor);
	*/




	if(!waylandState.outputs.empty())
	{
		for(size_t x = 0; x < waylandState.outputs.size(); ++x)
		{
			wl_output_destroy(waylandState.outputs[x]);
		}
	}
	if(waylandState.registry)
	{
		wl_registry_destroy(waylandState.registry);
	}
	if(waylandState.compositor)
	{
		wl_compositor_destroy(waylandState.compositor);
	}
	if(waylandState.sharedMemory)
	{
		wl_shm_destroy(waylandState.sharedMemory);
	}
	if(waylandState.surface)
	{
		wl_surface_destroy(waylandState.surface);
	}
	if(waylandState.buffer)
	{
		wl_buffer_destroy(waylandState.buffer);
	}
	if(waylandState.layerShell)
	{
		zwlr_layer_shell_v1_destroy(waylandState.layerShell);
	}
	if(waylandState.layerSurface)
	{
		zwlr_layer_surface_v1_destroy(waylandState.layerSurface);
	}
	if(waylandState.screenCopyManager)
	{
		zwlr_screencopy_manager_v1_destroy(waylandState.screenCopyManager);
	}
	if(waylandState.screenCopyFrame)
	{
		zwlr_screencopy_frame_v1_destroy(waylandState.screenCopyFrame);
	}
	wl_shm_pool_destroy(waylandState.sharedMemoryPool);
	close(waylandState.sharedMemoryPoolFileDescriptor);
	munmap(waylandState.sharedMemoryPoolData, waylandState.sharedMemoryPoolSize);
	if(waylandState.seat)
	{
		wl_seat_destroy(waylandState.seat);
	}
	if(waylandState.keyboard)
	{
		wl_keyboard_destroy(waylandState.keyboard);
	}

	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	waylandState.virtualPointerTime += waylandState.buttonDownDelayMilliseconds;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);

	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
	waylandState.virtualPointerTime += waylandState.buttonDownDelayMilliseconds;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);

	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	waylandState.virtualPointerTime += waylandState.buttonDownDelayMilliseconds;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);

	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
	waylandState.virtualPointerTime += waylandState.buttonDownDelayMilliseconds;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);

	if(waylandState.virtualPointerManager)
	{
		zwlr_virtual_pointer_manager_v1_destroy(waylandState.virtualPointerManager);
	}
	if(waylandState.virtualPointer)
	{
		zwlr_virtual_pointer_v1_destroy(waylandState.virtualPointer);
	}
	if(waylandState.region)
	{
		wl_region_destroy(waylandState.region);
	}

	if(waylandState.display)
	{
		//must be called last!
		wl_display_disconnect(waylandState.display);
	}
	return 0;
}
