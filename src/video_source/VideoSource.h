#ifndef CAPTURE_VIDEO_H
#define CAPTURE_VIDEO_H

#include <iostream>
#include <atomic>

#include <opencv2/opencv.hpp>

#include "VideoFrameQueue.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libswscale/swscale.h>
}


class VideoSource
{
private:
    /* data */
public:
    VideoSource(const char*device_name, std::atomic<bool>* is_pushing);
    ~VideoSource();

    bool Init();

    void start();

    bool initDecoder(AVStream* video_stream);
private:
    cv::Mat HWFrameToCvMat(AVFrame* frame);
private:
    const char *device_name;

    const AVInputFormat *input_format = nullptr;

    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;
    SwsContext *sws_ctx = nullptr;

    std::atomic<bool>* is_running;

    int video_stream_index;

    VideoFrameQueue<cv::Mat> frame_queue;
};
#endif // CAPTURE_VIDEO_H