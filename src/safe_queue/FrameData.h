#ifndef FRAME_DATA_H
#define FRAME_DATA_H

#include <opencv2/opencv.hpp>
#include <vector>
#include <chrono>

struct Detection {
    cv::Rect bbox;
    float confidence;
    int class_id;
    std::string class_name;
};

struct FrameData {
    cv::Mat frame;
    std::vector<Detection> detections;
    int64_t timestamp_ms = 0;
    int64_t frame_id = 0;
    bool has_person = false;
    
    FrameData() = default;
    
    FrameData(const cv::Mat& f, int64_t id)
        : frame(f.clone()), frame_id(id) {
        timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    
    FrameData(cv::Mat&& f, int64_t id)
        : frame(std::move(f)), frame_id(id) {
        timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

#endif
