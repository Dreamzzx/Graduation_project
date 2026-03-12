#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <thread>
#include <vector>
#include <mutex>
#include <iostream>
#include <functional>

class ThreadPool
{
public:
    ThreadPool(/* args */);

    ~ThreadPool();
	
    // 禁止拷贝和移动（线程池不允许拷贝）
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

	template<typename F>
    void submitTask(F&& task)
    {
        std::lock_guard<std::mutex> lock(mtx);
        if (is_running) {
            std::cerr << "ThreadPool is already running. Cannot submit new tasks." << std::endl;
			return;
        }
        tasks.emplace_back(std::forward<F>(task));
	}

    void start();

	void stop();

private:
	std::mutex mtx;
	std::atomic<bool> is_running{ false };
	std::vector<std::thread> threads;
	std::vector<std::function<void()>> tasks;
};



#endif