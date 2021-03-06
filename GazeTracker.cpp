#include <fstream>

#include <boost/lexical_cast.hpp>


#include "GazeTracker.h"
#include "Point.h"
#include "mir.h"
#include "Application.h"


static void ignore(const cv::Mat *) {
}


GazeTracker::GazeTracker()
{
	gazePoint.x = 0;
	gazePoint.y = 0;
	gazePoint.isBlinking=false;
	_eyeExtractor = NULL;
	_pointTracker = NULL;
}

bool GazeTracker::isActive() {
	return _gaussianProcessX.get() && _gaussianProcessY.get();
}

void GazeTracker::addExemplar() {
	// Add new sample to the GPs. Save the image samples (average eye images) in corresponding vectors
	_calibrationTargetImages.push_back(_eyeExtractor->averageEye->getMean());
	_calibrationTargetImagesLeft.push_back(_eyeExtractor->averageEyeLeft->getMean());

	updateGaussianProcesses();
}

void GazeTracker::clear() {
    // Clear previous calibration information
    _calibrationTargetImages.clear();
    _calibrationTargetImagesLeft.clear();
    
    _calibrationTargetImagesAllFrames.clear();
    _calibrationTargetImagesLeftAllFrames.clear();
    
    _calibrationTargetPointsAllFrames.clear();

    // Reset GP estimators
	_gaussianProcessX.reset(NULL);
	_gaussianProcessY.reset(NULL);

	_gaussianProcessXLeft.reset(NULL);
	_gaussianProcessYLeft.reset(NULL);
}

void GazeTracker::draw() {
	if (!Application::Data::isTrackingSuccessful)
		return;

	cv::Mat image = Application::Components::videoInput->debugFrame;

	// If not blinking, draw the estimations to debug window
	if (isActive() && !_eyeExtractor->isBlinking()) {
		cv::Point estimation(gazePoint.x, gazePoint.y);
        
		cv::circle(image,
			Utils::mapFromSecondMonitorToDebugFrameCoordinates(estimation),
			8, cv::Scalar(0, 255, 0), -1, 8, 0);
	}
}

void GazeTracker::process() {
    if(_pointTracker == NULL) {
        _pointTracker = (PointTracker*) Application::getComponent("PointTracker");
    }
    
    if(_eyeExtractor == NULL) {
        _eyeExtractor = (EyeExtractor*) Application::getComponent("EyeExtractor");
    }
    
	if (!Application::Data::isTrackingSuccessful) {
		return;
	}

	// If recalibration is necessary (there is a new target), recalibrate the Gaussian Processes
	if(Application::Components::calibrator->needRecalibration) {
		addExemplar();
	}

	if(Application::Components::calibrator->isActive()
		&& Application::Components::calibrator->getPointFrameNo() >= 11
		&& !_eyeExtractor->isBlinking()) {

		// Add current sample (not the average, but sample from each usable frame) to the vector
		cv::Mat *temp = new cv::Mat(EyeExtractor::eyeSize, CV_32FC1);
		_eyeExtractor->eyeFloat.copyTo(*temp);

		Utils::SharedImage temp2(new cv::Mat(temp->size(), temp->type()), Utils::releaseImage);
		_calibrationTargetImagesAllFrames.push_back(temp2);


		// Repeat for left eye
		temp = new cv::Mat(EyeExtractor::eyeSize, CV_32FC1);
		_eyeExtractor->eyeFloatLeft.copyTo(*temp);

		Utils::SharedImage temp3(new cv::Mat(temp->size(), temp->type()), Utils::releaseImage);
		_calibrationTargetImagesLeftAllFrames.push_back(temp3);

		_calibrationTargetPointsAllFrames.push_back(Application::Components::calibrator->getActivePoint());
	}

	// Update the left and right estimations
	updateEstimations();
}

void GazeTracker::updateEstimations() {
	if (isActive()) {
		cv::Mat *image = &_eyeExtractor->eyeFloat;
		cv::Mat *leftImage = &_eyeExtractor->eyeFloatLeft;
        
		gazePoint.x = (_gaussianProcessX->getmean(Utils::SharedImage(image, &ignore)) + _gaussianProcessXLeft->getmean(Utils::SharedImage(leftImage, &ignore))) / 2;
		gazePoint.y = (_gaussianProcessY->getmean(Utils::SharedImage(image, &ignore)) + _gaussianProcessYLeft->getmean(Utils::SharedImage(leftImage, &ignore))) / 2;
		gazePoint.isBlinking=_eyeExtractor->isBlinking();
		// Make sure estimation stays in the screen area
		Utils::boundToScreenArea(gazePoint);
	}
}

double GazeTracker::imageDistance(const cv::Mat *image1, const cv::Mat *image2) {
	double norm = cv::norm(*image1, *image2, CV_L2);
	return norm * norm;
}

double GazeTracker::covarianceFunction(Utils::SharedImage const &image1, Utils::SharedImage const &image2) {
	static double sigma = Utils::getParameterAsDouble("sigma", 2.0);
	static double lscale = Utils::getParameterAsDouble("lscale", 2000.0);
	
	return sigma * sigma * exp(-imageDistance(image1.get(), image2.get()) / (2 * lscale * lscale));
}

void GazeTracker::updateGaussianProcesses() {
	std::vector<double> xLabels;
	std::vector<double> yLabels;

	// Prepare separate vector of targets for X and Y directions
	for (int i = 0; i < Application::Data::calibrationTargets.size(); i++) {
		xLabels.push_back(Application::Data::calibrationTargets[i].x);
		yLabels.push_back(Application::Data::calibrationTargets[i].y);
	}

	_gaussianProcessX.reset(new ImProcess(_calibrationTargetImages, xLabels, covarianceFunction, 0.01));
	_gaussianProcessY.reset(new ImProcess(_calibrationTargetImages, yLabels, covarianceFunction, 0.01));

	_gaussianProcessXLeft.reset(new ImProcess(_calibrationTargetImagesLeft, xLabels, covarianceFunction, 0.01));
	_gaussianProcessYLeft.reset(new ImProcess(_calibrationTargetImagesLeft, yLabels, covarianceFunction, 0.01));
}
