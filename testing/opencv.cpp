#include <iostream>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/core/types.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv4/opencv2/core/base.hpp>

void nextApeture(int state, void* data)
{
	int* apeturePointer = static_cast<int*>(data);
	*apeturePointer += 2;
	if(*apeturePointer > 7)
	{
		*apeturePointer = 3;
	}

	std::cout << *apeturePointer << std::endl;
}

int main(int argc, char** argv)
{
	cv::Scalar color(0, 0, 255); //BGR
	int thickness = 1;
	double lowerThreshold = 0;
	double upperThreshold = 255;
	int apetureSize = 5;
	double maxval = 255;

	cv::namedWindow("mainWindow", cv::WINDOW_NORMAL);
	cv::createTrackbar("lowerThresholdTrackbar", "mainWindow", 0, 255);
	cv::createTrackbar("upperThresholdTrackbar", "mainWindow", 0, 255);
	cv::createButton("apetureButton", nextApeture, &apetureSize);
	do
	{
		double lowerThreshold = cv::getTrackbarPos("lowerThresholdTrackbar", "mainWindow");
		double upperThreshold = cv::getTrackbarPos("upperThresholdTrackbar", "mainWindow");

		cv::Mat originalMat = cv::imread(argv[1]);
		cv::Mat grayMat(originalMat);
		cv::cvtColor(grayMat, grayMat, cv::COLOR_BGR2GRAY);
		cv::Canny(grayMat, grayMat, lowerThreshold, upperThreshold, apetureSize, false);

		std::vector<std::vector<cv::Point>> contours;
		//cv::findContours(grayMat, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
		//cv::findContours(grayMat, contours, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
		cv::findContours(grayMat, contours, cv::RETR_LIST, cv::CHAIN_APPROX_SIMPLE);

		//cv::dilate(grayMat, grayMat, cv::getStructuringElement(cv::MORPH_DILATE, cv::Size(16, 16)), cv::Point(-1, -1), 1, cv::BORDER_CONSTANT);
		//cv::dilate(grayMat, grayMat, cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
		cv::dilate(grayMat, grayMat, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 3)));
		//cv::erode(grayMat, grayMat, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(1, 1)));

		/*
		cv2.RETR_EXTERNAL: Retrieves only the outermost contours.
		cv2.RETR_LIST: Retrieves all contours without any hierarchy.
		cv2.RETR_TREE: Retrieves all contours and reconstructs a full hierarchy of nested contours. 

		cv2.CHAIN_APPROX_NONE: Stores all contour points.
		cv2.CHAIN_APPROX_SIMPLE: Compresses horizontal, vertical, and diagonal segments, saving memory by storing only the endpoints of these segments.
		*/

		for(size_t x = 0; x < contours.size(); ++x)
		{
			cv::Rect boundingRect = cv::boundingRect(contours[x]);
			cv::rectangle
			(
				originalMat,
				boundingRect.tl(),
				boundingRect.br(),
				color,
				thickness
			);
		}

		cv::imshow("mainWindow", grayMat);
		cv::imshow("secondWindow", originalMat);
	} while(cv::waitKey(500) != 27);
	cv::destroyAllWindows();

	return 0;
}
