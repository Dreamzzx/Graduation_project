#include "Zpusher.h"
#include "../core_capture/video_capture.h"

ZPusher::ZPusher(const char *device_name)
{
    this->device_name = new std::string(device_name);
    Init();
}

ZPusher::~ZPusher()
{
}

void ZPusher::Init() // 初始化
{   
    // 初始化视频捕获模块
    video_capture = new VideoCapture();
    if(!video_capture->Init())
    {
        std::cerr << "[采集模块]: 初始化失败!" << std::endl;
    }else{
        std::cout << "[采集模块]: 初始化成功!" << std::endl;
    }

}