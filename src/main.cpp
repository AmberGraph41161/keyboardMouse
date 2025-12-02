#include <iostream>
#include <filesystem>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>

#include <opencv4/opencv2/imgproc.hpp>
#include <opencv4/opencv2/core.hpp>
#include <opencv4/opencv2/highgui.hpp>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "xdg-shell.h"
#include "wlr-screencopy-unstable-v1.h"

#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/input.h>

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

struct WaylandState
{
	wl_output* output;
	wl_display* display;
	wl_registry* registry;
	wl_compositor* compositor;

	wl_shm* sharedMemory;
	wl_surface* surface;
	wl_buffer* buffer;

	xdg_surface* xdgSurface;
	xdg_toplevel* xdgTopLevel;
	xdg_wm_base* xdgWmBase;

	zwlr_screencopy_manager_v1* screenCopyManager;
	zwlr_screencopy_frame_v1* frame;
	uint32_t screenCopyFlags;
	bool handledBuffer;
	char* outputName;
	int32_t fallbackX;
	int32_t fallbackY;
	int32_t transform;
	int32_t modeWidth;
	int32_t modeHeight;
	int32_t scale;
};

void waylandBufferRelease(void* data, wl_buffer* buffer)
{
	wl_buffer_destroy(buffer);
}

const wl_buffer_listener waylandBufferListener =
{
	.release = waylandBufferRelease
};

void screenCopyFrameHandleBuffer
(
	void *data,
	zwlr_screencopy_frame_v1 *frame,
	uint32_t format,
	uint32_t width,
	uint32_t height,
	uint32_t stride
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);

	zwlr_screencopy_frame_v1_copy(frame, waylandState->buffer);
}

void screenCopyFrameHandleFlags(void *data, zwlr_screencopy_frame_v1 *frame, uint32_t flags)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->screenCopyFlags = flags;
}

void screenCopyFrameHandleReady
(
	void *data,
	zwlr_screencopy_frame_v1 *frame,
	uint32_t tv_sec_hi,
	uint32_t tv_sec_lo,
	uint32_t tv_nsec
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->handledBuffer = true;
}

void screenCopyFrameHandleFailed(void *data, zwlr_screencopy_frame_v1 *frame)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	fprintf(stderr, "failed to copy output %s\n", waylandState->outputName);
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
	void *data,
	wl_output *wl_output,
	int32_t x,
	int32_t y,
	int32_t physical_width,
	int32_t physical_height,
	int32_t subpixel,
	const char *make,
	const char *model,
	int32_t transform
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->fallbackX = x;
	waylandState->fallbackY = y;
	waylandState->transform = transform;
}

void outputHandleMode
(
	void *data,
	wl_output *wl_output,
	uint32_t flags,
	int32_t width,
	int32_t height,
	int32_t refresh
)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);

	if ((flags & WL_OUTPUT_MODE_CURRENT) != 0) {
		waylandState->modeWidth = width;
		waylandState->modeHeight = height;
	}
}

void outputHandleDone(void *data, wl_output *wl_output) {
	// No-op
}

void outputHandleScale(void *data, wl_output *wl_output, int32_t factor)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->scale = factor;
}

void outputHandleName(void *data, wl_output *wl_output, const char *name)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->outputName = strdup(name);
}

void outputHandleDescription(void *data, wl_output *wl_output, const char *description)
{
	// No-op
}

const struct wl_output_listener outputListener = {
	.geometry = outputHandleGeometry,
	.mode = outputHandleMode,
	.done = outputHandleDone,
	.scale = outputHandleScale,
	.name = outputHandleName,
	.description = outputHandleDescription,
};

wl_buffer* drawFrame(WaylandState* waylandState)
{
	waylandState->handledBuffer = false;

	const int width = 1920;
	const int height = 1080;
	const int stride = width * 4;
	const int shm_pool_size = height * stride;

	int waylandSharedMemoryPoolFileDescriptor = allocate_shm_file(shm_pool_size);
	if(waylandSharedMemoryPoolFileDescriptor == -1)
	{
		return NULL;
	}

	uint8_t *pool_data = static_cast<uint8_t*>(mmap(NULL, shm_pool_size, PROT_READ | PROT_WRITE, MAP_SHARED, waylandSharedMemoryPoolFileDescriptor, 0));
	if(pool_data == MAP_FAILED)
	{
		close(waylandSharedMemoryPoolFileDescriptor);
		return NULL;
	}

	wl_shm_pool *pool = wl_shm_create_pool(waylandState->sharedMemory, waylandSharedMemoryPoolFileDescriptor, shm_pool_size);

	int index = 0;
	int offset = height * stride * index;
	waylandState->buffer = wl_shm_pool_create_buffer(pool, offset, width, height, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(waylandSharedMemoryPoolFileDescriptor);

	waylandState->frame = zwlr_screencopy_manager_v1_capture_output(waylandState->screenCopyManager, false, waylandState->output);
	zwlr_screencopy_frame_v1_add_listener(waylandState->frame, &screenCopyFrameListener, waylandState);
	while(!waylandState->handledBuffer)
	{
		wl_display_dispatch(waylandState->display);
	}

	//note:
	//WL_SHM_FORMAT_XRGB8888 == CV_8UC4
	//cv::Scalar is in BGR format, NOT RGB
	cv::Mat originalMat(height, width, CV_8UC4, pool_data, stride);
	cv::Mat grayMat(originalMat);
	cv::cvtColor(grayMat, grayMat, cv::COLOR_BGR2GRAY);
	cv::threshold(grayMat, grayMat, 100, 255, cv::THRESH_BINARY);
	std::vector<std::vector<cv::Point>> contours;
	cv::findContours(grayMat, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
	for(size_t x = 0; x < contours.size(); ++x)
	{
		cv::Rect boundingRect = cv::boundingRect(contours[x]);
		cv::rectangle(originalMat, boundingRect.tl(), boundingRect.br(), cv::Scalar(10, 10, 255), 1);
	}

	munmap(pool_data, shm_pool_size);
	return waylandState->buffer;
}

void xdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	xdg_surface_ack_configure(xdgSurface, serial);

	wl_buffer* buffer = drawFrame(waylandState);
	wl_surface_attach(waylandState->surface, buffer, 0, 0);
	wl_surface_commit(waylandState->surface);
}

const xdg_surface_listener xdgSurfaceListener =
{
	.configure  = xdgSurfaceConfigure
};

void xdgWmBasePing(void *data, xdg_wm_base *xdg_wm_base, uint32_t serial)
{
	xdg_wm_base_pong(xdg_wm_base, serial);
}

const xdg_wm_base_listener xdgWmBaseListener =
{
	.ping = xdgWmBasePing,
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
		waylandState->scale = 1;
		waylandState->output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, version));
		wl_output_add_listener(waylandState->output, &outputListener, waylandState);
	} else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
	{
		waylandState->xdgWmBase = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
		xdg_wm_base_add_listener(waylandState->xdgWmBase, &xdgWmBaseListener, waylandState);
	} else if(strcmp(interface, zwlr_screencopy_manager_v1_interface.name) == 0)
	{
		waylandState->screenCopyManager = static_cast<zwlr_screencopy_manager_v1*>(wl_registry_bind(registry, name, &zwlr_screencopy_manager_v1_interface, 1));
	}
}

void waylandRegistryHandleGlobalRemove(void* data, wl_registry* registry, uint32_t name)
{
	//"intentionally left blank"
}

int main()
{
	WaylandState waylandState = { 0 };

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

	waylandState.surface = wl_compositor_create_surface(waylandState.compositor);
	waylandState.xdgSurface = xdg_wm_base_get_xdg_surface(waylandState.xdgWmBase, waylandState.surface);
	xdg_surface_add_listener(waylandState.xdgSurface, &xdgSurfaceListener, &waylandState);
	waylandState.xdgTopLevel = xdg_surface_get_toplevel(waylandState.xdgSurface);
	xdg_toplevel_set_title(waylandState.xdgTopLevel, "Example client");
	wl_surface_commit(waylandState.surface);

	int counter = 0;
	while(wl_display_dispatch(waylandState.display) != -1)
	{
		++counter;
		std::cout << counter << std::endl;
		if(counter >= 30)
		{
			break;
		}
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

	wl_buffer_add_listener(waylandState.buffer, &waylandBufferListener, NULL);
	wl_display_disconnect(waylandState.display);

	return 0;
}
