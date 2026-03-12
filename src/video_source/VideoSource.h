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
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
}

class VideoSource
{
public:
    VideoSource(const char* device_name, std::atomic<bool>* is_pushing, VideoFrameQueue<cv::Mat>* shared_queue = nullptr);
    VideoSource(const char* device_name, std::atomic<bool>* is_pushing, int width, int height, int framerate, VideoFrameQueue<cv::Mat>* shared_queue = nullptr);
    ~VideoSource();

    bool Init();
    void start();

    bool initDecoder(AVStream* video_stream);

    VideoFrameQueue<cv::Mat>& getFrameQueue() { return *frame_queue; }

private:
    cv::Mat HWFrameToCvMat(AVFrame* frame);

private:
    const char* device_name;
    int capture_width = 640;
    int capture_height = 360;
    int capture_framerate = 15;

    const AVInputFormat* input_format = nullptr;

    AVFormatContext* format_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVPixelFormat hw_pix_fmt = AV_PIX_FMT_NONE;

    std::atomic<bool>* is_running;

    int video_stream_index;

    VideoFrameQueue<cv::Mat>* frame_queue;
    VideoFrameQueue<cv::Mat> local_queue;
    bool use_shared_queue = false;
};

#endif
