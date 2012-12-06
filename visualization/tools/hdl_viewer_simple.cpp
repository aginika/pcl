/*
 * Software License Agreement (BSD License)
 *
 *  Copyright (c) 2011, The MITRE Corporation
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 * Author: Keven Ring <keven@mitre.org>
 */

#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/common/time.h> //fps calculations
#include <pcl/io/hdl_grabber.h>
#include <pcl/visualization/cloud_viewer.h>
#include <pcl/visualization/image_viewer.h>
#include <pcl/io/openni_camera/openni_driver.h>
#include <pcl/console/parse.h>
#include <pcl/visualization/boost.h>
#include <pcl/visualization/mouse_event.h>
#include <vector>
#include <string>
#include <boost/algorithm/string.hpp>

using namespace std;

#define SHOW_FPS 0
#if SHOW_FPS
#define FPS_CALC(_WHAT_) \
do \
{ \
    static unsigned count = 0;\
    static double last = pcl::getTime ();\
    double now = pcl::getTime (); \
    ++count; \
    if (now - last >= 1.0) \
    { \
      std::cout << "Average framerate("<< _WHAT_ << "): " << double(count)/double(now - last) << " Hz" <<  std::endl; \
      count = 0; \
      last = now; \
    } \
}while(false)
#else
#define FPS_CALC(_WHAT_) \
do \
{ \
}while(false)
#endif

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
template<typename PointType>
class SimpleHDLViewer {
public:
	typedef pcl::PointCloud<PointType> Cloud;
	typedef typename Cloud::ConstPtr CloudConstPtr;

	SimpleHDLViewer(pcl::Grabber& grabber) :
		 cloud_viewer_ (new pcl::visualization::PCLVisualizer ("PCL HDL Cloud")), grabber_ (grabber){
	}

	void cloud_callback(const CloudConstPtr& cloud) {
		FPS_CALC("cloud callback");
		boost::mutex::scoped_lock lock(cloud_mutex_);
		cloud_ = cloud;
		//std::cout << cloud->points[0] << " " << cloud->size() << std::endl;
	}

	void keyboard_callback(const pcl::visualization::KeyboardEvent& event, void* cookie) {
		if (event.getKeyCode())
			cout << "the key \'" << event.getKeyCode() << "\' (" << static_cast<int>(event.getKeyCode()) << ") was";
		else
			cout << "the special key \'" << event.getKeySym() << "\' was";
		if (event.keyDown())
			cout << " pressed" << endl;
		else
			cout << " released" << endl;
	}

	void mouse_callback(const pcl::visualization::MouseEvent& mouse_event, void* cookie) {
		if (mouse_event.getType() == pcl::visualization::MouseEvent::MouseButtonPress
				&& mouse_event.getButton() == pcl::visualization::MouseEvent::LeftButton) {
			cout << mouse_event.getX() << " , " << mouse_event.getY() << endl;
		}
	}

	void run() {
		cloud_viewer_->addCoordinateSystem(300.0);
		cloud_viewer_->setBackgroundColor(0, 0, 0);
		cloud_viewer_->initCameraParameters();
		cloud_viewer_->setCameraPosition(0.0, 0.0, 3000.0, 0.0, 1.0, 0.0, 0);
		cloud_viewer_->setCameraClipDistances(0.0, 5000.0);
		//cloud_viewer_->registerMouseCallback(&SimpleHDLViewer::mouse_callback, *this);
		//cloud_viewer_->registerKeyboardCallback(&SimpleHDLViewer::keyboard_callback, *this);

		boost::function<void(const CloudConstPtr&)> cloud_cb = boost::bind(&SimpleHDLViewer::cloud_callback, this, _1);
		boost::signals2::connection cloud_connection = grabber_.registerCallback(cloud_cb);

		grabber_.start();

		while (!cloud_viewer_->wasStopped()) {
			CloudConstPtr cloud;

			// See if we can get a cloud
			if (cloud_mutex_.try_lock()) {
				cloud_.swap(cloud);
				cloud_mutex_.unlock();
			}

			if (cloud) {
				FPS_CALC("drawing cloud");
				//if (!cloud_viewer_->updatePointCloud(cloud, "HDL")) {
				//	cloud_viewer_->addPointCloud(cloud, "HDL");
				//}
				if (!cloud_viewer_->updatePointCloud(cloud, "HDL")) {
					cloud_viewer_->addPointCloud(cloud, "HDL");
				}
				cloud_viewer_->spinOnce();
			}

			if (!grabber_.isRunning()) {
				cloud_viewer_->spin();
			}

			boost::this_thread::sleep(boost::posix_time::microseconds(100));
		}

		grabber_.stop();

		cloud_connection.disconnect();
	}

	boost::shared_ptr<pcl::visualization::PCLVisualizer> cloud_viewer_;
	boost::shared_ptr<pcl::visualization::ImageViewer> image_viewer_;

	pcl::Grabber& grabber_;
	boost::mutex cloud_mutex_;
	boost::mutex image_mutex_;

	CloudConstPtr cloud_;
};

void usage(char ** argv) {
	cout << "usage: " << argv[0]
			<< " [-hdlCalibration <path-to-calibration-file>] [-pcapFile <path-to-pcap-file>] [-h | --help] [-format XYZ|XYZRGB]" << endl;
	cout << argv[0] << " -h | --help : shows this help" << endl;
	return;
}

int main(int argc, char ** argv) {
	std::string hdlCalibration, pcapFile, format("XYZ");

	if (pcl::console::find_switch(argc, argv, "-h") || pcl::console::find_switch(argc, argv, "--help")) {
		usage(argv);
		return (0);
	}

	pcl::console::parse_argument(argc, argv, "-calibrationFile", hdlCalibration);
	pcl::console::parse_argument(argc, argv, "-pcapFile", pcapFile);
	pcl::console::parse_argument(argc, argv, "-format", format);

	pcl::HDL_Grabber grabber(hdlCalibration, pcapFile);

	if (boost::iequals(format, std::string("XYZ"))) {
		SimpleHDLViewer<pcl::PointXYZ> v(grabber);
		v.run();
	}
	else if (boost::iequals(format, std::string("XYZRGB"))) {
		SimpleHDLViewer<pcl::PointXYZRGB> v(grabber);
		v.run();
	}
	return (0);
}