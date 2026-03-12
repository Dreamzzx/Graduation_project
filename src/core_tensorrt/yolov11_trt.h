#ifndef  YOLOV11_TRT_H
#define  YOLOV11_TRT_H

#include <string>
#include <vector>
#include <NvInfer.h>
#include <NvInferRuntime.h>
#include <opencv2/opencv.hpp>

// TensorRT日志记录器，用于捕获TensorRT的日志输出
class Logger : public nvinfer1::ILogger{
public:
    void log(Severity severity, const char* msg) noexcept override
    {
        // 只记录错误和警告信息
        if (severity <= Severity::kWARNING)
        {
            std::cerr << "TensorRT: " << msg << std::endl;
        }
    }
};

struct Detection
{
    float x1,y1,x2,y2; // 预测框坐标
    int classId;       // 预测类别
    float confidence;  // 预测置信度
    std::string className; // 预测类别名称
};


class YOLOv11RT
{
public:
    YOLOv11RT(  const std::string& onnxFilePath, 
                const std::string& engineFilePath,
                int inputWidth, int inputHeight,
                float confThreshold, float nmsThreshold
            );
    
    ~YOLOv11RT();

    bool init();
private:
    bool buildEngine(const std::string& onnxFilePath, 
                    const std::string& engineFilePath);

    bool loadEngine(const std::string& engineFilePath);

    // 图像预处理
    void preprocess(const cv::Mat& image, float* inputData);

    // 图像后处理
    std::vector<Detection> postprocess(const float* outputData, std::vector<cv::Rect>& boxes, std::vector<int>& classIds, std::vector<float>& confidences);
private:
    std::string onnx_path;
    std::string engine_path;
    int input_width; 
    int input_height;
    float conf_threshold; // 置信度阈值
    float nms_threshold;  // NMS阈值

    Logger logger; // TensorRT日志记录器
    nvinfer1::ICudaEngine* engine = nullptr; // tensorRT引擎
    nvinfer1::IExecutionContext* context = nullptr; // 执行上下文

    // GPU缓冲区指针
    void *device_input_buffer = nullptr; // 输入缓冲区
    void *device_output_buffer = nullptr; // 输出缓冲区
    size_t input_buffer_size;  // 输入缓冲区大小
    size_t output_buffer_size; // 输出缓冲区大小
};



#endif  // YOLOV11_TRT_H