#include "Zpusher.h"
#include "../video_source/VideoSource.h"

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
    video_source = new VideoSource(device_name->c_str(),&is_pushing);
    if(!video_source->Init())
    {
        std::cerr << "[采集模块]: 初始化失败!" << std::endl;
    }else{
        std::cout << "[采集模块]: 初始化成功!" << std::endl;
    }

    video_source->start();
}