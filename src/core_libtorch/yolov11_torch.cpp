#include "yolov11_torch.h"

YOLOv11Detector::YOLOv11Detector(const std::string &model_path,
                                 const std::vector<std::string> &class_names,
                                 int input_size,
                                 float conf_thresh,
                                 float nms_thresh,
                                 bool use_gpu) : class_names(class_names), input_size(input_size),
                                                 conf_thresh(conf_thresh), nms_thresh(nms_thresh),
                                                 use_gpu(use_gpu && torch::cuda::is_available()),
                                                 device(use_gpu ? torch::kCUDA : torch::kCPU)
{
    try
    {
        model = torch::jit::load(model_path, device);
        model.eval(); // 设置模型为评估模式
        std::cout << "[YOLOv11Detector]: Model loaded successfully on " << (use_gpu ? "GPU" : "CPU") << std::endl;

        torch::jit::optimize_for_inference(model); // 优化模型以提高推理速度

        torch::Tensor dummy_input = torch::randn({1, 3, input_size, input_size}, device).to(torch::kFloat32);
        model.forward({dummy_input});
    }
    catch (const torch::Error &e)
    {
        std::cerr << "model loading error: " << e.what() << '\n';
    }
}