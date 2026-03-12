#ifndef VIDEO_PUSH_H
#define VIDEO_PUSH_H

#ifdef VIDEO_PUSH_DEBUG
#define DEBUG_LOG(msg) std::cout << "[DEBUG] " << msg << std::endl
#define DEBUG_LOG_VAR(msg, var) std::cout << "[DEBUG] " << msg << var << std::endl
#else
#define DEBUG_LOG(msg) 
#define DEBUG_LOG_VAR(msg, var) 
#endif

#include <iostream>
#include "VideoFrameQueue.h"
#include "FrameData.h"
#include <opencv2/opencv.hpp>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
#include <libavcodec/bsf.h>
}

class FFmpegPush
{
public:
    FFmpegPush();
    ~FFmpegPush();

    bool Init(const char* url, VideoFrameQueue<FrameData>& frame_queue_ptr, 
             int width, int height, int bitrate, int fps);
    void start();
    void pushFrame(FrameData frame_data);

private:
    bool init_format_ctx();
    bool init_codec_ctx();
    bool init_video_stream();
    bool init_hwaccel();
    bool init_swscale_ctx();

    bool init_frames();
    bool write_rtsp_header();
    bool convert_bgr_to_nv12(const cv::Mat& bgr_frame);
    bool encode_and_send_frame(int64_t pts, int frame_count);
    void process_packets(int64_t pts, int& idr_count, int& sps_pps_injected);
    void handle_idr_frame(int& idr_count, int& sps_pps_injected);
    bool inject_sps_pps_to_packet();
    bool check_frame_contains_sps_pps();

    void extract_sps_pps_from_extradata();
    void extract_sps_pps_from_frame(uint8_t* data, int size);
    bool frame_contains_sps_pps(uint8_t* data, int size);
    bool is_avcc_format(uint8_t* data, int size);
    int convert_avcc_to_annexb(uint8_t* data, int size, uint8_t** out_data);
    bool is_idr_frame(uint8_t* data, int size);

private:
    std::string push_url;
    int video_width;
    int video_height;
    int video_bitrate;
    int video_fps;

    AVFormatContext* fmt_ctx = nullptr;
    AVCodecContext* codec_ctx = nullptr;
    const AVCodec* codec = nullptr;
    AVBufferRef* hw_device_ctx = nullptr;
    AVBufferRef* hw_frames_ctx = nullptr;
    SwsContext* sws_ctx = nullptr;
    AVStream* video_stream = nullptr;
    AVPacket* pkt = nullptr;
    AVBSFContext* bsf_ctx = nullptr;
    bool bsf_init = false;

    AVFrame* sw_frame = nullptr;
    AVFrame* hw_frame = nullptr;

    uint8_t* sps_data = nullptr;
    int sps_size = 0;
    uint8_t* pps_data = nullptr;
    int pps_size = 0;
    bool headers_extracted = false;

    int64_t current_pts = 0;
    int frame_count = 0;
    const AVRational src_time_base = { 1, 90000 };
    int64_t frame_duration;

    int idr_count = 0;
    int sps_pps_injected = 0;
    bool initialized = false;

    VideoFrameQueue<FrameData>* frame_queue_ptr;
};

#endif
