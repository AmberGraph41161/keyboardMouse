#include <iostream>
#include <filesystem>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>

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
	wl_display* display;
	wl_registry* registry;
	wl_compositor* compositor;
	wl_shm* sharedMemory;
	wl_surface* surface;
	xdg_surface* xdgSurface;
	xdg_toplevel* xdgTopLevel;
	xdg_wm_base* xdgWmBase;
};

void waylandBufferRelease(void* data, wl_buffer* buffer)
{
	wl_buffer_destroy(buffer);
}

const wl_buffer_listener waylandBufferListener =
{
	.release = waylandBufferRelease
};

wl_buffer* drawFrame(WaylandState* waylandState)
{
	const int width = 1920;
	const int height = 1080;
	const int stride = width * 4;
	const int shm_pool_size = height * stride * 2;

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
	wl_buffer *waylandBuffer = wl_shm_pool_create_buffer(pool, offset, width, height, stride, WL_SHM_FORMAT_XRGB8888);
	wl_shm_pool_destroy(pool);
	close(waylandSharedMemoryPoolFileDescriptor);

	uint32_t *pixels = (uint32_t*)&pool_data[offset];
	for (int y = 0; y < height; ++y)
	{
		for (int x = 0; x < width; ++x)
		{
			if ((x + y / 8 * 8) % 16 < 8)
			{
				pixels[y * width + x] = 0xFF666666;
			} else
			{
				pixels[y * width + x] = 0xFFEEEEEE;
			}
		}
	}

	munmap(pool_data, shm_pool_size);
	wl_buffer_add_listener(waylandBuffer, &waylandBufferListener, NULL);
	return waylandBuffer;
}

void xdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	xdg_surface_ack_configure(xdgSurface, serial);

	wl_buffer* buffer = drawFrame(waylandState);
	wl_surface_attach(waylandState->surface, buffer, 0, 0);
	wl_surface_commit(waylandState->surface);
}

static const xdg_surface_listener xdgSurfaceListener =
{
	.configure  = xdgSurfaceConfigure
};

void xdgWmBasePing(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
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
	} else if(strcmp(interface, xdg_wm_base_interface.name) == 0)
	{
		waylandState->xdgWmBase = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
		xdg_wm_base_add_listener(waylandState->xdgWmBase, &xdgWmBaseListener, waylandState);
	}
}

void waylandRegistryHandleGlobalRemove(void* data, wl_registry* registry, uint32_t name)
{
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

	waylandState.surface = wl_compositor_create_surface(waylandState.compositor);
	waylandState.xdgSurface = xdg_wm_base_get_xdg_surface(waylandState.xdgWmBase, waylandState.surface);
	xdg_surface_add_listener(waylandState.xdgSurface, &xdgSurfaceListener, &waylandState);
	waylandState.xdgTopLevel = xdg_surface_get_toplevel(waylandState.xdgSurface);
    xdg_toplevel_set_title(waylandState.xdgTopLevel, "Example client");
    wl_surface_commit(waylandState.surface);

	wl_output* waylandOutput = nullptr;
	zwlr_screencopy_manager_v1* screenCopyManager = nullptr;

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

	wl_display_disconnect(waylandState.display);

	return 0;
}
