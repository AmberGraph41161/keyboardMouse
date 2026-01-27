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
#include <fstream>
#include <filesystem>
#include <sstream>

#include <opencv4/opencv2/core/types.hpp>
#include <opencv4/opencv2/core.hpp>
#include <opencv4/opencv2/imgproc.hpp>

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1.h"
#include "wlr-screencopy-unstable-v1.h"
#include "xdg-output-unstable-v1.h"

#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/input.h>
#include <linux/input-event-codes.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "mouse.hpp"
#include "absolutePointer.hpp"

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
	char name[] = "/keyboardMouse_shm-XXXXXX";
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
	Dimension() : width(0), height(0) {};
	Dimension(uint32_t width, uint32_t height) : width(width), height(height) {}
	uint32_t width;
	uint32_t height;
};

struct Position
{
	Position() : x(0), y(0) {};
	Position(uint32_t x, uint32_t y) : x(x), y(y) {}
	uint32_t x;
	uint32_t y;
};

struct ClickTarget
{
	cv::Rect rectangle;
	cv::Scalar scalar;
	cv::Point clickPoint;
	std::string letterCombination;
};

enum class DisplayMode { grid, buttonDetection, mouse };
enum class MouseAction { move, leftClick, rightClick, middleClick, leftDrag, rightDrag, middleDrag };
std::unordered_map<std::string, MouseAction> stringToAction =
{
	{ "move", MouseAction::move },
	{ "leftClick", MouseAction::leftClick },
	{ "rightClick", MouseAction::rightClick },
	{ "middleClick", MouseAction::middleClick },
	{ "leftDrag", MouseAction::leftDrag },
	{ "rightDrag", MouseAction::rightDrag },
	{ "middleDrag", MouseAction::middleDrag }
};

struct WaylandState
{
	DisplayMode displayMode = DisplayMode::grid;
	MouseAction mouseAction = MouseAction::leftClick;
	bool mouseActionCommitted = false;

	wl_display* display = nullptr;
	wl_registry* registry = nullptr;
	wl_compositor* compositor = nullptr;

	std::vector<wl_output*> outputs;
	std::vector<std::string> outputsNames;
	std::vector<Dimension> outputsDimensionsScaled;
	std::vector<Dimension> outputsDimensionsNotScaled;

	wl_output* selectedOutput;
	std::string selectedOutputName;
	Dimension selectedOutputDimensionScaled;
	Dimension selectedOutputDimensionNotScaled;
	double selectedOutputScale;

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
	bool layerSurfaceShouldClose = false;

	wl_seat* seat = nullptr;
	wl_region* region = nullptr;

	zwlr_screencopy_manager_v1* screenCopyManager = nullptr;
	zwlr_screencopy_frame_v1* screenCopyFrame = nullptr;
	uint32_t screenCopyFlags;
	bool screenCopyBufferWasHandled = false;

	uint32_t mouseXExtent = 0;
	uint32_t mouseYExtent = 0;
	uint32_t mouseXOrigin = 0;
	uint32_t mouseYOrigin = 0;
	bool shouldActionAndExit = false;
	unsigned int mouseRelativeJitterSleepMilliseconds = 5;

	unsigned int openCVFontThickness = 2;
	unsigned int openCVFontShadowThickness = 3;
	unsigned int openCVFontUserInputThickness = 30;
	unsigned int openCVFontUserInputShadowThickness = 35;
	unsigned int openCVFontDrawTextThickness = 10;
	unsigned int openCVFontDrawTextShadowThickness = 15;
	double openCVFontScale = 0.5;
	double openCVFontUserInputScale = 10;
	double openCVFontDrawTextScale = 3;
	cv::Scalar openCVRectangleScalar{0, 0, 255, 255}; //BGRA
	cv::Scalar openCVTextScalar{0, 0, 0, 255};
	unsigned int openCVRectangleThickness = 2;
	/* https://docs.opencv.org/3.4/da/d5c/tutorial_canny_detector.html
	Hysteresis: The final step. Canny does use two thresholds (upper and lower):
	If a pixel gradient is higher than the upper threshold, the pixel is accepted as an edge
	If a pixel gradient value is below the lower threshold, then it is rejected.
	If the pixel gradient is between the two thresholds, then it will be accepted only if it is connected to a pixel that is above the upper threshold.
	Canny recommended a upper:lower ratio between 2:1 and 3:1.
	*/
	unsigned int openCVCannyLowerThreshold = 10;
	unsigned int openCVCannyUpperThreshold = 30;
	unsigned int openCVCannyApetureSize = 3; //only allowed to be 3, 5, or 7
	bool openCVCannyL2Gradient = false;
	unsigned int openCVDilateWidth = 5;
	unsigned int openCVDilateHeight = 5;

	int letterCombinationsWidth = 1;
	std::string userKeyboardInput = "";
	std::unordered_map<std::string, std::vector<ClickTarget>> layeredLetterCombinationsToClickTargets;
	std::unordered_map<std::string, ClickTarget> letterCombinationsToClickTargets;
	cv::Rect drawArea;
	int drawAreaResizeCount = 0;
	std::vector<unsigned int> drawResizeDivisorFromNthDraw = { 8, 8, 2 };
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
	fprintf(stderr, "failed to copy output %s\n", waylandState->selectedOutputName.c_str());
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
	WaylandState* waylandState = static_cast<WaylandState*>(data);
	waylandState->outputsDimensionsNotScaled.emplace_back(Dimension(width, height));
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
	waylandState->outputsDimensionsScaled.emplace_back(Dimension(width, height));
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
	//don't need
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

void drawText(WaylandState* waylandState, std::string text)
{
	cv::Mat drawMat
	(
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryWidth,
		CV_8UC4,
		waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize,
		waylandState->sharedMemoryStride
	);

	cv::putText
	(
		drawMat,
		text,
		cv::Point
		(
			0,
			waylandState->selectedOutputDimensionScaled.height / 2
		),
		cv::FONT_HERSHEY_SIMPLEX,
		waylandState->openCVFontDrawTextScale,
		waylandState->openCVRectangleScalar,
		waylandState->openCVFontDrawTextShadowThickness
	);
	cv::putText
	(
		drawMat,
		text,
		cv::Point
		(
			0,
			waylandState->selectedOutputDimensionScaled.height / 2
		),
		cv::FONT_HERSHEY_SIMPLEX,
		waylandState->openCVFontDrawTextScale,
		waylandState->openCVTextScalar,
		waylandState->openCVFontDrawTextThickness
	);
}

void resetDrawFrameVariables(WaylandState& waylandState, bool resetGridDrawAreaResizeCount = true)
{
	std::memset(waylandState.sharedMemoryPoolData + waylandState.sharedMemoryFrameSize, 0, waylandState.sharedMemoryFrameSize);
	waylandState.userKeyboardInput.clear();
	waylandState.layeredLetterCombinationsToClickTargets.clear();
	waylandState.letterCombinationsWidth = 1;
	waylandState.letterCombinationsToClickTargets.clear();
	if(resetGridDrawAreaResizeCount)
	{
		waylandState.drawAreaResizeCount = 0;
		waylandState.drawArea = cv::Rect
		(
			0,
			0,
			waylandState.selectedOutputDimensionScaled.width,
			waylandState.selectedOutputDimensionScaled.height
		);
	}
}

void drawNthInitialGridFrame(WaylandState* waylandState, bool increment = false)
{
	if(increment && waylandState->drawAreaResizeCount + 1 < waylandState->drawResizeDivisorFromNthDraw.size())
	{
		++waylandState->drawAreaResizeCount;
	}

	std::memset(waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize, 0, waylandState->sharedMemoryFrameSize);
	cv::Mat drawMat
	(
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryWidth,
		CV_8UC4,
		waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize,
		waylandState->sharedMemoryStride
	);

	unsigned int drawResizeDivisorFromNthDraw = waylandState->drawResizeDivisorFromNthDraw.at(waylandState->drawAreaResizeCount);
	if(drawResizeDivisorFromNthDraw == 0)
	{
		drawResizeDivisorFromNthDraw = 1;
	}

	uint32_t gridBoxHeight = waylandState->drawArea.height / drawResizeDivisorFromNthDraw;
	uint32_t gridBoxWidth = waylandState->drawArea.width / drawResizeDivisorFromNthDraw;
	if(gridBoxHeight == 0)
	{
		gridBoxHeight = 1;
	}
	if(gridBoxWidth == 0)
	{
		gridBoxWidth = 1;
	}

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
	waylandState->letterCombinationsWidth = 1;
	while(nLetterCombinations < boundingRects.size())
	{
		nLetterCombinations *= nLetterCombinations;
		++waylandState->letterCombinationsWidth;
	}

	std::string letterCombination(waylandState->letterCombinationsWidth, 'A');

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
			letterCombination,
			boundingRectBottomLeft,
			cv::FONT_HERSHEY_SIMPLEX,
			waylandState->openCVFontScale,
			waylandState->openCVRectangleScalar,
			waylandState->openCVFontShadowThickness
		);
		cv::putText
		(
			drawMat,
			letterCombination,
			boundingRectBottomLeft,
			cv::FONT_HERSHEY_SIMPLEX,
			waylandState->openCVFontScale,
			waylandState->openCVTextScalar,
			waylandState->openCVFontThickness
		);

		ClickTarget clickTarget
		{
			.rectangle = boundingRects[x],
			.scalar = waylandState->openCVRectangleScalar,
			.clickPoint = cv::Point
			(
				boundingRects[x].tl().x + ((boundingRects[x].br().x - boundingRects[x].tl().x) / 2),
				boundingRects[x].tl().y + ((boundingRects[x].br().y - boundingRects[x].tl().y) / 2)
			),
			.letterCombination = letterCombination
		};

		waylandState->letterCombinationsToClickTargets[letterCombination] = clickTarget;
		for(size_t y = 1; y < letterCombination.size(); ++y)
		{
			waylandState->layeredLetterCombinationsToClickTargets[letterCombination.substr(0, y)].emplace_back(clickTarget);
		}


		incrementLetterCombination(letterCombination);
		nextScalarColor(waylandState->openCVRectangleScalar, 50);
	}
}

void drawInitialButtonDetectionFrame(WaylandState* waylandState)
{
	waylandState->screenCopyBufferWasHandled = false;

	waylandState->screenCopyFrame = zwlr_screencopy_manager_v1_capture_output
	(
		waylandState->screenCopyManager,
		false, //overlay cursor
		waylandState->selectedOutput
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
	cv::dilate(grayMat, grayMat, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(waylandState->openCVDilateWidth, waylandState->openCVDilateHeight)));
	cv::findContours(grayMat, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

	std::vector<cv::Rect> boundingRects;
	for(size_t x = 0; x < contours.size(); ++x)
	{
		cv::Rect boundingRect = cv::boundingRect(contours[x]);
		boundingRect.x /= waylandState->selectedOutputScale;
		boundingRect.y /= waylandState->selectedOutputScale;
		boundingRects.emplace_back(boundingRect);
	}

	int nLetterCombinations = 26;
	while(nLetterCombinations < boundingRects.size())
	{
		nLetterCombinations *= nLetterCombinations;
		++waylandState->letterCombinationsWidth;
	}
	std::string letterCombination(waylandState->letterCombinationsWidth, 'A');

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
			letterCombination,
			boundingRectBottomLeft,
			cv::FONT_HERSHEY_SIMPLEX,
			waylandState->openCVFontScale,
			waylandState->openCVRectangleScalar,
			waylandState->openCVFontShadowThickness
		);
		cv::putText
		(
			drawMat,
			letterCombination,
			boundingRectBottomLeft,
			cv::FONT_HERSHEY_SIMPLEX,
			waylandState->openCVFontScale,
			waylandState->openCVTextScalar,
			waylandState->openCVFontThickness
		);

		ClickTarget clickTarget
		{
			.rectangle = boundingRects[x],
			.scalar = waylandState->openCVRectangleScalar,
			.clickPoint = cv::Point
			(
				boundingRects[x].tl().x + ((boundingRects[x].br().x - boundingRects[x].tl().x) / 2),
				boundingRects[x].tl().y + ((boundingRects[x].br().y - boundingRects[x].tl().y) / 2)
			),
			.letterCombination = letterCombination
		};

		waylandState->letterCombinationsToClickTargets[letterCombination] = clickTarget;
		for(size_t y = 1; y < letterCombination.size(); ++y)
		{
			//omg this looks cryptic as heck
			waylandState->layeredLetterCombinationsToClickTargets[letterCombination.substr(0, y)].emplace_back(clickTarget);
		}

		incrementLetterCombination(letterCombination);
		nextScalarColor(waylandState->openCVRectangleScalar);
	}
}

void drawLayeredLetterCombinationsToClickTargetsFrame(WaylandState* waylandState)
{
	std::memset(waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize, 0, waylandState->sharedMemoryFrameSize);
	cv::Mat drawMat
	(
		waylandState->sharedMemoryHeight,
		waylandState->sharedMemoryWidth,
		CV_8UC4,
		waylandState->sharedMemoryPoolData + waylandState->sharedMemoryFrameSize,
		waylandState->sharedMemoryStride
	);

	std::vector<ClickTarget> clickTargets;
	if(waylandState->userKeyboardInput.size() > 0)
	{
		clickTargets = waylandState->layeredLetterCombinationsToClickTargets[waylandState->userKeyboardInput];
	} else
	{
		for
		(
			std::unordered_map<std::string, ClickTarget>::iterator it = waylandState->letterCombinationsToClickTargets.begin();
			it != waylandState->letterCombinationsToClickTargets.end();
			++it
		)
		{
			clickTargets.emplace_back((*it).second);
		}
	}

	for(size_t x = 0; x < clickTargets.size(); ++x)
	{
		cv::rectangle
		(
			drawMat,
			clickTargets[x].rectangle.tl(),
			clickTargets[x].rectangle.br(),
			clickTargets[x].scalar,
			waylandState->openCVRectangleThickness
		);

		cv::Point boundingRectBottomLeft
		(
			clickTargets[x].rectangle.tl().x + waylandState->openCVRectangleThickness,
			clickTargets[x].rectangle.br().y - waylandState->openCVRectangleThickness
		);

		cv::putText
		(
			drawMat,
			clickTargets[x].letterCombination,
			boundingRectBottomLeft,
			cv::FONT_HERSHEY_SIMPLEX,
			waylandState->openCVFontScale,
			clickTargets[x].scalar,
			waylandState->openCVFontShadowThickness
		);
		cv::putText
		(
			drawMat,
			clickTargets[x].letterCombination,
			boundingRectBottomLeft,
			cv::FONT_HERSHEY_SIMPLEX,
			waylandState->openCVFontScale,
			waylandState->openCVTextScalar,
			waylandState->openCVFontThickness
		);
	}
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

	cv::putText
	(
		drawMat,
		waylandState->userKeyboardInput,
		cv::Point
		(
			0,
			waylandState->selectedOutputDimensionScaled.height
		),
		cv::FONT_HERSHEY_SIMPLEX,
		waylandState->openCVFontUserInputScale,
		waylandState->openCVRectangleScalar,
		waylandState->openCVFontUserInputShadowThickness
	);
	cv::putText
	(
		drawMat,
		waylandState->userKeyboardInput,
		cv::Point
		(
			0,
			waylandState->selectedOutputDimensionScaled.height
		),
		cv::FONT_HERSHEY_SIMPLEX,
		waylandState->openCVFontUserInputScale,
		waylandState->openCVTextScalar,
		waylandState->openCVFontUserInputThickness
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

	waylandState->sharedMemoryWidth = waylandState->selectedOutputDimensionNotScaled.width;
	waylandState->sharedMemoryHeight = waylandState->selectedOutputDimensionNotScaled.height;
	waylandState->sharedMemoryStride = waylandState->selectedOutputDimensionNotScaled.width * 4;

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

	if(waylandState->displayMode == DisplayMode::grid)
	{
		waylandState->drawArea = cv::Rect
		(
			0,
			0,
			waylandState->selectedOutputDimensionScaled.width,
			waylandState->selectedOutputDimensionScaled.height
		);
		bool increment = false;
		drawNthInitialGridFrame(waylandState, increment);
	} else if(waylandState->displayMode == DisplayMode::buttonDetection)
	{
		drawInitialButtonDetectionFrame(waylandState);
	}
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

	drawLayeredLetterCombinationsToClickTargetsFrame(waylandState);
	drawFrame(waylandState);
	wl_surface_attach(waylandState->surface, waylandState->buffer, 0, 0);
	wl_surface_damage(waylandState->surface, 0, 0, waylandState->sharedMemoryWidth, waylandState->sharedMemoryHeight);
	wl_surface_commit(waylandState->surface);
}

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
	}
}

void waylandRegistryHandleGlobalRemove(void* data, wl_registry* registry, uint32_t name)
{
	//"intentionally left blank"
}

std::vector<unsigned int> stringToUnsignedIntVector(std::string inputString)
{
	std::vector<unsigned int> unsignedIntVector;
	std::stringstream inputStringStream(inputString);
	std::string getlinestring;
	while(std::getline(inputStringStream, getlinestring, ' '))
	{
		try
		{
			unsigned int uint = std::stoul(getlinestring);
			unsignedIntVector.push_back(uint);
		} catch(...)
		{
			//noop
		}
	}

	if(unsignedIntVector.empty())
	{
		throw "bad stringToIntVector!";
	}

	return unsignedIntVector;
}

std::string unsignedIntVectorToString(std::vector<unsigned int>& inputVector)
{
	std::string returnString;
	returnString.reserve(inputVector.size() * 2);
	for(int x = 0; x < inputVector.size(); ++x)
	{
		returnString += std::to_string(inputVector.at(x));
		if(x + 1 < inputVector.size())
		{
			returnString += ' ';
		}
	}
	return returnString;
}

void setXDG_RUNTIME_DIR()
{
	/* as of Thursday, January 22, 2026, 12:13:49
	assuming the following:
		1. XDG_RUNTIME_DIR is set in /run/user/$UID by default
		2. User is in "single user mode," thus only
			the first std::filesystem::directory_entry
			from XDG_RUNTIME_DIR is taken, and is assumed
			to be the "target user"
	*/
	std::filesystem::path XDG_RUNTIME_DIR("/run/user/");
	std::filesystem::directory_iterator it(XDG_RUNTIME_DIR);
	setenv("XDG_RUNTIME_DIR", (*it).path().c_str(), true);
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
	{ KEY_GRAVE, '`' },
	{ KEY_1, '1' },
	{ KEY_2, '2' },
	{ KEY_3, '3' },
};

static Mouse mouse; //chromium doesn't register mouse on fly fast enough or something of the sort, as of Wednesday, January 21, 2026, 11:33:52
void keyboardMouse(DisplayMode displayMode, int keyboardFileDescriptor, const std::filesystem::path& configFilePath)
{
	WaylandState waylandState;
	waylandState.displayMode = displayMode;

	std::fstream configReader;
	configReader.open(configFilePath, std::fstream::in);
	if(configReader.fail())
	{
		std::cerr << "failed to read from " << configFilePath << "!" << std::endl;
		configReader.close();
	} else
	{
		std::string getlinestring;
		while(std::getline(configReader, getlinestring))
		{
			if(getlinestring.find('#') != std::string::npos)
			{
				getlinestring = getlinestring.substr(0, getlinestring.find('#'));
			}

			if(getlinestring.size() == 0)
			{
				continue;
			}

			std::array<std::pair<std::string, unsigned int&>, 12> stringIntegerValuePairs =
			{
				std::pair<std::string, unsigned int&>("openCVFontThickness", waylandState.openCVFontThickness),
				std::pair<std::string, unsigned int&>("openCVFontShadowThickness", waylandState.openCVFontShadowThickness),
				std::pair<std::string, unsigned int&>("openCVFontUserInputThickness", waylandState.openCVFontUserInputThickness),
				std::pair<std::string, unsigned int&>("openCVFontUserInputShadowThickness", waylandState.openCVFontUserInputShadowThickness),
				std::pair<std::string, unsigned int&>("openCVFontDrawTextThickness", waylandState.openCVFontDrawTextThickness),
				std::pair<std::string, unsigned int&>("openCVFontDrawTextShadowThickness", waylandState.openCVFontDrawTextShadowThickness),
				std::pair<std::string, unsigned int&>("openCVRectangleThickness", waylandState.openCVRectangleThickness),
				std::pair<std::string, unsigned int&>("openCVCannyLowerThreshold", waylandState.openCVCannyLowerThreshold),
				std::pair<std::string, unsigned int&>("openCVCannyUpperThreshold", waylandState.openCVCannyUpperThreshold),
				std::pair<std::string, unsigned int&>("mouseRelativeJitterSleepMilliseconds", waylandState.mouseRelativeJitterSleepMilliseconds),
				std::pair<std::string, unsigned int&>("openCVDilateWidth", waylandState.openCVDilateWidth),
				std::pair<std::string, unsigned int&>("openCVDilateHeight", waylandState.openCVDilateHeight)
			};

			std::array<std::pair<std::string, double&>, 3> stringDoubleValuePairs =
			{
				std::pair<std::string, double&>("openCVFontScale", waylandState.openCVFontScale),
				std::pair<std::string, double&>("openCVFontUserInputScale", waylandState.openCVFontUserInputScale),
				std::pair<std::string, double&>("openCVFontDrawTextScale", waylandState.openCVFontDrawTextScale)
			};

			std::array<std::pair<std::string, std::vector<unsigned int>&>, 1> stringUnsignedIntVectorValuePairs =
			{
				std::pair<std::string, std::vector<unsigned int>&>("drawResizeDivisorFromNthDraw", waylandState.drawResizeDivisorFromNthDraw)
			};

			bool getlineWasEvaluated = false;

			for(int x = 0; x < stringIntegerValuePairs.size(); ++x)
			{
				const std::string& currentString = stringIntegerValuePairs.at(x).first;
				if(getlinestring.find(currentString) != std::string::npos)
				{
					std::string valueAsString = getlinestring.substr(getlinestring.find(currentString) + currentString.size());
					try
					{
						stringIntegerValuePairs.at(x).second = std::stoul(valueAsString);
						if(stringIntegerValuePairs.at(x).second > 255)
						{
							stringIntegerValuePairs.at(x).second = 255;
						}
						std::cout << "set \"" << currentString << "\" = " << stringIntegerValuePairs.at(x).second << std::endl;
					} catch(...)
					{
						std::cerr << "error reading string integer value pair! expected unsigned integer but got: ";
						std::cerr << currentString << " = " << valueAsString << std::endl;
					}
					getlineWasEvaluated = true;
					break;
				}
			}
			if(getlineWasEvaluated)
			{
				continue;
			}

			for(int x = 0; x < stringDoubleValuePairs.size(); ++x)
			{
				const std::string& currentString = stringDoubleValuePairs.at(x).first;
				if(getlinestring.find(currentString) != std::string::npos)
				{
					std::string valueAsString = getlinestring.substr(getlinestring.find(currentString) + currentString.size());
					try
					{
						stringDoubleValuePairs.at(x).second = std::stod(valueAsString);
						if(stringDoubleValuePairs.at(x).second > 255)
						{
							stringDoubleValuePairs.at(x).second = 255;
						}
						std::cout << "set \"" << currentString << "\" = " << stringDoubleValuePairs.at(x).second << std::endl;
					} catch(...)
					{
						std::cerr << "error reading string double value pair! expected double but got: ";
						std::cerr << currentString << " = " << valueAsString << std::endl;
					}
					getlineWasEvaluated = true;
					break;
				}
			}
			if(getlineWasEvaluated)
			{
				continue;
			}

			for(int x = 0; x < stringUnsignedIntVectorValuePairs.size(); ++x)
			{
				const std::string& currentString = stringUnsignedIntVectorValuePairs.at(x).first;
				std::string valueAsString = getlinestring.substr(getlinestring.find(currentString) + currentString.size());
				try
				{
					std::vector<unsigned int>& unsingedIntVectorRef = stringUnsignedIntVectorValuePairs.at(x).second;
					unsingedIntVectorRef = stringToUnsignedIntVector(valueAsString);
					std::cout << "set \"" << currentString << "\" = " << unsignedIntVectorToString(unsingedIntVectorRef) << std::endl;
				} catch(...)
				{
					std::cerr << "error reading string unsinged int vector value pair! expected unsigned int vector but got: ";
					std::cerr << currentString << " = " << valueAsString << std::endl;
				}

				getlineWasEvaluated = true;
				break;
			}
			if(getlineWasEvaluated)
			{
				continue;
			}

			std::cerr << "error unknown variable name \"" << getlinestring << "\"!" << std::endl;
		}
	}

	setXDG_RUNTIME_DIR();
	const std::array<std::string, 3> possibleWaylandDisplays =
	{
		"wayland-0",
		"wayland-1",
		"wayland-2"
	};
	for(int x = 0; x < possibleWaylandDisplays.size(); ++x)
	{
		waylandState.display = wl_display_connect(possibleWaylandDisplays.at(x).c_str());
		if(waylandState.display)
		{
			std::cout << "connected to " << possibleWaylandDisplays.at(x) << std::endl;
			break;
		}
	}
	if(!waylandState.display)
	{
		std::cerr << "failed to connect to waylandDisplay" << std::endl;
		return;
	}

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

	if(waylandState.outputs.empty())
	{
		std::cerr << "failed to get output!" << std::endl;
		exit(EXIT_FAILURE);
	}

	waylandState.mouseXOrigin = waylandState.outputsPositions.at(0).x;
	waylandState.mouseYOrigin = waylandState.outputsPositions.at(0).y;
	waylandState.selectedOutput = waylandState.outputs.at(0);
	waylandState.selectedOutputName = waylandState.outputsNames.at(0);
	waylandState.selectedOutputDimensionScaled = waylandState.outputsDimensionsScaled.at(0);
	waylandState.selectedOutputDimensionNotScaled = waylandState.outputsDimensionsNotScaled.at(0);
	waylandState.selectedOutputScale = static_cast<double>(waylandState.selectedOutputDimensionNotScaled.width) / waylandState.selectedOutputDimensionScaled.width;


	waylandState.mouseXExtent = waylandState.selectedOutputDimensionScaled.width;
	waylandState.mouseYExtent = waylandState.selectedOutputDimensionScaled.height;

	AbsolutePointer absolutePointer(waylandState.mouseXOrigin, waylandState.mouseXExtent, waylandState.mouseYOrigin, waylandState.mouseYExtent);

	waylandState.layerSurfaceShouldClose = false;
	waylandState.surface = wl_compositor_create_surface(waylandState.compositor);
	waylandState.layerSurface = zwlr_layer_shell_v1_get_layer_surface
	(
		waylandState.layerShell,
		waylandState.surface,
		waylandState.selectedOutput,
		ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY,
		"keyboardMouseOverlay"
	);
	zwlr_layer_surface_v1_set_keyboard_interactivity(waylandState.layerSurface, false);
	zwlr_layer_surface_v1_set_exclusive_zone(waylandState.layerSurface, -1); //allows surface to sit on top of waybar
	zwlr_layer_surface_v1_add_listener(waylandState.layerSurface, &layerSurfaceListener, &waylandState);
	zwlr_layer_surface_v1_set_size
	(
		waylandState.layerSurface,
		waylandState.selectedOutputDimensionNotScaled.width, //as of Wednesday, January 21, 2026, 14:55:19, yea idk either bru. Monitor scaling killing me rn
		waylandState.selectedOutputDimensionNotScaled.height
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

	bool shiftKeyIsHeldDown = false;
	bool dragActionMouseDown = false;
	input_event inputEvent;
	while
	(
		!waylandState.layerSurfaceShouldClose &&
		!waylandState.shouldActionAndExit &&
		wl_display_dispatch(waylandState.display) != -1
	)
	{
		ssize_t inputEventReadSize = read(keyboardFileDescriptor, &inputEvent, sizeof(inputEvent));
		if(inputEventReadSize == (ssize_t)(-1))
		{
			perror("error reading input device inputEvent!");
			break;
		}
		if(inputEventReadSize == (ssize_t)(0))
		{
			std::cout << "nothing read from inputEvent. EOF maybe?" << std::endl;
			break;
		}

		if(inputEvent.type == EV_KEY)
		{
			if(inputEvent.value == 1) //pressed
			{
				switch(inputEvent.code)
				{
					case KEY_ESC:
					{
						if(waylandState.userKeyboardInput.size() > 0)
						{
							waylandState.userKeyboardInput.clear();
						} else
						{
							waylandState.layerSurfaceShouldClose = true;
						}
						break;
					}

					case KEY_BACKSPACE:
					{
						if(waylandState.userKeyboardInput.size() > 0)
						{
							waylandState.userKeyboardInput.pop_back();
						}
						break;
					}

					case KEY_RIGHTSHIFT:
					case KEY_LEFTSHIFT:
					{
						shiftKeyIsHeldDown = true;
						break;
					}

					default:
					{
						char inputEventCodeAsAscii = '\0';
						try
						{
							inputEventCodeAsAscii = inputEventCodeToAscii.at(inputEvent.code);
						} catch(...)
						{
							inputEventCodeAsAscii = '?';
						}

						if(inputEventCodeAsAscii == '`' || (inputEventCodeAsAscii >= '0' && inputEventCodeAsAscii <= '9'))
						{
							if(waylandState.mouseActionCommitted)
							{
								drawText(&waylandState, "mouse action already committed");
							} else
							{
								switch(inputEventCodeAsAscii)
								{
									case '`':
									{
										drawText(&waylandState, "mouseMove");
										waylandState.mouseAction = MouseAction::move;
										break;
									}
									case '1':
									{
										if(shiftKeyIsHeldDown)
										{
											drawText(&waylandState, "leftDrag");
											waylandState.mouseAction = MouseAction::leftDrag;
										} else
										{
											drawText(&waylandState, "leftClick");
											waylandState.mouseAction = MouseAction::leftClick;
										}
										break;
									}

									case '2':
									{
										if(shiftKeyIsHeldDown)
										{
											drawText(&waylandState, "middleDrag");
											waylandState.mouseAction = MouseAction::middleDrag;
										} else
										{
											drawText(&waylandState, "middleClick");
											waylandState.mouseAction = MouseAction::middleClick;
										}
										break;
									}

									case '3':
									{
										if(shiftKeyIsHeldDown)
										{
											drawText(&waylandState, "rightDrag");
											waylandState.mouseAction = MouseAction::rightDrag;
										} else
										{
											drawText(&waylandState, "rightClick");
											waylandState.mouseAction = MouseAction::rightClick;
										}
										break;
									}
								}
								break;
							}
						}
						waylandState.userKeyboardInput += inputEventCodeAsAscii;

						if
						(
							waylandState.userKeyboardInput.size() > waylandState.letterCombinationsWidth ||
							(
								waylandState.userKeyboardInput.size() == waylandState.letterCombinationsWidth &&
								waylandState.letterCombinationsToClickTargets.count(waylandState.userKeyboardInput) == 0
							) ||
							inputEventCodeAsAscii < 'A' ||
							inputEventCodeAsAscii > 'Z'
						)
						{
							drawFrame(&waylandState);
							wl_surface_attach(waylandState.surface, waylandState.buffer, 0, 0);
							wl_surface_damage(waylandState.surface, 0, 0, waylandState.sharedMemoryWidth, waylandState.sharedMemoryHeight);
							wl_surface_commit(waylandState.surface);
							waylandState.userKeyboardInput.clear();
							break;
						}
						if(waylandState.letterCombinationsToClickTargets.count(waylandState.userKeyboardInput))
						{
							waylandState.mouseActionCommitted = true;
							if
							(
								waylandState.displayMode == DisplayMode::buttonDetection ||
								waylandState.drawAreaResizeCount >= waylandState.drawResizeDivisorFromNthDraw.size() - 1
							)
							{
								int absoluteX = waylandState.mouseXOrigin + waylandState.letterCombinationsToClickTargets.at(waylandState.userKeyboardInput).clickPoint.x;
								int absoluteY = waylandState.mouseYOrigin + waylandState.letterCombinationsToClickTargets.at(waylandState.userKeyboardInput).clickPoint.y;
								absolutePointer.moveAbsolute(absoluteX, absoluteY);

								std::this_thread::sleep_for(std::chrono::milliseconds(waylandState.mouseRelativeJitterSleepMilliseconds));
								if(absoluteX >= waylandState.mouseXExtent)
								{
									mouse.moveRelative(-1, 0);
									std::this_thread::sleep_for(std::chrono::milliseconds(waylandState.mouseRelativeJitterSleepMilliseconds));
									mouse.moveRelative(1, 0);
									std::this_thread::sleep_for(std::chrono::milliseconds(waylandState.mouseRelativeJitterSleepMilliseconds));
								} else
								{
									mouse.moveRelative(1, 0);
									std::this_thread::sleep_for(std::chrono::milliseconds(waylandState.mouseRelativeJitterSleepMilliseconds));
									mouse.moveRelative(-1, 0);
									std::this_thread::sleep_for(std::chrono::milliseconds(waylandState.mouseRelativeJitterSleepMilliseconds));
								}

								drawFrame(&waylandState);
								wl_surface_attach(waylandState.surface, waylandState.buffer, 0, 0);
								wl_surface_damage(waylandState.surface, 0, 0, waylandState.sharedMemoryWidth, waylandState.sharedMemoryHeight);
								wl_surface_commit(waylandState.surface);
								switch(waylandState.mouseAction)
								{
									case MouseAction::move:
									{
										waylandState.shouldActionAndExit = true;
										break;
									}
									case MouseAction::leftClick:
									{
										mouse.leftClick();
										waylandState.shouldActionAndExit = true;
										break;
									}
									case MouseAction::rightClick:
									{
										mouse.rightClick();
										waylandState.shouldActionAndExit = true;
										break;
									}
									case MouseAction::middleClick:
									{
										mouse.middleClick();
										waylandState.shouldActionAndExit = true;
										break;
									}
									case MouseAction::leftDrag:
									{
										if(dragActionMouseDown)
										{
											mouse.leftUp();
											waylandState.shouldActionAndExit = true;
										} else
										{
											mouse.leftDown();
											resetDrawFrameVariables(waylandState);
											if(waylandState.displayMode == DisplayMode::buttonDetection)
											{
												drawInitialButtonDetectionFrame(&waylandState);
											} else if(waylandState.displayMode == DisplayMode::grid)
											{
												drawNthInitialGridFrame(&waylandState);
											}
										}
										dragActionMouseDown = !dragActionMouseDown;
										break;
									}
									case MouseAction::rightDrag:
									{
										if(dragActionMouseDown)
										{
											mouse.rightUp();
											waylandState.shouldActionAndExit = true;
										} else
										{
											mouse.rightDown();
											resetDrawFrameVariables(waylandState);
											if(waylandState.displayMode == DisplayMode::buttonDetection)
											{
												drawInitialButtonDetectionFrame(&waylandState);
											} else if(waylandState.displayMode == DisplayMode::grid)
											{
												drawNthInitialGridFrame(&waylandState);
											}
										}
										dragActionMouseDown = !dragActionMouseDown;
										break;
									}
									case MouseAction::middleDrag:
									{
										if(dragActionMouseDown)
										{
											mouse.middleUp();
											waylandState.shouldActionAndExit = true;
										} else
										{
											mouse.middleDown();
											resetDrawFrameVariables(waylandState);
											if(waylandState.displayMode == DisplayMode::buttonDetection)
											{
												drawInitialButtonDetectionFrame(&waylandState);
											} else if(waylandState.displayMode == DisplayMode::grid)
											{
												drawNthInitialGridFrame(&waylandState);
											}
										}
										dragActionMouseDown = !dragActionMouseDown;
										break;
									}
								}
							} else if(waylandState.displayMode == DisplayMode::grid)
							{
								waylandState.drawArea = waylandState.letterCombinationsToClickTargets.at(waylandState.userKeyboardInput).rectangle;
								bool resetGridDrawAreaResizeCount = false;
								resetDrawFrameVariables(waylandState, resetGridDrawAreaResizeCount);
								bool increment = true;
								drawNthInitialGridFrame(&waylandState, increment);
							}
						}
						break;
					}
				}
			} else if(inputEvent.value == 0) //released
			{
				switch(inputEvent.code)
				{
					case KEY_RIGHTSHIFT:
					case KEY_LEFTSHIFT:
					{
						shiftKeyIsHeldDown = false;
						break;
					}
				}
			}
		}
	}

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

	wl_shm_pool_destroy(waylandState.sharedMemoryPool);
	close(waylandState.sharedMemoryPoolFileDescriptor);
	munmap(waylandState.sharedMemoryPoolData, waylandState.sharedMemoryPoolSize);

	if(waylandState.display)
	{
		//must be called last!
		wl_display_disconnect(waylandState.display);
		std::cout << "disconnected from wayland display" << std::endl;
	}
}

int main()
{
	const std::filesystem::path datFolderFilePath("/etc/keyboardMouse/");
	const std::filesystem::path keyboardTargetTextFilePath(datFolderFilePath / "keyboardTarget.txt");
	const std::filesystem::path configFilePath(datFolderFilePath / "config.txt");
	const std::filesystem::path eventDeviceFolderPath("/dev/input/");

	if(!std::filesystem::exists(datFolderFilePath))
	{
		std::cerr << datFolderFilePath << " doesn't exist! creating " << datFolderFilePath << " folder..." << std::endl;
		if(!std::filesystem::create_directory(datFolderFilePath))
		{
			std::cerr << "failed to create " << datFolderFilePath << " folder! aborting!" << std::endl;
			exit(EXIT_FAILURE);
		}
	}

	std::fstream fstreamReader;
	fstreamReader.open(keyboardTargetTextFilePath, std::fstream::in);
	if(fstreamReader.fail())
	{
		std::cerr << "failed to read from " << keyboardTargetTextFilePath << "! aborting!" << std::endl;
		exit(EXIT_FAILURE);
	}
	std::string keyboardTarget;
	std::getline(fstreamReader, keyboardTarget);

	std::filesystem::path keyboardDevicePath("/");
	std::string keyboardDeviceName;
	for
	(
		std::filesystem::directory_iterator it(eventDeviceFolderPath);
		it != std::filesystem::directory_iterator{};
		++it
	)
	{
		std::filesystem::directory_entry entry = *it;

		int fileDescriptor = open(entry.path().c_str(), O_RDONLY);
		if(fileDescriptor == -1)
		{
			++it;
			continue;
		}
		char deviceName[256];
		if(ioctl(fileDescriptor, EVIOCGNAME(sizeof(deviceName)), deviceName) == -1)
		{
			++it;
			continue;
		}

		if(std::string(deviceName).find(keyboardTarget) != std::string::npos)
		{
			keyboardDevicePath = entry.path();
			keyboardDeviceName = deviceName;
			break;
		}
	}

	if(keyboardDevicePath.string() == "/")
	{
		std::cerr << "failed to find eventDevice named \"" << keyboardTarget << "\"!" << std::endl;
		std::cerr << "listing all eventDevice names!" << std::endl;
		for
		(
			std::filesystem::directory_iterator it(eventDeviceFolderPath);
			it != std::filesystem::directory_iterator{};
			++it
		)
		{
			std::filesystem::directory_entry entry = *it;

			int fileDescriptor = open(entry.path().c_str(), O_RDONLY);
			if(fileDescriptor == -1)
			{
				++it;
				continue;
			}
			char deviceName[256];
			if(ioctl(fileDescriptor, EVIOCGNAME(sizeof(deviceName)), deviceName) == -1)
			{
				++it;
				continue;
			}
			std::cerr << entry.path() << " " << deviceName << std::endl;
		}
		std::cerr << "aborting!" << std::endl;
		exit(EXIT_FAILURE);
	}
	std::cout << "keyboardTarget \"" << keyboardTarget << "\" found!" << std::endl;
	std::cout << keyboardDevicePath << " " << keyboardDeviceName << std::endl;

	int keyboardFileDescriptor = open(keyboardDevicePath.c_str(), O_RDONLY);
	if(keyboardFileDescriptor == -1)
	{
		perror("something went wrong while opening device...");
		exit(EXIT_FAILURE);
	}

	std::chrono::time_point<std::chrono::high_resolution_clock> previousShiftClickTime = std::chrono::high_resolution_clock::now();
	std::chrono::time_point<std::chrono::high_resolution_clock> currentShiftClickTime = previousShiftClickTime;
	std::chrono::time_point<std::chrono::high_resolution_clock> currentAltClickTime = previousShiftClickTime;
	std::chrono::time_point<std::chrono::high_resolution_clock> previousAltClickTime = previousShiftClickTime;
	std::chrono::milliseconds doubleClickkMillisecondsThreshold(200);

	bool escapeKeyDown = false;
	bool deleteKeyDown = false;
	while(!escapeKeyDown || !deleteKeyDown)
	{
		input_event inputEvent;

		ssize_t inputEventReadSize = read(keyboardFileDescriptor, &inputEvent, sizeof(inputEvent));
		if(inputEventReadSize == (ssize_t)(-1))
		{
			perror("error reading input device inputEvent!");
			break;
		}
		if(inputEventReadSize == (ssize_t)(0))
		{
			std::cout << "nothing read from inputEvent. EOF maybe?" << std::endl;
			break;
		}
		if(inputEvent.type == EV_KEY)
		{
			if(inputEvent.value == 1) //pressed
			{
				switch(inputEvent.code)
				{
					case KEY_ESC:
						escapeKeyDown = true;
						break;
					case KEY_DELETE:
						deleteKeyDown = true;
						break;
				}
			} else if(inputEvent.value == 0) //released
			{
				switch(inputEvent.code)
				{
					case KEY_ESC:
						escapeKeyDown = false;
						break;
					case KEY_DELETE:
						deleteKeyDown = false;
						break;
					case KEY_LEFTALT:
					case KEY_RIGHTALT:
					{
						currentAltClickTime = std::chrono::high_resolution_clock::now();
						std::chrono::duration<double> previousAltclickToCurrentAltClickDurationMilliseconds = currentAltClickTime - previousAltClickTime;
						if(previousAltclickToCurrentAltClickDurationMilliseconds <= doubleClickkMillisecondsThreshold)
						{
							ioctl(keyboardFileDescriptor, EVIOCGRAB, 1); //grab
							keyboardMouse(DisplayMode::grid, keyboardFileDescriptor, configFilePath);
							ioctl(keyboardFileDescriptor, EVIOCGRAB, 0); //ungrab
						}
						previousAltClickTime = currentAltClickTime;
						break;
					}
					case KEY_LEFTSHIFT:
					case KEY_RIGHTSHIFT:
					{
						currentShiftClickTime = std::chrono::high_resolution_clock::now();
						std::chrono::duration<double> previousShiftclickToCurrentShiftClickDurationMilliseconds = currentShiftClickTime - previousShiftClickTime;
						if(previousShiftclickToCurrentShiftClickDurationMilliseconds <= doubleClickkMillisecondsThreshold)
						{
							ioctl(keyboardFileDescriptor, EVIOCGRAB, 1); //grab
							keyboardMouse(DisplayMode::buttonDetection, keyboardFileDescriptor, configFilePath);
							ioctl(keyboardFileDescriptor, EVIOCGRAB, 0); //ungrab
						}
						previousShiftClickTime = currentShiftClickTime;
						break;
					}
				}
			}
		}
	}
	close(keyboardFileDescriptor);

	return 0;
}
