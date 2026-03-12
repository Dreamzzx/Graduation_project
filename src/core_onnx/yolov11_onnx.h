#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <VideoFrameQueue.h>
#include <FrameData.h>

class YOLOv11Detector {
public:
    YOLOv11Detector();
    ~YOLOv11Detector();

    bool Init(
        VideoFrameQueue<cv::Mat>* input_queue,
        VideoFrameQueue<FrameData>* output_queue,
        const std::string& model_path,
        const std::vector<std::string>& class_names,
        int input_size = 640,
        float conf_thresh = 0.25f,
        float nms_thresh = 0.45f,
        bool use_gpu = false);

    std::vector<Detection> detect(const cv::Mat& img);
    cv::Mat draw_detections(const cv::Mat& img, const std::vector<Detection>& results);
    void start();

private:
    std::vector<float> preprocess(const cv::Mat& img);
    std::vector<Detection> postprocess(float* output_data, int output_size, int img_width, int img_height);

    Ort::Env env;
    Ort::Session* session = nullptr;
    Ort::MemoryInfo* memory_info = nullptr;
    Ort::SessionOptions session_options;
    std::vector<char*> input_node_names;
    std::vector<char*> output_node_names;
    size_t input_node_num;
    size_t output_node_num;

    int input_size;
    float conf_thresh;
    float nms_thresh;
    std::vector<std::string> class_names;
    float scale;
    int pad_h;
    int pad_w;

    VideoFrameQueue<cv::Mat>* input_queue = nullptr;
    VideoFrameQueue<FrameData>* output_queue = nullptr;
    
    int64_t frame_id_counter = 0;
};
