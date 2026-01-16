#include "video_capture.h"

VideoCapture::VideoCapture(/* args */)
{

}

VideoCapture::~VideoCapture()
{
}

bool VideoCapture::Init()
{
    avdevice_register_all();

    device_name = "video=HP Wide Vision HD Camera"; // 设备名称，根据实际情况修改
    input_format = av_find_input_format("dshow"); // 使用DirectShow

    format_ctx = avformat_alloc_context();
    if(format_ctx == nullptr) {
        std::cerr << "Failed to allocate format context." << std::endl;
        return false;
    }

    // 设置读取摄像头缓冲区
    AVDictionary* options = nullptr;
    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "framerate", "15", 0);
    av_dict_set(&options, "pixel_format", "yuyv422", 0);

    if (avformat_open_input(&format_ctx, device_name, input_format, &options) < 0) {
        std::cerr << "Failed to open input device." << std::endl;
        return false;
    }

    int ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "Failed to find stream info." << std::endl;
        return false;
    }   

    // 调试信息
    av_dump_format(format_ctx, 0, device_name, 0);

    return true;
}