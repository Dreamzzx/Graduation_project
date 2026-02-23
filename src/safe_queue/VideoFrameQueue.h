#ifndef GLOBAL_CACHE_H
#define GLOBAL_CACHE_H

#include <queue>

class VideoFrameQueue
{
private:
    std::queue<void*> frame_queue;
public:
    VideoFrameQueue(/* args */);
    ~VideoFrameQueue();
};


#endif // GLOBAL_CACHE_H
