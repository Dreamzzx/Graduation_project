#ifndef CONFIG_H
#define CONFIG_H

#include <string>
#include <vector>

struct PushConfig {
    std::string rtsp_url;
    int video_width = 1920;
    int video_height = 1080;
    int video_bitrate = 2000000;
    int frame_rate = 15;
};

struct CaptureConfig {
    std::string device_name;
    std::string input_format;
    int video_width = 640;
    int video_height = 360;
    int framerate = 15;
    std::vector<std::string> options;
};

struct DetectorConfig {
    bool enabled = false;
    std::string model_path;
    std::vector<std::string> classes;
    float confidence_threshold = 0.5f;
    int input_width = 640;
    int input_height = 640;
};

struct RecorderConfig {
    bool enabled = true;
    std::string output_dir = "recordings";
    int person_leave_timeout_ms = 3000;
};

struct LogConfig {
    std::string level = "info";
    bool debug_enabled = false;
};

class Config {
public:
    static Config& getInstance();
    bool load(const std::string& config_path);
    
    const PushConfig& getPushConfig() const { return push_config_; }
    const CaptureConfig& getCaptureConfig() const { return capture_config_; }
    const DetectorConfig& getDetectorConfig() const { return detector_config_; }
    const RecorderConfig& getRecorderConfig() const { return recorder_config_; }
    const LogConfig& getLogConfig() const { return log_config_; }
    
    void printConfig() const;

private:
    Config();
    ~Config();
    Config(const Config&) = delete;
    Config& operator=(const Config&) = delete;

    PushConfig push_config_;
    CaptureConfig capture_config_;
    DetectorConfig detector_config_;
    RecorderConfig recorder_config_;
    LogConfig log_config_;
};

#endif
