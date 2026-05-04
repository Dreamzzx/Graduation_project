#include "yolov11_onnx.h"
#include <string>
#include <iostream>
#include <stdexcept>
#define NOMINMAX
#include <Windows.h>
#include "C:/cuda12.3/include/cuda_runtime.h"

static const std::vector<std::string> COCO_CLASSES = {
    "person", "bicycle", "car", "motorcycle", "airplane",
    "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird",
    "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack",
    "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat",
    "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
    "wine glass", "cup", "fork", "knife", "spoon",
    "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut",
    "cake", "chair", "couch", "potted plant", "bed",
    "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven",
    "toaster", "sink", "refrigerator", "book", "clock",
    "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

std::wstring string_to_wstring(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
    std::wstring result(size - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, &result[0], size);
    return result;
}

bool checkCudaCompatibility() {
    int device_count = 0;
    cudaError_t cuda_err = cudaGetDeviceCount(&device_count);
    if (cuda_err != cudaSuccess || device_count == 0) {
        return false;
    }

    cudaDeviceProp prop;
    cuda_err = cudaGetDeviceProperties(&prop, 0);
    if (cuda_err != cudaSuccess) {
        return false;
    }
    if (prop.major < 7) {
        return false;
    }

    auto available_providers = Ort::GetAvailableProviders();
    bool has_cuda = false;
    for (const auto& p : available_providers) {
        if (p == "CUDAExecutionProvider") {
            has_cuda = true;
            break;
        }
    }
    if (!has_cuda) {
        return false;
    }

    return true;
}

YOLOv11Detector::YOLOv11Detector()
{
    checkCudaCompatibility();
}

YOLOv11Detector::~YOLOv11Detector()
{
    if (session) {
        delete session;
        session = nullptr;
    }
    if (memory_info) {
        delete memory_info;
        memory_info = nullptr;
    }
    for (auto name : input_node_names) {
        free(name);
    }
    for (auto name : output_node_names) {
        free(name);
    }
}

bool YOLOv11Detector::Init(
    VideoFrameQueue<cv::Mat>* input_queue,
    VideoFrameQueue<FrameData>* output_queue,
    const std::string& model_path,
    const std::vector<std::string>& class_names,
    int input_size,
    float conf_thresh,
    float nms_thresh,
    bool use_gpu)
{
    this->class_names = class_names;
    this->input_size = input_size;
    this->conf_thresh = conf_thresh;
    this->nms_thresh = nms_thresh;
    this->input_queue = input_queue;
    this->output_queue = output_queue;
    
    target_class_ids.clear();
    for (const auto& cls : class_names) {
        for (size_t i = 0; i < COCO_CLASSES.size(); i++) {
            if (cls == COCO_CLASSES[i]) {
                target_class_ids.insert(static_cast<int>(i));
                break;
            }
        }
    }
    
    if (target_class_ids.empty()) {
        std::cerr << "[Detector] Warning: no valid target classes found, detecting all classes" << std::endl;
        for (size_t i = 0; i < COCO_CLASSES.size(); i++) {
            target_class_ids.insert(static_cast<int>(i));
        }
    }
    
    std::cout << "[Detector] Target classes: ";
    for (const auto& cls : class_names) {
        std::cout << cls << " ";
    }
    std::cout << std::endl;
    
    std::string full_model_path = model_path;
    if (model_path.find(':') == std::string::npos) {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string exe_path(buffer);
        std::string exe_dir = exe_path.substr(0, exe_path.find_last_of('\\'));
        full_model_path = exe_dir + "\\..\\..\\..\\" + model_path;
    }
    
    std::cout << "Model path: " << full_model_path << std::endl;
    
    env = Ort::Env(OrtLoggingLevel::ORT_LOGGING_LEVEL_WARNING, "YOLOv11");
    session_options = Ort::SessionOptions();

    session_options.SetIntraOpNumThreads(1);
    session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);

    if (!use_gpu) {
        std::cout << "Using CPU for inference." << std::endl;
    }
    else {
        int deviceCount = 0;
        cudaError_t err = cudaGetDeviceCount(&deviceCount);
        std::cout << "CUDA devices found: " << deviceCount << std::endl;
        for (int i = 0; i < deviceCount; i++) {
            cudaDeviceProp prop;
            cudaGetDeviceProperties(&prop, i);
            std::cout << "Device " << i << ": " << prop.name << std::endl;
        }
        if (err != cudaSuccess || deviceCount == 0) {
            std::cerr << "CUDA not available: " << cudaGetErrorString(err) << std::endl;
            use_gpu = false;
        }
        else {
            OrtCUDAProviderOptions cuda_options;
            cuda_options.device_id = 0;
            cuda_options.gpu_mem_limit = 3ULL * 1024 * 1024 * 1024;
            cuda_options.arena_extend_strategy = 0;
            cuda_options.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchHeuristic;
            cuda_options.do_copy_in_default_stream = 1;

            try {
                session_options.AppendExecutionProvider_CUDA(cuda_options);
                std::cout << "Using GPU for inference." << std::endl;
            }
            catch (const Ort::Exception& e) {
                std::cerr << "Failed to enable CUDA: " << e.what() << std::endl;
                use_gpu = false;
            }
        }
    }

    std::wstring w_model_path = string_to_wstring(full_model_path);

    try {
        std::cout << "Loading model from: " << full_model_path << std::endl;
        session = new Ort::Session(env, w_model_path.c_str(), session_options);
        std::cout << "Model loaded successfully!" << std::endl;
    }
    catch (const Ort::Exception& e) {
        std::cerr << "[ONNX Runtime Error] Code: " << static_cast<int>(e.GetOrtErrorCode()) << std::endl;
        std::cerr << "[ONNX Runtime Error] Msg: " << e.what() << std::endl;
        return false;
    }
    catch (const std::exception& e) {
        std::cerr << "[General Error] " << e.what() << std::endl;
        return false;
    }
    catch (...) {
        std::cerr << "[Unknown Error] Failed to load model!" << std::endl;
        return false;
    }

    Ort::AllocatorWithDefaultOptions allocator;
    input_node_names.clear();
    output_node_names.clear();

    input_node_num = session->GetInputCount();
    output_node_num = session->GetOutputCount();

    for (size_t i = 0; i < input_node_num; i++)
    {
        auto input_name_ptr = session->GetInputNameAllocated(i, allocator);
        if (input_name_ptr) {
            char* name_copy = _strdup(input_name_ptr.get());
            input_node_names.push_back(name_copy);
            std::cout << "Input node " << i << ": " << name_copy << std::endl;
        }
    }

    for (size_t i = 0; i < output_node_num; i++)
    {
        auto output_name_ptr = session->GetOutputNameAllocated(i, allocator);
        if (output_name_ptr) {
            char* name_copy = _strdup(output_name_ptr.get());
            output_node_names.push_back(name_copy);
            std::cout << "Output node " << i << ": " << name_copy << std::endl;
        }
    }

    memory_info = new Ort::MemoryInfo(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault));

    std::cout << "Yolov11 Onnx Model loaded successfully: " << model_path << std::endl;
    return true;
}

std::vector<Detection> YOLOv11Detector::detect(const cv::Mat& image)
{
    if (image.empty())
    {
        std::cerr << "Input image is empty!" << std::endl;
        return std::vector<Detection>();
    }

    if (!memory_info || !session) {
        std::cerr << "Detector not initialized!" << std::endl;
        return std::vector<Detection>();
    }

    std::vector<float> input_tensor_values = preprocess(image);

    std::array<int64_t, 4> input_shape = { 1, 3, input_size, input_size };

    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
        *memory_info,
        input_tensor_values.data(),
        input_tensor_values.size(),
        input_shape.data(),
        input_shape.size());

    Ort::RunOptions run_options;
    auto output_tensors = session->Run(run_options,
        input_node_names.data(),
        &input_tensor, 1,
        output_node_names.data(),
        output_node_names.size());

    Ort::TensorTypeAndShapeInfo output_info = output_tensors[0].GetTensorTypeAndShapeInfo();
    size_t output_size = output_info.GetElementCount();

    float* output_data = output_tensors[0].GetTensorMutableData<float>();

    auto results = postprocess(output_data, static_cast<int>(output_size), image.cols, image.rows);

    return results;
}

cv::Mat YOLOv11Detector::draw_detections(const cv::Mat& img, const std::vector<Detection>& results)
{
    cv::Mat draw_img = img;
    
    if (!results.empty()) {
        int padding = 20;
        int box_height = 50;
        int box_width = 320;
        
        cv::rectangle(draw_img, 
            cv::Point(padding - 2, padding - 2), 
            cv::Point(padding + box_width + 2, padding + box_height + 2),
            cv::Scalar(0, 0, 200), -1);
        
        cv::rectangle(draw_img, 
            cv::Point(padding, padding), 
            cv::Point(padding + box_width, padding + box_height),
            cv::Scalar(0, 0, 255), -1);
        
        std::string alert_text = "ALERT: TARGET DETECTED!";
        int font_face = cv::FONT_HERSHEY_SIMPLEX;
        double font_scale = 0.8;
        int thickness = 2;
        
        cv::Size text_size = cv::getTextSize(alert_text, font_face, font_scale, thickness, nullptr);
        int text_x = padding + (box_width - text_size.width) / 2;
        int text_y = padding + (box_height + text_size.height) / 2 - 5;
        
        cv::putText(draw_img, alert_text, cv::Point(text_x + 1, text_y + 1),
            font_face, font_scale, cv::Scalar(0, 0, 0), thickness);
        
        cv::putText(draw_img, alert_text, cv::Point(text_x, text_y),
            font_face, font_scale, cv::Scalar(255, 255, 255), thickness);
    }

    std::vector<cv::Scalar> colors = {
        cv::Scalar(0, 255, 0),
        cv::Scalar(255, 0, 0),
        cv::Scalar(0, 0, 255),
        cv::Scalar(255, 255, 0),
        cv::Scalar(255, 0, 255),
        cv::Scalar(0, 255, 255)
    };

    for (const auto& res : results) {
        cv::Scalar color = colors[res.class_id % colors.size()];
        cv::rectangle(draw_img, res.bbox, color, 2);

        std::string label = res.class_name + " " + std::to_string(static_cast<int>(res.confidence * 100)) + "%";
        double label_font_scale = 0.6;
        int label_thickness = 2;
        int baseline = 0;
        cv::Size text_size = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, label_font_scale, label_thickness, &baseline);

        cv::Rect text_rect(res.bbox.x, res.bbox.y - text_size.height - 5,
            text_size.width, text_size.height + 5);
        cv::rectangle(draw_img, text_rect, color, -1);

        cv::putText(draw_img, label, cv::Point(res.bbox.x, res.bbox.y - 5),
            cv::FONT_HERSHEY_SIMPLEX, label_font_scale, cv::Scalar(255, 255, 255), label_thickness);
    }

    return draw_img;
}

std::vector<float> YOLOv11Detector::preprocess(const cv::Mat& img)
{
    int img_h = img.rows;
    int img_w = img.cols;

    scale = (std::min)(static_cast<float>(input_size) / img_w, static_cast<float>(input_size) / img_h);
    int new_w = static_cast<int>(img_w * scale);
    int new_h = static_cast<int>(img_h * scale);

    cv::Mat resized_img;
    cv::resize(img, resized_img, cv::Size(new_w, new_h), 0, 0, cv::INTER_NEAREST);

    pad_w = (input_size - new_w) / 2;
    pad_h = (input_size - new_h) / 2;

    cv::Mat padded_img;
    cv::copyMakeBorder(resized_img, padded_img, pad_h, input_size - new_h - pad_h,
        pad_w, input_size - new_w - pad_w, cv::BORDER_CONSTANT,
        cv::Scalar(114, 114, 114));

    padded_img.convertTo(padded_img, CV_32FC3, 1.0 / 255.0);

    std::vector<float> input_data(input_size * input_size * 3);
    
    std::vector<cv::Mat> channels(3);
    cv::split(padded_img, channels);
    
    for (int c = 0; c < 3; ++c) {
        float* channel_data = channels[c].ptr<float>();
        for (int i = 0; i < input_size * input_size; ++i) {
            input_data[c * input_size * input_size + i] = channel_data[i];
        }
    }
    
    return input_data;
}

std::vector<Detection> YOLOv11Detector::postprocess(float* output_data, int output_size, int img_width, int img_height)
{
    std::vector<Detection> results;

    int num_channels = output_size / 8400;
    int num_boxes = 8400;

    std::vector<cv::Rect> boxes;
    std::vector<float> confidences;
    std::vector<int> class_ids;

    for (int i = 0; i < num_boxes; ++i) {
        float x = output_data[i];
        float y = output_data[i + num_boxes];
        float w = output_data[i + 2 * num_boxes];
        float h = output_data[i + 3 * num_boxes];

        float max_conf = 0.0f;
        int cls_id = 0;
        for (int j = 4; j < num_channels; ++j) {
            float cls_conf = output_data[i + j * num_boxes];
            if (cls_conf > max_conf) {
                max_conf = cls_conf;
                cls_id = j - 4;
            }
        }

        if (max_conf < conf_thresh) {
            continue;
        }

        if (target_class_ids.find(cls_id) == target_class_ids.end()) {
            continue;
        }

        float x_center = (x - pad_w) / scale;
        float y_center = (y - pad_h) / scale;
        float box_w = w / scale;
        float box_h = h / scale;

        int x1 = static_cast<int>((std::max)(0.0f, x_center - box_w / 2));
        int y1 = static_cast<int>((std::max)(0.0f, y_center - box_h / 2));
        int x2 = static_cast<int>((std::min)((float)img_width, x_center + box_w / 2));
        int y2 = static_cast<int>((std::min)((float)img_height, y_center + box_h / 2));

        if (x2 - x1 <= 0 || y2 - y1 <= 0) {
            continue;
        }

        boxes.push_back(cv::Rect(x1, y1, x2 - x1, y2 - y1));
        confidences.push_back(max_conf);
        class_ids.push_back(cls_id);
    }

    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, conf_thresh, nms_thresh, indices);

    for (int idx : indices) {
        Detection res;
        res.bbox = boxes[idx];
        res.confidence = confidences[idx];
        res.class_id = class_ids[idx];
        if (class_ids[idx] >= 0 && class_ids[idx] < static_cast<int>(COCO_CLASSES.size())) {
            res.class_name = COCO_CLASSES[class_ids[idx]];
        } else {
            res.class_name = "unknown";
        }
        results.push_back(res);
    }
    return results;
}

void YOLOv11Detector::start() {
    bool is_run = true;
    cv::Size target_size(1920, 1080);
    while (is_run) {
        auto opt_frame = input_queue->PopLatestNonBlocking(is_run);
        if (opt_frame) {
            if (!opt_frame->empty() && opt_frame->rows > 0 && opt_frame->cols > 0) {
                std::vector<Detection> results = detect(*opt_frame);
                cv::Mat output_frame = draw_detections(*opt_frame, results);
                
                cv::Mat resized_frame;
                cv::resize(output_frame, resized_frame, target_size, 0, 0, cv::INTER_NEAREST);
                
                FrameData frame_data;
                frame_data.frame = std::move(resized_frame);
                frame_data.detections = std::move(results);
                frame_data.has_person = !frame_data.detections.empty();
                frame_data.frame_id = frame_id_counter++;
                
                output_queue->Push(std::move(frame_data));
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}
