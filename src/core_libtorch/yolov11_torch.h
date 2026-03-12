#ifndef YOLOV11_TORCH_H
#define YOLOV11_TORCH_H

#include <torch/script.h>
#include <torch/torch.h>
#include <vector>
#include <opencv2/opencv.hpp>

struct Detection {
    int class_id;
    std::string class_name;
    float confidence;
    float x1, y1, x2, y2; // Bounding box coordinates
};

class YOLOv11Detector {
public:
    YOLOv11Detector(const std::string& model_path,
                    const std::vector<std::string>& class_names,
                    int input_size = 640,
                    float conf_thresh = 0.25f,
                    float nms_thresh = 0.45f,
                    bool use_gpu = true);

    std::vector<Detection> detect(const cv::Mat& image);
private:
    torch::jit::script::Module model; // 加载的模型
    torch::Device device; // 设备（CPU或GPU）
    std::vector<std::string> class_names; // 类别名称列表
    int input_size; // 输入图像尺寸
    float conf_thresh; // 置信度阈值
    float nms_thresh; // NMS阈值
    bool use_gpu;
};

#endif // YOLOV11_TORCH_H