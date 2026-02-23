#ifndef CAPTURE_VIDEO_H
#define CAPTURE_VIDEO_H

#include <iostream>
#include <atomic>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
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

private:
    const char *device_name;

    const AVInputFormat *input_format = nullptr;

    AVFormatContext *format_ctx = nullptr;
    AVCodecContext *codec_ctx = nullptr;

    std::atomic<bool>* is_running;

    int video_stream_index;

};
#endif // CAPTURE_VIDEO_H