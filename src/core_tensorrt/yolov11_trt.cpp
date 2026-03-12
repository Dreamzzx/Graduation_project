#include "yolov11_trt.h"
#include <fstream>
#include <iostream>

YOLOv11RT::YOLOv11RT(const std::string &onnxFilePath,
                     const std::string &engineFilePath,
                     int inputWidth, int inputHeight,
                     float confThreshold, float nmsThreshold)
{
    this->onnx_path = onnxFilePath;
    this->engine_path = engineFilePath;
    this->input_width = inputWidth;
    this->input_height = inputHeight;
    this->conf_threshold = confThreshold;
    this->nms_threshold = nmsThreshold;
}
YOLOv11RT::~YOLOv11RT()
{
}

// 计算张量的体积
int64_t volume(const nvinfer1::Dims &d)
{
    int64_t v = 1;
    for (int i = 0; i < d.nbDims; i++)
        v *= d.d[i];
    return v;
}

bool YOLOv11RT::init()
{
    // 先尝试加载引擎，如果失败则构建引擎
    if (!loadEngine(engine_path))
    {
        std::cout << "Failed to load engine, trying to build it..." << std::endl;
        if (!buildEngine(onnx_path, engine_path))
        {
            std::cerr << "Failed to build engine from ONNX file: " << onnx_path << std::endl;
            return false;
        }
    }

    context = engine->createExecutionContext();
    if (!context)
    {
        std::cerr << "Failed to create execution context for engine: " << engine_path << std::endl;
        return false;
    }
    
    // 获取输出维度
    auto input_tensor= engine->getIOTensorName(0);   //　输入张量名称
    auto output_tensor = engine->getIOTensorName(1); //　输出张量名称

    auto input_dims = engine->getTensorShape(input_tensor); // 输入维度
    auto output_dims = engine->getTensorShape(output_tensor); // 输出维度

    input_buffer_size = volume(input_dims) * sizeof(float); // 输入缓冲区大小
    output_buffer_size = volume(output_dims) * sizeof(float); // 输出缓冲区大小

    cudaMalloc(&device_input_buffer, input_buffer_size); // 输入缓冲区
    cudaMalloc(&device_output_buffer, output_buffer_size); // 输出缓冲区
    
    return true;
}

bool YOLOv11RT::loadEngine(const std::string& engineFilePath)
{
    std::ifstream file(engineFilePath, std::ios::binary);
    if (!file.good())
    {
        std::cerr << "Failed to open engine file: " << engineFilePath << std::endl;
        return false;
    }

    // 读取引擎文件内容到内存
    file.seekg(0, file.end);
    size_t size = file.tellg();
    file.seekg(0, file.beg);
    std::vector<char> engine_data(size);
    file.read(engine_data.data(), size);
    file.close();

    // 反序列化引擎
    nvinfer1::IRuntime* runtime = nvinfer1::createInferRuntime(logger);
    engine = runtime->deserializeCudaEngine(engine_data.data(), size);
    delete runtime;
    runtime = nullptr;

    return !engine ? false : true;
}