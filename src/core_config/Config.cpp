#include "Config.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <Windows.h>

Config::Config() {
}

Config::~Config() {
}

Config& Config::getInstance() {
    static Config instance;
    return instance;
}

bool Config::load(const std::string& config_path) {
    std::string full_path = config_path;
    if (config_path.find(':') == std::string::npos) {
        char buffer[MAX_PATH];
        GetModuleFileNameA(NULL, buffer, MAX_PATH);
        std::string exe_path(buffer);
        std::string exe_dir = exe_path.substr(0, exe_path.find_last_of('\\'));
        full_path = exe_dir + "\\..\\..\\..\\" + config_path;
    }
    
    std::ifstream config_file(full_path);
    if (!config_file.is_open()) {
        std::cerr << "[Config] Failed to open config file: " << full_path << std::endl;
        return false;
    }
    
    std::string content((std::istreambuf_iterator<char>(config_file)), 
                         std::istreambuf_iterator<char>());
    config_file.close();
    
    std::cout << "[Config] Parsing config file: " << full_path << std::endl;
    
    auto findSectionEnd = [&content](size_t start) {
        int brace_count = 0;
        size_t pos = start;
        while (pos < content.size()) {
            if (content[pos] == '{') brace_count++;
            else if (content[pos] == '}') {
                brace_count--;
                if (brace_count == 0) return pos;
            }
            pos++;
        }
        return content.size();
    };
    
    auto parseValueInSection = [&content](const std::string& key, std::string& out, size_t section_start, size_t section_end) {
        std::string search_str = "\"" + key + "\"";
        size_t pos = content.find(search_str, section_start);
        if (pos != std::string::npos && pos < section_end) {
            size_t colon = content.find(':', pos);
            if (colon != std::string::npos && colon < section_end) {
                size_t start = content.find('"', colon);
                if (start != std::string::npos && start < section_end) {
                    size_t end = content.find('"', start + 1);
                    if (end != std::string::npos && end < section_end) {
                        out = content.substr(start + 1, end - start - 1);
                        return true;
                    }
                }
            }
        }
        return false;
    };
    
    auto parseIntInSection = [&content](const std::string& key, int& out, size_t section_start, size_t section_end) {
        std::string search_str = "\"" + key + "\"";
        size_t pos = content.find(search_str, section_start);
        if (pos != std::string::npos && pos < section_end) {
            size_t colon = content.find(':', pos);
            if (colon != std::string::npos && colon < section_end) {
                size_t start = colon + 1;
                while (start < content.size() && (content[start] == ' ' || content[start] == '\t' || content[start] == '\n')) start++;
                size_t end = start;
                while (end < content.size() && (content[end] >= '0' && content[end] <= '9')) end++;
                if (end > start && end <= section_end) {
                    out = std::stoi(content.substr(start, end - start));
                    return true;
                }
            }
        }
        return false;
    };
    
    auto parseBoolInSection = [&content](const std::string& key, bool& out, size_t section_start, size_t section_end) {
        std::string search_str = "\"" + key + "\"";
        size_t pos = content.find(search_str, section_start);
        if (pos != std::string::npos && pos < section_end) {
            size_t colon = content.find(':', pos);
            if (colon != std::string::npos && colon < section_end) {
                size_t start = colon + 1;
                while (start < content.size() && (content[start] == ' ' || content[start] == '\t' || content[start] == '\n')) start++;
                if (start + 4 <= section_end && content.substr(start, 4) == "true") { out = true; return true; }
                if (start + 5 <= section_end && content.substr(start, 5) == "false") { out = false; return true; }
            }
        }
        return false;
    };
    
    auto parseFloatInSection = [&content](const std::string& key, float& out, size_t section_start, size_t section_end) {
        std::string search_str = "\"" + key + "\"";
        size_t pos = content.find(search_str, section_start);
        if (pos != std::string::npos && pos < section_end) {
            size_t colon = content.find(':', pos);
            if (colon != std::string::npos && colon < section_end) {
                size_t start = colon + 1;
                while (start < content.size() && (content[start] == ' ' || content[start] == '\t' || content[start] == '\n')) start++;
                size_t end = start;
                while (end < content.size() && ((content[end] >= '0' && content[end] <= '9') || content[end] == '.')) end++;
                if (end > start && end <= section_end) {
                    out = std::stof(content.substr(start, end - start));
                    return true;
                }
            }
        }
        return false;
    };
    
    auto parseStringArrayInSection = [&content](const std::string& key, std::vector<std::string>& out, size_t section_start, size_t section_end) {
        std::string search_str = "\"" + key + "\"";
        size_t pos = content.find(search_str, section_start);
        if (pos != std::string::npos && pos < section_end) {
            size_t colon = content.find(':', pos);
            if (colon != std::string::npos && colon < section_end) {
                size_t start = content.find('[', colon);
                size_t end = content.find(']', start);
                if (start != std::string::npos && end != std::string::npos && end <= section_end) {
                    std::string arr = content.substr(start + 1, end - start - 1);
                    size_t i = 0;
                    while (i < arr.size()) {
                        size_t q1 = arr.find('"', i);
                        if (q1 == std::string::npos) break;
                        size_t q2 = arr.find('"', q1 + 1);
                        if (q2 == std::string::npos) break;
                        out.push_back(arr.substr(q1 + 1, q2 - q1 - 1));
                        i = q2 + 1;
                    }
                    return true;
                }
            }
        }
        return false;
    };
    
    // Parse push section
    size_t pushPos = content.find("\"push\"");
    if (pushPos != std::string::npos) {
        size_t brace_start = content.find('{', pushPos);
        size_t brace_end = findSectionEnd(brace_start);
        parseValueInSection("rtsp_url", push_config_.rtsp_url, brace_start, brace_end);
        parseIntInSection("video_width", push_config_.video_width, brace_start, brace_end);
        parseIntInSection("video_height", push_config_.video_height, brace_start, brace_end);
        parseIntInSection("video_bitrate", push_config_.video_bitrate, brace_start, brace_end);
        parseIntInSection("frame_rate", push_config_.frame_rate, brace_start, brace_end);
    }
    
    // Parse capture section
    size_t capturePos = content.find("\"capture\"");
    if (capturePos != std::string::npos) {
        size_t brace_start = content.find('{', capturePos);
        size_t brace_end = findSectionEnd(brace_start);
        parseValueInSection("device_name", capture_config_.device_name, brace_start, brace_end);
        parseValueInSection("input_format", capture_config_.input_format, brace_start, brace_end);
        parseIntInSection("video_width", capture_config_.video_width, brace_start, brace_end);
        parseIntInSection("video_height", capture_config_.video_height, brace_start, brace_end);
        parseIntInSection("framerate", capture_config_.framerate, brace_start, brace_end);
    }
    
    // Parse detector section
    size_t detectorPos = content.find("\"detector\"");
    if (detectorPos != std::string::npos) {
        size_t brace_start = content.find('{', detectorPos);
        size_t brace_end = findSectionEnd(brace_start);
        parseBoolInSection("enabled", detector_config_.enabled, brace_start, brace_end);
        parseValueInSection("model_path", detector_config_.model_path, brace_start, brace_end);
        parseFloatInSection("confidence_threshold", detector_config_.confidence_threshold, brace_start, brace_end);
        parseIntInSection("input_width", detector_config_.input_width, brace_start, brace_end);
        parseIntInSection("input_height", detector_config_.input_height, brace_start, brace_end);
        parseStringArrayInSection("classes", detector_config_.classes, brace_start, brace_end);
    }
    
    // Parse recorder section
    size_t recorderPos = content.find("\"recorder\"");
    if (recorderPos != std::string::npos) {
        size_t brace_start = content.find('{', recorderPos);
        size_t brace_end = findSectionEnd(brace_start);
        parseBoolInSection("enabled", recorder_config_.enabled, brace_start, brace_end);
        parseValueInSection("output_dir", recorder_config_.output_dir, brace_start, brace_end);
        parseIntInSection("person_leave_timeout_ms", recorder_config_.person_leave_timeout_ms, brace_start, brace_end);
    }
    
    // Parse log section
    size_t logPos = content.find("\"log\"");
    if (logPos != std::string::npos) {
        size_t brace_start = content.find('{', logPos);
        size_t brace_end = findSectionEnd(brace_start);
        parseValueInSection("level", log_config_.level, brace_start, brace_end);
        parseBoolInSection("debug_enabled", log_config_.debug_enabled, brace_start, brace_end);
    }
    
    std::cout << "[Config] Loaded successfully from " << full_path << std::endl;
    return true;
}

void Config::printConfig() const {
    std::cout << "========== Configuration ==========" << std::endl;
    std::cout << "[Push]" << std::endl;
    std::cout << "  RTSP URL: " << push_config_.rtsp_url << std::endl;
    std::cout << "  Video: " << push_config_.video_width << "x" << push_config_.video_height 
              << ", Bitrate: " << push_config_.video_bitrate 
              << ", FPS: " << push_config_.frame_rate << std::endl;
    std::cout << "[Capture]" << std::endl;
    std::cout << "  Device: " << capture_config_.device_name << std::endl;
    std::cout << "  Video: " << capture_config_.video_width << "x" << capture_config_.video_height 
              << ", FPS: " << capture_config_.framerate << std::endl;
    std::cout << "  Input Format: " << capture_config_.input_format << std::endl;
    std::cout << "[Detector]" << std::endl;
    std::cout << "  Enabled: " << (detector_config_.enabled ? "YES" : "NO") << std::endl;
    std::cout << "  Model: " << detector_config_.model_path << std::endl;
    std::cout << "  Confidence: " << detector_config_.confidence_threshold << std::endl;
    std::cout << "  Input: " << detector_config_.input_width << "x" << detector_config_.input_height << std::endl;
    std::cout << "[Recorder]" << std::endl;
    std::cout << "  Enabled: " << (recorder_config_.enabled ? "YES" : "NO") << std::endl;
    std::cout << "  Output Dir: " << recorder_config_.output_dir << std::endl;
    std::cout << "  Person Leave Timeout: " << recorder_config_.person_leave_timeout_ms << "ms" << std::endl;
    std::cout << "[Log]" << std::endl;
    std::cout << "  Level: " << log_config_.level << std::endl;
    std::cout << "  Debug: " << (log_config_.debug_enabled ? "ON" : "OFF") << std::endl;
    std::cout << "==========================================" << std::endl;
}
