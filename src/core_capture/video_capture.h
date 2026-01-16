#ifndef CAPTURE_VIDEO_H
#define CAPTURE_VIDEO_H

#include <iostream>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
}


class VideoCapture
{
private:
    /* data */
public:
    VideoCapture(/* args */);
    ~VideoCapture();

    bool Init();
private:

private:
    const char *device_name;

    const AVInputFormat *input_format = nullptr;

    AVFormatContext *format_ctx = nullptr;
};
#endif // CAPTURE_VIDEO_H