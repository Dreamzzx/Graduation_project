#ifndef BASE_THREAD_H
#define BASE_THREAD_H   
#include <thread>

class BaseThread
{
public:
    BaseThread(/* args */);
    ~BaseThread();
protected:
    virtual void run() = 0;

    void start() {
        thread_ = std::thread(&BaseThread::run, this);
    }

    void join() {
        if (thread_.joinable()) {
            thread_.join();
        }
    }
private:
    std::thread thread_;
};

#endif // BASE_THREAD_H