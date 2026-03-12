#include "threadPool.h"

ThreadPool::ThreadPool(/* args */)
{
}

ThreadPool::~ThreadPool()
{
}

void ThreadPool::start()
{
	std::lock_guard<std::mutex> lock(mtx);
	if (is_running) {
		std::cerr << "ThreadPool is already running." << std::endl;
		return;
	}
	is_running = true;
	for (auto& task : tasks) {
		threads.emplace_back(std::thread(task));
	}
}

void ThreadPool::stop()
{
	std::lock_guard<std::mutex> lock(mtx);
	if (!is_running) {
		std::cerr << "ThreadPool is not running." << std::endl;
		return;
	}
	is_running = false;
	for (auto& thread : threads) {
		if (thread.joinable()) {
			thread.join();
		}
	}
	threads.clear();
	tasks.clear();
}