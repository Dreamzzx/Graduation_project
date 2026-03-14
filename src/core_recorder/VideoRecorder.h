#ifndef VIDEO_RECORDER_H
#define VIDEO_RECORDER_H

#include <string>
#include <atomic>
#include <chrono>
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
}

struct RecordingConfig {
    int width = 1920;
    int height = 1080;
    int fps = 15;
    int bitrate = 2000000;
    std::string output_dir = "recordings";
    int person_leave_timeout_ms = 3000;
};

class VideoRecorder {
public:
    VideoRecorder();
    ~VideoRecorder();

    bool Init(const RecordingConfig& config);
    
    void onPersonDetected(const cv::Mat& frame);
    void onPersonLeft();
    
    bool isRecording() const { return is_recording.load(); }
    
    void stop();

private:
    bool startRecording();
    void stopRecording();
    bool writeFrame(const cv::Mat& frame);
    std::string generateFilename();
    bool initHardwareEncoder();

private:
    static constexpr AVRational MPEG_TIME_BASE = {1, 90000};
    
    RecordingConfig config_;
    
    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVStream* video_stream = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVFrame* yuv_frame = nullptr;
    AVFrame* hw_frame = nullptr;
    AVPacket* pkt = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVBufferRef* hw_frames_ctx = nullptr;
    
    bool use_hw_encoder = false;
    
    std::atomic<bool> is_recording{false};
    std::atomic<bool> should_stop{false};
    
    int64_t frame_count = 0;
    int64_t next_pts = 0;
    int64_t start_time_ms = 0;
    
    std::chrono::steady_clock::time_point last_person_time;
    std::string current_filename;
};

#endif
