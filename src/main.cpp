#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <unordered_map>

#include <opencv4/opencv2/core/types.hpp>
#include <opencv4/opencv2/core.hpp>
#include <opencv4/opencv2/imgproc.hpp>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1.h"
#include "wlr-screencopy-unstable-v1.h"
#include "wlr-virtual-pointer-unstable-v1.h"
#include "xdg-output-unstable-v1.h"

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <libinput.h>
#include <libudev.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

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

struct Position
{
	Position(uint32_t x, uint32_t y) : x(x), y(y) {}
	uint32_t x = 0;
	uint32_t y = 0;
};

struct WaylandState
{
	wl_display* display = nullptr;
	wl_registry* registry = nullptr;
	wl_compositor* compositor = nullptr;
	
	std::vector<wl_output*> outputs;
	std::vector<std::string> outputsNames;
	std::vector<Dimension> outputsDimensions;
	int selectedOutputIndex = 0;

	zxdg_output_manager_v1* xdgOutputManager = nullptr;
	std::vector<Position> outputsPositions;

	wl_surface* surface = nullptr;
	wl_buffer* screenshotBuffer = nullptr;
	wl_buffer* buffer = nullptr;

	const uint32_t sharedMemoryNBuffers = 2;
	uint32_t sharedMemoryWidth;
	uint32_t sharedMemoryHeight;
	uint32_t sharedMemoryStride;
	wl_shm* sharedMemory = nullptr;
	int sharedMemoryPoolFileDescriptor;
	uint8_t *sharedMemoryPoolData = nullptr;
	wl_shm_pool *sharedMemoryPool = nullptr;
	uint32_t sharedMemoryPoolSize;
	uint32_t sharedMemoryFrameSize;

	//int layerSurfacePaddingTop = -21;
	int layerSurfacePaddingTop = 0;
	int layerSurfacePaddingRight = 0;
	int layerSurfacePaddingBottom = 0;
	int layerSurfacePaddingLeft = 0;
	zwlr_layer_shell_v1* layerShell = nullptr;
	zwlr_layer_surface_v1* layerSurface = nullptr;
	bool layerSurfaceShouldClose;

	wl_seat* seat = nullptr;
	wl_region* region = nullptr;

	zwlr_screencopy_manager_v1* screenCopyManager = nullptr;
	zwlr_screencopy_frame_v1* screenCopyFrame = nullptr;
	uint32_t screenCopyFlags;
	bool screenCopyBufferWasHandled;

	zwlr_virtual_pointer_manager_v1* virtualPointerManager;
	zwlr_virtual_pointer_v1* virtualPointer;
	uint32_t virtualPointerTime = 0;
	uint32_t virtualPointerXExtent = 0;
	uint32_t virtualPointerYExtent = 0;
	uint32_t virtualPointerXOrigin = 0;
	uint32_t virtualPointerYOrigin = 0;
	bool virtualPointerHasJumped = true;
	bool shouldClickAndExit = false;

	cv::Mat openCVInitialFrame;
	cv::Scalar openCVRectangleScalar{0, 0, 255, 255}; //BGRA
	int openCVRectangleThickness = 2;
	int openCVCannyLowerThreshold = 0;
	int openCVCannyUpperThreshold = 255;
	int openCVCannyApetureSize = 3; //only allowed to be 3, 5, or 7
	bool openCVCannyL2Gradient = false;

	int letterCombinationsWidth = 1;
	std::string userKeyboardInput = "";
	std::unordered_map<std::string, cv::Point> letterCombinationsToClickPoints;
	std::unordered_map<std::string, cv::Rect> letterCombinationsToRects;
	cv::Rect drawArea;
	int drawAreaResizeCount = 0;
	std::array<int, 3> drawResizeDivisorFromNthDraw = { 8, 8, 2 };
};

void waylandBufferRelease(void* data, wl_buffer* buffer)
{
	//WaylandState* waylandState = static_cast<WaylandState*>(data);
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

	zwlr_screencopy_frame_v1_copy(frame, waylandState->screenshotBuffer);
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
	fprintf(stderr, "failed to copy output %s\n", waylandState->outputsNames[waylandState->selectedOutputIndex].c_str());
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
	//zxdg_output_v1 handles instead
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
	//zxdg_output_v1 handles instead
}

void outputHandleDone(void* data, wl_output* wl_output)
{
}

void outputHandleScale(void* data, wl_output* wl_output, int32_t factor)
{
	//zxdg_output_v1 handles instead
}

void outputHandleName(void* data, wl_output* wl_output, const char* name)
{
	//zxdg_output_v1 handles instead
}

void outputHandleDescription(void* data, wl_output* wl_output, const char* description)
{
	//zxdg_output_v1 handles instead
}

const struct wl_output_listener outputListener = {
	.geometry = outputHandleGeometry,
	.mode = outputHandleMode,
	.done = outputHandleDone,
	.scale = outputHandleScale,
	.name = outputHandleName,
	.description = outputHandleDescription,
};

void zxdgOutputHandleLogicalPosition(void *data, zxdg_output_v1 *zxdgOutput, int32_t x, int32_t y)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->outputsPositions.emplace_back(Position(x, y));
}

void zxdgOutputHandleLogicalSize(void *data, zxdg_output_v1 *zxdgOutput, int32_t width, int32_t height)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->outputsDimensions.emplace_back(Dimension(width, height));
}

void zxdgOutputHandleDone(void *data, zxdg_output_v1 *zxdgOutput)
{
	//cleaned later
}

void zxdgOutputHandleName(void *data, zxdg_output_v1 *zxdgOutput, const char *name)
{
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->outputsNames.emplace_back(std::string(name));
}

void zxdgOutputHandleDescription(void *data, zxdg_output_v1 *zxdgOutput, const char *description)
{
}

const zxdg_output_v1_listener xdgOutputListener =
{
	.logical_position = zxdgOutputHandleLogicalPosition,
	.logical_size = zxdgOutputHandleLogicalSize,
	.done = zxdgOutputHandleDone,
	.name = zxdgOutputHandleName,
	.description = zxdgOutputHandleDescription
};

void incrementLetterCombination(std::string& letterCombination)
{
	if(letterCombination.size() == 0)
	{
		return;
	}

	for(size_t x = letterCombination.size() - 1; x >= 0; --x)
	{
		if(letterCombination[x] + 1 <= 'Z')
		{
			++letterCombination[x];
			return;
		} else
		{
			letterCombination[x] = 'A';
		}
	}
}

void nextScalarColor(cv::Scalar& scalar, int incrementBy = 1)
{
	//taken from https://github.com/AmberGraph41161/ncursesMinesweeper
	if(scalar[2] == 255 && scalar[1] < 255 && scalar[0] == 0)
	{
		scalar[1] += incrementBy;
	} else if(scalar[1] == 255 && scalar[2] > 0)
	{
		scalar[2] -= incrementBy;
	} else if(scalar[1] == 255 && scalar[0] < 255)
	{
		scalar[0] += incrementBy;
	} else if(scalar[0] == 255 && scalar[1] > 0)
	{
		scalar[1] -= incrementBy;
	} else if(scalar[0] == 255 && scalar[2] < 255)
	{
		scalar[2] += incrementBy;
	} else if(scalar[2] == 255 && scalar[0] > 0)
	{
		scalar[0] -= incrementBy;
	}

	if(scalar[0] > 255)
	{
		scalar[0] = 255;
	} else if(scalar[0] < 0)
	{
		scalar[0] = 0;
	}
	if(scalar[1] > 255)
	{
		scalar[1] = 255;
	} else if(scalar[1] < 0)
	{
		scalar[1] = 0;
	}
	if(scalar[2] > 255)
	{
		scalar[2] = 255;
	} else if(scalar[2] < 0)
	{
		scalar[2] = 0;
	}
}

void drawGridFrame(WaylandState* waylandState)
{
	if(waylandState->virtualPointerHasJumped)
	{
		waylandState->virtualPointerHasJumped = false;
		waylandState->letterCombinationsWidth = 1;
		waylandState->letterCombinationsToRects.clear();
		waylandState->letterCombinationsToClickPoints.clear();
		std::memset(waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize, 0, waylandState->sharedMemoryFrameSize);
	} else
	{
		return;
	}

	cv::Mat drawMat
	(
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryWidth,
		CV_8UC4,
		waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize,
		waylandState->sharedMemoryStride
	);

	uint32_t gridBoxHeight = waylandState->drawArea.height / waylandState->drawResizeDivisorFromNthDraw[waylandState->drawAreaResizeCount];
	uint32_t gridBoxWidth = waylandState->drawArea.width / waylandState->drawResizeDivisorFromNthDraw[waylandState->drawAreaResizeCount];
	++waylandState->drawAreaResizeCount;
	std::vector<cv::Rect> boundingRects;
	for
	(
		uint32_t y = waylandState->drawArea.y;
		y < waylandState->drawArea.y + waylandState->drawArea.height;
		y += gridBoxHeight
	)
	{
		for
		(
			int x = waylandState->drawArea.x;
			x < waylandState->drawArea.x + waylandState->drawArea.width;
			x += gridBoxWidth
		)
		{
			boundingRects.emplace_back(cv::Rect(x, y, gridBoxWidth, gridBoxHeight));
		}
	}

	int nLetterCombinations = 26;
	while(nLetterCombinations < boundingRects.size())
	{
		nLetterCombinations *= nLetterCombinations;
		++waylandState->letterCombinationsWidth;
	}
	std::string letterCombinationString(waylandState->letterCombinationsWidth, 'A');

	double fontScale = 0.5;
	for(size_t x = 0; x < boundingRects.size(); ++x)
	{
		cv::rectangle
		(
			drawMat,
			boundingRects[x].tl(),
			boundingRects[x].br(),
			waylandState->openCVRectangleScalar,
			waylandState->openCVRectangleThickness
		);

		cv::Point boundingRectBottomLeft
		(
			boundingRects[x].tl().x + waylandState->openCVRectangleThickness,
			boundingRects[x].br().y - waylandState->openCVRectangleThickness
		);

		cv::putText
		(
			drawMat,
			letterCombinationString,
			boundingRectBottomLeft,
			cv::FONT_HERSHEY_SIMPLEX,
			fontScale,
			waylandState->openCVRectangleScalar
		);

		waylandState->letterCombinationsToRects[letterCombinationString] = boundingRects[x];
		waylandState->letterCombinationsToClickPoints[letterCombinationString] = cv::Point
		(
			boundingRects[x].tl().x + ((boundingRects[x].br().x - boundingRects[x].tl().x) / 2),
			boundingRects[x].tl().y + ((boundingRects[x].br().y - boundingRects[x].tl().y) / 2)
		);

		incrementLetterCombination(letterCombinationString);
		nextScalarColor(waylandState->openCVRectangleScalar, 50);
	}

	drawMat.copyTo(waylandState->openCVInitialFrame);
}

void drawButtonDetectionFrame(WaylandState* waylandState)
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
	cv::Mat screenshotMat
	(
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryWidth,
		CV_8UC4,
		waylandState->sharedMemoryPoolData,
		waylandState->sharedMemoryStride
	);
	cv::Mat drawMat
	(
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryWidth,
		CV_8UC4,
		waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize,
		waylandState->sharedMemoryStride
	);
	cv::Mat grayMat(screenshotMat);
	cv::cvtColor(grayMat, grayMat, cv::COLOR_BGR2GRAY);
	cv::Canny
	(
		grayMat,
		grayMat,
		waylandState->openCVCannyLowerThreshold,
		waylandState->openCVCannyUpperThreshold,
		waylandState->openCVCannyApetureSize,
		waylandState->openCVCannyL2Gradient
	);

	std::vector<std::vector<cv::Point>> contours;
	cv::findContours(grayMat, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

	std::vector<cv::Rect> boundingRects;
	for(size_t x = 0; x < contours.size(); ++x)
	{
		cv::Rect boundingRect = cv::boundingRect(contours[x]);
		boundingRects.push_back(boundingRect);
	}

	for(size_t x = 0; x < boundingRects.size(); ++x)
	{
	}

	int nLetterCombinations = 26;
	while(nLetterCombinations < boundingRects.size())
	{
		nLetterCombinations *= nLetterCombinations;
		++waylandState->letterCombinationsWidth;
	}
	std::string letterCombinationString(waylandState->letterCombinationsWidth, 'A');

	double fontScale = 0.5;
	for(size_t x = 0; x < boundingRects.size(); ++x)
	{
		cv::rectangle
		(
			drawMat,
			boundingRects[x].tl(),
			boundingRects[x].br(),
			waylandState->openCVRectangleScalar,
			waylandState->openCVRectangleThickness
		);

		cv::Point boundingRectBottomLeft
		(
			boundingRects[x].tl().x + waylandState->openCVRectangleThickness,
			boundingRects[x].br().y - waylandState->openCVRectangleThickness
		);

		cv::putText
		(
			drawMat,
			letterCombinationString,
			boundingRectBottomLeft,
			cv::FONT_HERSHEY_SIMPLEX,
			fontScale,
			waylandState->openCVRectangleScalar
		);

		waylandState->letterCombinationsToClickPoints[letterCombinationString] = cv::Point
		(
			boundingRects[x].tl().x + ((boundingRects[x].br().x - boundingRects[x].tl().x) / 2),
			boundingRects[x].tl().y + ((boundingRects[x].br().y - boundingRects[x].tl().y) / 2)
		);

		incrementLetterCombination(letterCombinationString);
		nextScalarColor(waylandState->openCVRectangleScalar);
	}

	drawMat.copyTo(waylandState->openCVInitialFrame);
}

void drawFrame(WaylandState* waylandState)
{
	cv::Mat drawMat
	(
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryWidth,
		CV_8UC4,
		waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize,
		waylandState->sharedMemoryStride
	);
	waylandState->openCVInitialFrame.copyTo(drawMat);

	cv::putText
	(
		drawMat,
		waylandState->userKeyboardInput,
		cv::Point
		(
			0, //waylandState->outputsDimensions[waylandState->selectedOutputIndex].width / 2,
			waylandState->outputsDimensions[waylandState->selectedOutputIndex].height
		),
		cv::FONT_HERSHEY_SIMPLEX,
		10,
		waylandState->openCVRectangleScalar,
		30
	);
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
	//waylandState->sharedMemoryWidth = waylandState->outputsDimensions[waylandState->selectedOutputIndex].width;
	//waylandState->sharedMemoryHeight = waylandState->outputsDimensions[waylandState->selectedOutputIndex].height;
	//waylandState->sharedMemoryStride = waylandState->outputsDimensions[waylandState->selectedOutputIndex].height * 4;

	waylandState->sharedMemoryFrameSize = waylandState->sharedMemoryHeight * waylandState->sharedMemoryStride;
	waylandState->sharedMemoryPoolSize = waylandState->sharedMemoryFrameSize * waylandState->sharedMemoryNBuffers;

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
	int offset = waylandState->sharedMemoryFrameSize * index;
	waylandState->screenshotBuffer = wl_shm_pool_create_buffer
	(
		waylandState->sharedMemoryPool,
		offset,
		waylandState->sharedMemoryWidth,
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryStride,
		WL_SHM_FORMAT_XRGB8888
	);
	wl_buffer_add_listener(waylandState->screenshotBuffer, &waylandBufferListener, waylandState);
	
	index = 1;
	offset = waylandState->sharedMemoryFrameSize * index;
	waylandState->buffer = wl_shm_pool_create_buffer
	(
		waylandState->sharedMemoryPool,
		offset,
		waylandState->sharedMemoryWidth,
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryStride,
		WL_SHM_FORMAT_ARGB8888
	);
	wl_buffer_add_listener(waylandState->buffer, &waylandBufferListener, waylandState);

	//drawInitialFrameButtonDetection(waylandState);
	waylandState->drawArea = cv::Rect
	(
		0,
		0,
		waylandState->outputsDimensions[waylandState->selectedOutputIndex].width,
		waylandState->outputsDimensions[waylandState->selectedOutputIndex].height
	);
	drawGridFrame(waylandState);
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

	drawGridFrame(waylandState);
	drawFrame(waylandState);
	wl_surface_attach(waylandState->surface, waylandState->buffer, 0, 0);
	wl_surface_damage(waylandState->surface, 0, 0, waylandState->sharedMemoryWidth, waylandState->sharedMemoryHeight);
	wl_surface_commit(waylandState->surface);
}

void virtualPointerMoveAbsolute(WaylandState& waylandState, uint32_t x, uint32_t y)
{
	zwlr_virtual_pointer_v1_motion_absolute
	(
		waylandState.virtualPointer,
		waylandState.virtualPointerTime,
		x,
		y,
		waylandState.virtualPointerXExtent,
		waylandState.virtualPointerYExtent
	);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);
}

void virtualPointerLeftDown(WaylandState& waylandState)
{
	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);
}

void virtualPointerLeftUp(WaylandState& waylandState)
{
	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);
}
void virtualPointerLeftClick(WaylandState& waylandState)
{
	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_LEFT, WL_POINTER_BUTTON_STATE_PRESSED);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);

	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_LEFT, WL_POINTER_BUTTON_STATE_RELEASED);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);
}

void virtualPointerRightDown(WaylandState& waylandState)
{
	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_RIGHT, WL_POINTER_BUTTON_STATE_PRESSED);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);
}

void virtualPointerRightUp(WaylandState& waylandState)
{
	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_RIGHT, WL_POINTER_BUTTON_STATE_RELEASED);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);
}

void virtualPointerRightClick(WaylandState& waylandState)
{
	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_RIGHT, WL_POINTER_BUTTON_STATE_PRESSED);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);

	zwlr_virtual_pointer_v1_button(waylandState.virtualPointer, waylandState.virtualPointerTime, BTN_RIGHT, WL_POINTER_BUTTON_STATE_RELEASED);
	++waylandState.virtualPointerTime;
	zwlr_virtual_pointer_v1_frame(waylandState.virtualPointer);
	wl_display_flush(waylandState.display);
}

int libinputOpenRestricted(const char* path, int flags, void* data)
{
	int fileDescriptor = open(path, flags);
	if(fileDescriptor < 0)
	{
		std::cerr << "failed to open fileDescriptor" << std::endl;
	}

	return fileDescriptor;
}

void libinputCloseRestricted(int fileDescriptor, void* data)
{
	close(fileDescriptor);
}

const libinput_interface libinputInterface =
{
	.open_restricted = libinputOpenRestricted,
	.close_restricted = libinputCloseRestricted
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
	} else if(strcmp(interface, zxdg_output_manager_v1_interface.name) == 0)
	{
		waylandState->xdgOutputManager = static_cast<zxdg_output_manager_v1*>(wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, version));
	} else if(strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
	{
		waylandState->layerShell = static_cast<zwlr_layer_shell_v1*>(wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, version));
	} else if(strcmp(interface, wl_seat_interface.name) == 0)
	{
		waylandState->seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, version));
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

std::unordered_map<uint32_t, char> inputEventCodeToAscii =
{
	{ KEY_A, 'A' },
	{ KEY_B, 'B' },
	{ KEY_C, 'C' },
	{ KEY_D, 'D' },
	{ KEY_E, 'E' },
	{ KEY_F, 'F' },
	{ KEY_G, 'G' },
	{ KEY_H, 'H' },
	{ KEY_I, 'I' },
	{ KEY_J, 'J' },
	{ KEY_K, 'K' },
	{ KEY_L, 'L' },
	{ KEY_M, 'M' },
	{ KEY_N, 'N' },
	{ KEY_O, 'O' },
	{ KEY_P, 'P' },
	{ KEY_Q, 'Q' },
	{ KEY_R, 'R' },
	{ KEY_S, 'S' },
	{ KEY_T, 'T' },
	{ KEY_U, 'U' },
	{ KEY_V, 'V' },
	{ KEY_W, 'W' },
	{ KEY_X, 'X' },
	{ KEY_Y, 'Y' },
	{ KEY_Z, 'Z' },
};

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

	std::vector<zxdg_output_v1*> zxdgOutputs;
	for(size_t x = 0; x < waylandState.outputs.size(); ++x)
	{
		zxdgOutputs.emplace_back(zxdg_output_manager_v1_get_xdg_output(waylandState.xdgOutputManager, waylandState.outputs[x]));
		zxdg_output_v1_add_listener(zxdgOutputs.back(), &xdgOutputListener, &waylandState);
	}
	wl_display_roundtrip(waylandState.display); //process globals and listeners, send zxdg_output_v1_add_listener
	wl_display_roundtrip(waylandState.display); //process dones

	const std::string targetMonitorName = "eDP-1";
	//const std::string targetMonitorName = "HDMI-A-1";
	for(size_t x = 0; x < waylandState.outputsNames.size(); ++x)
	{
		if(waylandState.outputsNames[x] == targetMonitorName)
		{
			waylandState.selectedOutputIndex = x;
			waylandState.virtualPointerXOrigin = waylandState.outputsPositions[x].x;
			waylandState.virtualPointerYOrigin = waylandState.outputsPositions[x].y;
			break;
		}
	}

	for(size_t x = 0; x < waylandState.outputsDimensions.size(); x++)
	{
		if(waylandState.outputsPositions[x].x + waylandState.outputsDimensions[x].width > waylandState.virtualPointerXExtent)
		{
			 waylandState.virtualPointerXExtent = waylandState.outputsPositions[x].x + waylandState.outputsDimensions[x].width;
		}
		if(waylandState.outputsPositions[x].y + waylandState.outputsDimensions[x].height > waylandState.virtualPointerYExtent)
		{
			 waylandState.virtualPointerYExtent = waylandState.outputsPositions[x].y + waylandState.outputsDimensions[x].height;
		}
	}

	waylandState.virtualPointer = zwlr_virtual_pointer_manager_v1_create_virtual_pointer(waylandState.virtualPointerManager, waylandState.seat);
	if(waylandState.virtualPointer == nullptr)
	{
		std::cerr << "failed to create virtual pointer!" << std::endl;
		exit(EXIT_FAILURE);
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
	zwlr_layer_surface_v1_set_keyboard_interactivity(waylandState.layerSurface, false);
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

	udev* udevContext = udev_new();
	if(!udevContext)
	{
		std::cerr << "failed to create udevContext" << std::endl;
		exit(EXIT_FAILURE);
	}

	libinput* libinputContext = libinput_udev_create_context(&libinputInterface, NULL, udevContext);
	if(!libinputContext)
	{
		std::cerr << "failed to create libinputContext" << std::endl;
		exit(EXIT_FAILURE);
	}

	if(libinput_udev_assign_seat(libinputContext, "seat0") != 0)
	{
		std::cerr << "failed to assign seat0" << std::endl;
		exit(EXIT_FAILURE);
	}

	while
	(
		!waylandState.layerSurfaceShouldClose &&
		!waylandState.shouldClickAndExit &&
		wl_display_dispatch(waylandState.display) != -1
	)
	{
		libinput_dispatch(libinputContext);
		libinput_event* libinputEvent;
		while((libinputEvent = libinput_get_event(libinputContext)))
		{
			libinput_event_type libinputEventType = libinput_event_get_type(libinputEvent);
			if(libinputEventType == LIBINPUT_EVENT_KEYBOARD_KEY)
			{
				libinput_event_keyboard* libinputEventKeyboard = libinput_event_get_keyboard_event(libinputEvent); //owned by libinputEvent, thus freed also when libinputEvent freed.
				uint32_t key = libinput_event_keyboard_get_key(libinputEventKeyboard);
				libinput_key_state libinputKeyState = libinput_event_keyboard_get_key_state(libinputEventKeyboard);

				if(libinputKeyState == LIBINPUT_KEY_STATE_PRESSED)
				{
					switch(key)
					{
						case KEY_ESC:
							if(waylandState.userKeyboardInput.size() > 0)
							{
								waylandState.userKeyboardInput.clear();
							} else
							{
								waylandState.layerSurfaceShouldClose = true;
							}
							break;

						case KEY_LEFTSHIFT:
						{
							virtualPointerLeftDown(waylandState);
							break;
						}
						case KEY_RIGHTSHIFT:
						{
							virtualPointerRightDown(waylandState);
							break;
						}

						default:
						{
							waylandState.userKeyboardInput += inputEventCodeToAscii[key];
							if
							(
								waylandState.userKeyboardInput.size() > waylandState.letterCombinationsWidth ||
								(
									waylandState.userKeyboardInput.size() == waylandState.letterCombinationsWidth &&
									waylandState.letterCombinationsToClickPoints.count(waylandState.userKeyboardInput) == 0
								) ||
								inputEventCodeToAscii[key] < 'A' ||
								inputEventCodeToAscii[key] > 'Z'
							)
							{
								waylandState.userKeyboardInput.clear();
								break;
							}
							if(waylandState.letterCombinationsToClickPoints.count(waylandState.userKeyboardInput))
							{
								virtualPointerMoveAbsolute
								(
									waylandState,
									waylandState.virtualPointerXOrigin + waylandState.letterCombinationsToClickPoints.at(waylandState.userKeyboardInput).x,
									waylandState.virtualPointerYOrigin + waylandState.letterCombinationsToClickPoints.at(waylandState.userKeyboardInput).y
								);

								if(waylandState.drawAreaResizeCount >= waylandState.drawResizeDivisorFromNthDraw.size())
								{
									waylandState.shouldClickAndExit = true;
									virtualPointerLeftClick(waylandState);
								} else
								{
									waylandState.drawArea = waylandState.letterCombinationsToRects.at(waylandState.userKeyboardInput);
									waylandState.userKeyboardInput.clear();
									waylandState.virtualPointerHasJumped = true;
								}
							}
							break;
						}
					}
				} else if(libinputKeyState == LIBINPUT_KEY_STATE_RELEASED)
				{
					switch(key)
					{
						case KEY_LEFTSHIFT:
						{
							virtualPointerLeftUp(waylandState);
							break;
						}
						case KEY_RIGHTSHIFT:
						{
							virtualPointerRightUp(waylandState);
							break;
						}
					}
				}
			}
		}
		libinput_event_destroy(libinputEvent);
	}
	libinput_unref(libinputContext);
	udev_unref(udevContext);

	//cleanup
	if(waylandState.registry)
	{
		wl_registry_destroy(waylandState.registry);
	}
	if(waylandState.compositor)
	{
		wl_compositor_destroy(waylandState.compositor);
	}
	if(!waylandState.outputs.empty())
	{
		for(size_t x = 0; x < waylandState.outputs.size(); ++x)
		{
			wl_output_destroy(waylandState.outputs[x]);
		}
	}
	if(!waylandState.xdgOutputManager)
	{
		zxdg_output_manager_v1_destroy(waylandState.xdgOutputManager);
	}
	for(size_t x = 0; x < zxdgOutputs.size(); x++)
	{
		zxdg_output_v1_destroy(zxdgOutputs[x]);
	}
	if(waylandState.sharedMemory)
	{
		wl_shm_destroy(waylandState.sharedMemory);
	}
	if(waylandState.surface)
	{
		wl_surface_destroy(waylandState.surface);
	}
	if(waylandState.screenshotBuffer)
	{
		wl_buffer_destroy(waylandState.screenshotBuffer);
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
	if(waylandState.seat)
	{
		wl_seat_destroy(waylandState.seat);
	}
	if(waylandState.region)
	{
		wl_region_destroy(waylandState.region);
	}
	if(waylandState.screenCopyManager)
	{
		zwlr_screencopy_manager_v1_destroy(waylandState.screenCopyManager);
	}
	if(waylandState.screenCopyFrame)
	{
		zwlr_screencopy_frame_v1_destroy(waylandState.screenCopyFrame);
	}
	if(waylandState.virtualPointerManager)
	{
		zwlr_virtual_pointer_manager_v1_destroy(waylandState.virtualPointerManager);
	}
	if(waylandState.virtualPointer)
	{
		zwlr_virtual_pointer_v1_destroy(waylandState.virtualPointer);
	}

	wl_shm_pool_destroy(waylandState.sharedMemoryPool);
	close(waylandState.sharedMemoryPoolFileDescriptor);
	munmap(waylandState.sharedMemoryPoolData, waylandState.sharedMemoryPoolSize);

	if(waylandState.display)
	{
		//must be called last!
		wl_display_disconnect(waylandState.display);
	}

	return 0;
}
