#ifndef VIDEO_FRAME_QUEUE_H
#define VIDEO_FRAME_QUEUE_H

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <memory>
#include <atomic>
#include <chrono>

template <typename T>
class VideoFrameQueue
{
public:
    VideoFrameQueue(size_t max_size = 3) : max_queue_size(max_size) {}
    ~VideoFrameQueue() = default;

    void Clear() {
        std::lock_guard<std::mutex> lock(mtx);
        std::queue<T> empty;
        queue.swap(empty);
    }

    void Push(T&& frame) {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue.size() >= max_queue_size) {
            queue.pop();
        }
        queue.push(std::move(frame));
        cv.notify_all();
    }

    void Push(const T& frame) {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue.size() >= max_queue_size) {
            queue.pop();
        }
        queue.push(frame);
        cv.notify_all();
    }

    std::optional<T> Pop(bool& is_running) {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return !queue.empty() || !is_running; });

        if (!is_running && queue.empty()) {
            return std::nullopt;
        }

        T data = std::move(queue.front());
        queue.pop();
        return data;
    }

    std::optional<T> TryPop() {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue.empty()) {
            return std::nullopt;
        }
        T data = std::move(queue.front());
        queue.pop();
        return data;
    }

    std::optional<T> PeekLatest() {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue.empty()) {
            return std::nullopt;
        }
        return queue.back();
    }

    std::optional<T> PopLatest() {
        std::lock_guard<std::mutex> lock(mtx);
        if (queue.empty()) {
            return std::nullopt;
        }
        T data = std::move(queue.back());
        while (!queue.empty()) {
            queue.pop();
        }
        return data;
    }

    size_t Size() {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.size();
    }

    bool Empty() {
        std::lock_guard<std::mutex> lock(mtx);
        return queue.empty();
    }

    size_t GetMaxSize() const {
        return max_queue_size;
    }

    void SetMaxSize(size_t new_size) {
        std::lock_guard<std::mutex> lock(mtx);
        max_queue_size = new_size;
        while (queue.size() > max_queue_size) {
            queue.pop();
        }
    }

private:
    std::queue<T> queue;
    std::mutex mtx;
    std::condition_variable cv;
    size_t max_queue_size;
};

#endif
