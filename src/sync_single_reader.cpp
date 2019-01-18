#include "preprocess/process.h"
#include "ptgrey_lib/singleCameraReader.h"
#include <cv_bridge/cv_bridge.h>
#include <flycapture/FlyCapture2.h>
#include <iostream>
#include <opencv2/opencv.hpp>
#include <ros/ros.h>
#include <sstream>
#include <string>
#include <ros/ros.h>
#include <std_msgs/Header.h>
#include <sensor_msgs/TimeReference.h>

using namespace ros;

class SyncSingleReader {
    ros::NodeHandle nh;
    ros::Publisher imageROIPublisher;
    ros::Publisher imageGreyPublisher;
    ros::Publisher imageROIGreyPublisher;
    ros::Subscriber trigger_time_sub;
    ros::Publisher imagePublisher;

    preprocess::PreProcess* pre = nullptr;
    bool trigger_time_vaild = true;
    std_msgs::Header tri_header;

    bool is_pub              = true;
    bool is_show             = false;
    bool is_print            = true;
    bool is_first            = true;
    bool is_grey             = false;
    int serialNum            = 17221121;
    bool is_auto_shutter     = false;
    bool is_sync             = true;
    bool is_roi              = false;
    bool use_gpu             = false;
    bool need_skip           = true;
    double brightness        = 0.1;
    double exposure          = 0.1;
    double gain              = 1.0;
    double frameRate         = 20.0;
    double shutter           = 5.0;
    int WB_red               = 500;
    int WB_Blue              = 800;
    double saturation        = 100;
    double hue               = 0;
    double sharpness         = 0;
    double down_sample_scale = 0.75;
    bool no_sync_recved = true;
    std::string topic_name = "";
    int size_x = 0, size_y = 0;
    int center_x = 0, center_y = 0;
    int cropper_x = 0, cropper_y = 0;
    int src_rows, src_cols;
    cv::Mat image_grey;
    ptgrey_reader::singleCameraReader * camReader;
    int imageCnt = 0;

    std::vector<std_msgs::Header> trigger_ts_queue;



    void on_time_reference(const sensor_msgs::TimeReference & _ref) {
        double from_last = _ref.header.stamp.toSec() - tri_header.stamp.toSec();
        double from_last2 = ros::Time::now().toSec() - tri_header.stamp.toSec();
        tri_header = _ref.header;

        trigger_ts_queue.push_back(tri_header);

        trigger_time_vaild = true;
        no_sync_recved = false;
        if (is_print)
            ROS_INFO("tri recved align %3.2fms dt %3.2f %3.2fms", (ros::Time::now()- _ref.header.stamp).toSec()*1000, from_last*1000, from_last2*1000);
        grab();
    }

    void grab() {
    // void grab(const ros::TimerEvent & e) {
        // if (!trigger_time_vaild)
            // return;
        bool need_regrab = true;
        // int recali
        cv_bridge::CvImage outImg;
        ptgrey_reader::cvImage cv_image;
        while (need_regrab) {

            cv_image = camReader->grabImage( );
            ++imageCnt;

            if ( cv_image.image.empty( ) )
            {
                std::cout << "[#INFO] Grabbed no image." << std::endl;
                return;
            }
            
            outImg.header.stamp.sec  = cv_image.time.seconds;
            outImg.header.stamp.nsec = cv_image.time.microSeconds * 1000;

            auto old_ts = outImg.header.stamp;
            
            if (is_sync) {
                outImg.header.stamp = tri_header.stamp;
                //Still have bugs... that trigger is faster than image, should be slower..
                if (is_print)
                    ROS_INFO("using trigger ts, dt %3.2f ms", (tri_header.stamp - old_ts).toSec()*1000);
                
                if ((tri_header.stamp - old_ts).toSec() > 0) {
                    ROS_WARN("Image too old, will regrab");
                    continue;
                } else {
                    need_regrab = false;
                }
                
                if (fabs( (tri_header.stamp - old_ts).toSec()*1000 + 32) > 5) {
                    ROS_WARN("using unexpect trigger ts, dt %3.2f ms", (tri_header.stamp - old_ts).toSec()*1000);
                }

                
            } else {
                need_regrab = false;
            }
        }

        if ( is_print )
            std::cout << serialNum << " grabbed image " << imageCnt << std::endl;

        if ( is_pub )
        {

            outImg.header.frame_id = "frame";
            if ( camReader->Camera( ).isColorCamera( ) )
            {
                outImg.encoding = sensor_msgs::image_encodings::BGR8;
                if ( is_first )
                {
                    src_rows = cv_image.image.rows;
                    src_cols = cv_image.image.cols;
                    cv::Mat img_tmp( src_rows, src_cols, CV_8UC1 );
                    img_tmp.copyTo( image_grey );
                    is_first = false;
                }
            }
            else
                outImg.encoding = sensor_msgs::image_encodings::MONO8;

            outImg.image = cv_image.image;
            imagePublisher.publish( outImg );

            if ( is_roi )
            {
                if (use_gpu){
                    if ( is_print )
                        ROS_INFO("Proprecess with gpu");

                    pre->do_preprocess_gpu( cv_image.image, outImg.image);
                } else {
                    pre->do_preprocess_cpu( cv_image.image, outImg.image);
                }

                imageROIPublisher.publish( outImg );
            }

            if ( is_grey && camReader->Camera( ).isColorCamera( ) )
            {
                outImg.encoding = sensor_msgs::image_encodings::MONO8;

                // cv::cvtColor( cv_image.image, outImg.image, CV_BGR2GRAY );
                // colorToGrey( cv_image.image, image_grey );
                outImg.image = image_grey;
                imageGreyPublisher.publish( outImg );

                if ( is_roi )
                {
                    if (use_gpu){
                        pre->do_preprocess_gpu( cv_image.image, outImg.image);
                    } else {
                        pre->do_preprocess_cpu( cv_image.image, outImg.image);
                    }
                }
            }
        }

        if ( is_show )
        {
            cv::imshow( "image", cv_image.image );
            cv::waitKey( 10 );
        }
    }
public:
    SyncSingleReader(ros::NodeHandle & _nh):
        nh(_nh)
    {


        nh.getParam( "is_grey", is_grey );
        nh.getParam( "use_gpu", use_gpu );
        nh.getParam( "is_pub", is_pub );
        nh.getParam( "is_show", is_show );
        nh.getParam( "is_print", is_print );
        nh.getParam( "serialNum", serialNum );
        nh.getParam( "is_auto_shutter", is_auto_shutter );
        nh.getParam( "is_sync", is_sync );
        nh.getParam( "is_roi", is_roi );
        nh.getParam( "brightness", brightness );
        nh.getParam( "exposure", exposure );
        nh.getParam( "gain", gain );
        nh.getParam( "frameRate", frameRate );
        nh.getParam( "shutter", shutter );
        nh.getParam( "WB_red", WB_red );
        nh.getParam( "WB_Blue", WB_Blue );
        nh.getParam( "saturation", saturation );
        nh.getParam( "hue", hue );
        nh.getParam( "sharpness", sharpness );

        nh.getParam( "down_sample_scale", down_sample_scale );
        nh.getParam( "size_x", size_x );
        nh.getParam( "size_y", size_y );
        nh.getParam( "center_x", center_x );
        nh.getParam( "center_y", center_y );
        nh.getParam( "cropper_x", cropper_x );
        nh.getParam( "cropper_y", cropper_y );

        std::stringstream os;
        os << serialNum;

        ros::param::param<std::string>("topic_name", topic_name, "/pg_" + os.str( ));


        pre = new preprocess::PreProcess( cv::Size( size_x, size_y ),
                                        cv::Size( cropper_x, cropper_y ),
                                        cv::Point( center_x, center_y ),
                                        down_sample_scale );

        unsigned int cameraId = serialNum;

        camReader = new ptgrey_reader::singleCameraReader( cameraId );

        imagePublisher = nh.advertise< sensor_msgs::Image >( "/image_raw", 3 );


        if ( is_show )
            cv::namedWindow( "image", CV_WINDOW_NORMAL );

        bool is_cameraStarted = camReader->startCamera( cameraId,
                                                    frameRate,
                                                    brightness,
                                                    exposure,
                                                    gain, //
                                                    is_auto_shutter,
                                                    shutter,
                                                    WB_red,
                                                    WB_Blue,
                                                    saturation,
                                                    hue,
                                                    sharpness,
                                                    is_print,
                                                    is_sync );

        if ( is_roi )
            imageROIPublisher
            = nh.advertise< sensor_msgs::Image >( "/image", 3 );
        if ( is_grey && camReader->Camera( ).isColorCamera( ) )
        {
            imageGreyPublisher
            = nh.advertise< sensor_msgs::Image >( "/image_grey", 3 );

        if ( is_roi )
            imageROIGreyPublisher
            = nh.advertise< sensor_msgs::Image >( "/image_roi", 3 );
        }
        
        if (is_sync) {
            ROS_INFO("Is trigger, subscribe to time reference");
            trigger_time_sub = nh.subscribe("/dji_sdk_1/dji_sdk/trigger_time", 1, &SyncSingleReader::on_time_reference, this, ros::TransportHints().tcpNoDelay());
        }

        if ( !is_cameraStarted )
        {
            ros::shutdown( );
            std::cout << "[#INFO] Camera cannot start" << std::endl;
        }

        std::cout << "[#INFO] Loop start." << ros::ok( ) << std::endl;

        // timer = nh.createTimer(ros::Duration(0.05), &SyncSingleReader::grab, this);
    }

    ros::Timer timer;

    void stop() {
        camReader->stopCamera( );
    }

};

int main(int argc, char * argv[]) {

    ros::init(argc, argv, "sync_single_reader");

    ros::NodeHandle nh("sync_single_reader");
    ROS_INFO("Start Sync Single reader");
    SyncSingleReader ssr(nh);
    // ros::MultiThreadedSpinner spinner(1); // Use 4 threads
    // spinner.spin();
    ros::spin();
}