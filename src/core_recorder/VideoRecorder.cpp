#include "VideoRecorder.h"
#include <iostream>
#include <filesystem>
#include <iomanip>
#include <sstream>

VideoRecorder::VideoRecorder() {
}

VideoRecorder::~VideoRecorder() {
    stop();
    if (hw_device_ctx) {
        av_buffer_unref(&hw_device_ctx);
    }
    if (hw_frames_ctx) {
        av_buffer_unref(&hw_frames_ctx);
    }
}

bool VideoRecorder::Init(const RecordingConfig& config) {
    config_ = config;
    
    std::filesystem::path dir(config_.output_dir);
    if (!std::filesystem::exists(dir)) {
        std::filesystem::create_directories(dir);
        std::cout << "[Recorder] Created output directory: " << config_.output_dir << std::endl;
    }
    
    std::cout << "[Recorder] Initialized. Output dir: " << config_.output_dir << std::endl;
    return true;
}

std::string VideoRecorder::generateFilename() {
    auto now = std::chrono::system_clock::now();
    auto now_time = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << config_.output_dir << "/recording_"
       << std::put_time(std::localtime(&now_time), "%Y%m%d_%H%M%S")
       << ".mp4";
    return ss.str();
}

bool VideoRecorder::startRecording() {
    if (is_recording.load()) {
        return true;
    }
    
    current_filename = generateFilename();
    
    int ret = avformat_alloc_output_context2(&fmt_ctx, nullptr, "mp4", current_filename.c_str());
    if (ret < 0 || !fmt_ctx) {
        std::cerr << "[Recorder] Failed to allocate output context" << std::endl;
        return false;
    }
    
    codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (codec) {
        use_hw_encoder = true;
        std::cout << "[Recorder] Using NVENC hardware encoder" << std::endl;
    } else {
        codec = avcodec_find_encoder(AV_CODEC_ID_H264);
        use_hw_encoder = false;
        std::cout << "[Recorder] NVENC not available, using CPU encoder" << std::endl;
    }
    
    if (!codec) {
        std::cerr << "[Recorder] H264 encoder not found" << std::endl;
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        return false;
    }
    
    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        std::cerr << "[Recorder] Failed to allocate codec context" << std::endl;
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        return false;
    }
    
    codec_ctx->width = config_.width;
    codec_ctx->height = config_.height;
    codec_ctx->time_base = {1, 90000};
    codec_ctx->framerate = {config_.fps, 1};
    codec_ctx->bit_rate = config_.bitrate;
    codec_ctx->gop_size = config_.fps * 2;
    codec_ctx->max_b_frames = 0;
    
    if (use_hw_encoder) {
        codec_ctx->pix_fmt = AV_PIX_FMT_CUDA;
        
        if (!initHardwareEncoder()) {
            std::cerr << "[Recorder] Failed to init hardware encoder, falling back to CPU" << std::endl;
            use_hw_encoder = false;
            codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        }
    } else {
        codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
        codec_ctx->thread_count = 4;
    }
    
    if (fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    
    AVDictionary* codec_opts = nullptr;
    if (use_hw_encoder) {
        av_dict_set(&codec_opts, "preset", "p1", 0);
        av_dict_set(&codec_opts, "tune", "ull", 0);
        av_dict_set(&codec_opts, "rc", "cbr", 0);
        av_dict_set(&codec_opts, "zerolatency", "1", 0);
    } else {
        av_dict_set(&codec_opts, "preset", "ultrafast", 0);
        av_dict_set(&codec_opts, "tune", "zerolatency", 0);
    }
    
    ret = avcodec_open2(codec_ctx, codec, &codec_opts);
    av_dict_free(&codec_opts);
    if (ret < 0) {
        std::cerr << "[Recorder] Failed to open codec" << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        codec_ctx = nullptr;
        return false;
    }
    
    video_stream = avformat_new_stream(fmt_ctx, nullptr);
    if (!video_stream) {
        std::cerr << "[Recorder] Failed to create video stream" << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        codec_ctx = nullptr;
        return false;
    }
    
    video_stream->time_base = {1, 90000};
    avcodec_parameters_from_context(video_stream->codecpar, codec_ctx);
    
    ret = avio_open(&fmt_ctx->pb, current_filename.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        std::cerr << "[Recorder] Failed to open output file: " << current_filename << std::endl;
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        codec_ctx = nullptr;
        video_stream = nullptr;
        return false;
    }
    
    ret = avformat_write_header(fmt_ctx, nullptr);
    if (ret < 0) {
        std::cerr << "[Recorder] Failed to write header" << std::endl;
        if (fmt_ctx->pb) avio_closep(&fmt_ctx->pb);
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        codec_ctx = nullptr;
        video_stream = nullptr;
        return false;
    }
    
    sws_ctx = sws_getContext(
        config_.width, config_.height, AV_PIX_FMT_BGR24,
        config_.width, config_.height, AV_PIX_FMT_NV12,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    
    if (!sws_ctx) {
        std::cerr << "[Recorder] Failed to create sws context" << std::endl;
        av_write_trailer(fmt_ctx);
        if (fmt_ctx->pb) avio_closep(&fmt_ctx->pb);
        avcodec_free_context(&codec_ctx);
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        codec_ctx = nullptr;
        video_stream = nullptr;
        return false;
    }
    
    yuv_frame = av_frame_alloc();
    yuv_frame->format = AV_PIX_FMT_NV12;
    yuv_frame->width = config_.width;
    yuv_frame->height = config_.height;
    av_frame_get_buffer(yuv_frame, 0);
    
    if (use_hw_encoder) {
        hw_frame = av_frame_alloc();
        hw_frame->format = AV_PIX_FMT_CUDA;
        hw_frame->width = config_.width;
        hw_frame->height = config_.height;
        if (av_hwframe_get_buffer(hw_frames_ctx, hw_frame, 0) < 0) {
            std::cerr << "[Recorder] Failed to allocate hw frame" << std::endl;
        }
    }
    
    pkt = av_packet_alloc();
    
    frame_count = 0;
    start_time_ms = 0;
    
    is_recording.store(true);
    std::cout << "[Recorder] Started recording: " << current_filename 
              << " (" << (use_hw_encoder ? "GPU" : "CPU") << " encoding)" << std::endl;
    
    return true;
}

bool VideoRecorder::initHardwareEncoder() {
    int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    if (ret < 0) {
        std::cerr << "[Recorder] Failed to create CUDA device context" << std::endl;
        return false;
    }
    
    codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
    
    hw_frames_ctx = av_hwframe_ctx_alloc(hw_device_ctx);
    if (!hw_frames_ctx) {
        std::cerr << "[Recorder] Failed to allocate hw frames context" << std::endl;
        return false;
    }
    
    AVHWFramesContext* frames_ctx = (AVHWFramesContext*)hw_frames_ctx->data;
    frames_ctx->format = AV_PIX_FMT_CUDA;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = config_.width;
    frames_ctx->height = config_.height;
    frames_ctx->initial_pool_size = 4;
    
    ret = av_hwframe_ctx_init(hw_frames_ctx);
    if (ret < 0) {
        std::cerr << "[Recorder] Failed to init hw frames context" << std::endl;
        av_buffer_unref(&hw_frames_ctx);
        return false;
    }
    
    codec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx);
    return true;
}

void VideoRecorder::stopRecording() {
    if (!is_recording.load()) {
        return;
    }
    
    is_recording.store(false);
    
    if (codec_ctx) {
        avcodec_send_frame(codec_ctx, nullptr);
        while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
            av_packet_rescale_ts(pkt, {1, 90000}, video_stream->time_base);
            pkt->stream_index = video_stream->index;
            av_interleaved_write_frame(fmt_ctx, pkt);
        }
    }
    
    if (fmt_ctx) {
        av_write_trailer(fmt_ctx);
        if (fmt_ctx->pb) avio_closep(&fmt_ctx->pb);
    }
    
    if (sws_ctx) {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
    
    if (yuv_frame) {
        av_frame_free(&yuv_frame);
        yuv_frame = nullptr;
    }
    
    if (hw_frame) {
        av_frame_free(&hw_frame);
        hw_frame = nullptr;
    }
    
    if (pkt) {
        av_packet_free(&pkt);
        pkt = nullptr;
    }
    
    if (codec_ctx) {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }
    
    if (fmt_ctx) {
        avformat_free_context(fmt_ctx);
        fmt_ctx = nullptr;
        video_stream = nullptr;
    }
    
    double duration_sec = static_cast<double>(frame_count) / config_.fps;
    std::cout << "[Recorder] Stopped recording: " << current_filename 
              << " (frames: " << frame_count 
              << ", duration: " << std::fixed << std::setprecision(1) << duration_sec << "s)" 
              << std::endl;
    
    start_time_ms = 0;
}

bool VideoRecorder::writeFrame(const cv::Mat& frame) {
    if (!is_recording.load() || !codec_ctx || !sws_ctx || !yuv_frame) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    if (start_time_ms == 0) {
        start_time_ms = now_ms;
    }
    
    int64_t elapsed_ms = now_ms - start_time_ms;
    
    const uint8_t* src_data[1] = { frame.data };
    int src_linesize[1] = { static_cast<int>(frame.step) };
    
    if (av_frame_make_writable(yuv_frame) < 0) {
        std::cerr << "[Recorder] Failed to make frame writable" << std::endl;
        return false;
    }
    
    sws_scale(sws_ctx, src_data, src_linesize, 0, frame.rows,
              yuv_frame->data, yuv_frame->linesize);
    
    AVFrame* encode_frame = yuv_frame;
    
    if (use_hw_encoder && hw_frame) {
        if (av_hwframe_transfer_data(hw_frame, yuv_frame, 0) < 0) {
            std::cerr << "[Recorder] Failed to transfer frame to GPU" << std::endl;
            return false;
        }
        encode_frame = hw_frame;
    }
    
    encode_frame->pts = elapsed_ms * 90;
    encode_frame->key_frame = 0;
    encode_frame->pict_type = AV_PICTURE_TYPE_NONE;
    
    int ret = avcodec_send_frame(codec_ctx, encode_frame);
    if (ret < 0) {
        std::cerr << "[Recorder] Error sending frame to encoder" << std::endl;
        return false;
    }
    
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        } else if (ret < 0) {
            std::cerr << "[Recorder] Error receiving packet" << std::endl;
            return false;
        }
        
        av_packet_rescale_ts(pkt, {1, 90000}, video_stream->time_base);
        pkt->stream_index = video_stream->index;
        
        ret = av_interleaved_write_frame(fmt_ctx, pkt);
        if (ret < 0) {
            std::cerr << "[Recorder] Error writing packet" << std::endl;
            return false;
        }
    }
    
    frame_count++;
    return true;
}

void VideoRecorder::onPersonDetected(const cv::Mat& frame) {
    last_person_time = std::chrono::steady_clock::now();
    
    if (!is_recording.load()) {
        startRecording();
    }
    
    if (is_recording.load()) {
        writeFrame(frame);
    }
}

void VideoRecorder::onPersonLeft() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_person_time).count();
    
    if (is_recording.load() && elapsed > config_.person_leave_timeout_ms) {
        stopRecording();
    }
}

void VideoRecorder::stop() {
    should_stop.store(true);
    stopRecording();
}
