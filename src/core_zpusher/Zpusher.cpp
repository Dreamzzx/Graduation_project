#include "Zpusher.h"
#include "../video_source/VideoSource.h"
#include "../core_config/Config.h"

ZPusher::ZPusher()
{
}

ZPusher::~ZPusher()
{
    Destroy();
    delete video_source;
    if (yolov11_detector) delete yolov11_detector;
    if (ffmpeg_push) delete ffmpeg_push;
    if (video_recorder) delete video_recorder;
    if (capture_queue) delete capture_queue;
    if (detect_queue) delete detect_queue;
}

void ZPusher::Init()
{
    Config& config = Config::getInstance();
    if (!config.load("config.json")) {
        std::cerr << "[ZPusher] Failed to load config, using defaults" << std::endl;
    }
    
    config.printConfig();
    
    const CaptureConfig& capture_cfg = config.getCaptureConfig();
    const PushConfig& push_cfg = config.getPushConfig();
    const DetectorConfig& detector_cfg = config.getDetectorConfig();
    const RecorderConfig& recorder_cfg = config.getRecorderConfig();
    
    capture_queue = new VideoFrameQueue<cv::Mat>(1);
    detect_queue = new VideoFrameQueue<FrameData>(1);
    
    thread_pool = new ThreadPool();
    
    video_source = new VideoSource(
        capture_cfg.device_name.c_str(), 
        &is_pushing, 
        capture_cfg.video_width,
        capture_cfg.video_height,
        capture_cfg.framerate,
        capture_queue
    );
    
    if (!video_source->Init()) {
        std::cerr << "[Capture Mode]: Init Failed!" << std::endl;
    } else {
        std::cout << "[Capture Mode]: Init Success!" << std::endl;
    }
    
    if (detector_cfg.enabled) {
        yolov11_detector = new YOLOv11Detector();
        if (!yolov11_detector->Init(
            capture_queue,
            detect_queue,
            detector_cfg.model_path,
            detector_cfg.classes,
            detector_cfg.input_width,
            detector_cfg.confidence_threshold,
            0.45f,
            true))
        {
            std::cerr << "[Detection Mode]: Init Failed!" << std::endl;
            delete yolov11_detector;
            yolov11_detector = nullptr;
        } else {
            std::cout << "[Detection Mode]: Init Success!" << std::endl;
        }
    } else {
        std::cout << "[Detection Mode]: Disabled" << std::endl;
    }
    
    ffmpeg_push = new FFmpegPush();
    if (!ffmpeg_push->Init(push_cfg.rtsp_url.c_str(), *detect_queue,
        push_cfg.video_width, push_cfg.video_height, push_cfg.video_bitrate, push_cfg.frame_rate))
    {
        std::cerr << "[Push Mode]: Init Failed!" << std::endl;
    } else {
        std::cout << "[Push Mode]: Init Success!" << std::endl;
    }
    
    video_recorder = new VideoRecorder();
    RecordingConfig recorder_config;
    recorder_config.width = push_cfg.video_width;
    recorder_config.height = push_cfg.video_height;
    recorder_config.fps = push_cfg.frame_rate;
    recorder_config.bitrate = push_cfg.video_bitrate;
    recorder_config.output_dir = recorder_cfg.output_dir;
    recorder_config.person_leave_timeout_ms = recorder_cfg.person_leave_timeout_ms;
    
    if (!recorder_cfg.enabled) {
        std::cout << "[Recorder Mode]: Disabled in config" << std::endl;
        delete video_recorder;
        video_recorder = nullptr;
    } else if (!video_recorder->Init(recorder_config)) {
        std::cerr << "[Recorder Mode]: Init Failed!" << std::endl;
        delete video_recorder;
        video_recorder = nullptr;
    } else {
        std::cout << "[Recorder Mode]: Init Success!" << std::endl;
    }
}

void ZPusher::start_Push()
{
    is_pushing.store(true);
    
    thread_pool->submitTask([this]() {
        video_source->start();
    });
    
    if (yolov11_detector) {
        thread_pool->submitTask([this]() {
            yolov11_detector->start();
        });
    } else {
        thread_pool->submitTask([this]() {
            cv::Size target_size(1920, 1080);
            bool local_running = true;
            while (local_running && is_pushing.load()) {
                auto opt_frame = capture_queue->PopLatestNonBlocking(local_running);
                if (opt_frame) {
                    FrameData frame_data;
                    cv::resize(*opt_frame, frame_data.frame, target_size, 0, 0, cv::INTER_NEAREST);
                    frame_data.has_person = false;
                    detect_queue->Push(std::move(frame_data));
                } else {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        });
    }
    
    ffmpeg_push->start();
    
    thread_pool->submitTask([this]() {
        bool local_running = true;
        while (local_running && is_pushing.load()) {
            auto opt_frame = detect_queue->PopLatestNonBlocking(local_running);
            if (opt_frame) {
                FrameData frame_data = std::move(*opt_frame);
                
                if (video_recorder) {
                    if (frame_data.has_person) {
                        video_recorder->onPersonDetected(frame_data.frame);
                    } else {
                        video_recorder->onPersonLeft();
                    }
                }
                
                ffmpeg_push->pushFrame(std::move(frame_data));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });
    
    thread_pool->start();
    std::cout << "Pushing started." << std::endl;
}

void ZPusher::stop_Push()
{
    is_pushing.store(false);
    if (video_recorder) {
        video_recorder->stop();
    }
    std::cout << "Pushing stopped." << std::endl;
}

void ZPusher::Destroy()
{
    stop_Push();
    if (thread_pool) {
        delete thread_pool;
        thread_pool = nullptr;
    }
}
