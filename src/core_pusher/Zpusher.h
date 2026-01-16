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
#include "../core_capture/video_capture.h"

class ZPusher
{
private:
    std::string *device_name;
    std::string rtsp_url;
    std::atomic<bool> is_pushing;
public:
    ZPusher(const char *device_name);
    ~ZPusher();

    void start_Push(); // 开始推流

    void stop_Push(); // 停止推流

private:
    void Init(); // 初始化

    void Destroy(); // 反初始化

    void start(); // 启动推流

private:
    AVFormatContext* fmt_ctx = nullptr;

    VideoCapture* video_capture = nullptr;
};

#endif // ZPUSHER_H