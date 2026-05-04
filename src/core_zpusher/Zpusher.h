#ifndef ZPUSHER_H
#define ZPUSHER_H

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavdevice/avdevice.h>
}

#include <string>
#include <atomic>
#include <memory>
#include "video_source/VideoSource.h"
#include "yolov11_onnx.h"
#include "threadPool.h"
#include "video_push.h"
#include "core_recorder/VideoRecorder.h"
#include "safe_queue/VideoFrameQueue.h"
#include "safe_queue/FrameData.h"

class ZPusher
{
public:
    ZPusher();
    ~ZPusher();

    void Init(const std::string& config_path = "config.json");
    void start_Push();
    void stop_Push();

private:
    void Destroy();

private:
    ThreadPool* thread_pool = nullptr;
    VideoSource* video_source = nullptr;
    YOLOv11Detector* yolov11_detector = nullptr;
    FFmpegPush* ffmpeg_push = nullptr;
    VideoRecorder* video_recorder = nullptr;
    
    VideoFrameQueue<cv::Mat>* capture_queue = nullptr;
    VideoFrameQueue<FrameData>* detect_queue = nullptr;
    
    std::atomic<bool> is_pushing{false};
};

#endif
