#ifndef GLOBAL_CACHE_H
#define GLOBAL_CACHE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

template <typename T>
class VideoFrameQueue
{
public:
    VideoFrameQueue(/* args */) = default;
    ~VideoFrameQueue() = default;

    void Clear(){
        std::lock_guard<std::mutex> lock(mtx);
        std::queue<T> empty;  // 直接清空，Mat 引用计数自动减
        queue.swap(empty);
    }

    void Push(T && frame){
        std::lock_guard<std::mutex> lock(mtx);
        queue.push(std::move(frame)); 
        cv.notify_one(); 
    }

    std::optional<T> Pop(bool& is_running) {
        std::unique_lock<std::mutex> lock(mtx);
        // 等待条件：队列非空 或 停止运行
        cv.wait(lock, [&]() { return !queue.empty() || !is_running; });
        
        if (!is_running && queue.empty()) {
            return std::nullopt; // 停止且无数据，返回空
        }

        T data = std::move(queue.front());
        queue.pop();
        return data;
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size();
    }

private:
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;
};

#endif // GLOBAL_CACHE_H
