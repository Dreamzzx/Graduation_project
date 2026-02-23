#include <string>
#include <atomic>
#include <mutex>
#include <cstdlib>
#include <cstdint>
#include <memory>
#ifdef _WIN32
#include <malloc.h>
#else
#include <stdlib.h>
#endif

enum class PixFormatType
{
    FORMAT_YUV420P,
    FORMAT_NV12,
    FORMAT_BGR24,
    FORMAT_UNKNOWN
};

struct VideoFrame
{
    uint8_t* data = nullptr;
    int width = 0; 
    int height = 0;
    PixFormatType format = PixFormatType::FORMAT_UNKNOWN;
    int64_t pts = 0; 
    int stride = 0;

    std::atomic<int> ref_count{0}; // 引用计数
    std::mutex ref_mutex;

    VideoFrame(int w,int h,PixFormatType type):width(w),height(h),format(type)
    {
        int buffer_size = 0;
        switch(format)
        {
            case PixFormatType::FORMAT_YUV420P:
                buffer_size = width * height * 3 / 2;
                break;
            case PixFormatType::FORMAT_NV12:
                buffer_size = width * height * 3 / 2;
                break;
            case PixFormatType::FORMAT_BGR24:
                buffer_size = width * height * 3;
                break;
            default:
                buffer_size = 0;
                break;
        }

        if(buffer_size > 0){
        #ifdef _WIN32
                data = (uint8_t*)_aligned_malloc(buffer_size, 32);
        #else
                data = (uint8_t*)aligned_alloc(32, buffer_size);
        #endif
        }
        stride = width; 
        ref_count = 1;
    }
    
    ~VideoFrame()
    {
        if(data){
        #ifdef _WIN32
            _aligned_free(data);
        #else
            free(data);
        #endif
            data = nullptr;
        }
    }

    void add_ref()
    {
        std::lock_guard<std::mutex> lock(ref_mutex);
        ref_count++;
    }

    void release()
    {
        std::lock_guard<std::mutex> lock(ref_mutex);
        ref_count--;
        if(ref_count == 0)
        {
            delete this;
        }
    }

    // 禁止拷贝构造函数和赋值操作符
    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;

    VideoFrame(VideoFrame && other) noexcept
        : data(other.data), width(other.width), height(other.height),
          format(other.format), pts(other.pts), stride(other.stride), ref_count(other.ref_count.load())
    {
        other.data = nullptr;
        other.width = 0;
        other.height = 0;
        other.format = PixFormatType::FORMAT_UNKNOWN;
        other.pts = 0;
        other.stride = 0;
        other.ref_count = 0;
    }
    
};
