#include "VideoSource.h"
#include <sstream>

VideoSource::VideoSource(const char* device_name, std::atomic<bool>* is_pushing, VideoFrameQueue<cv::Mat>* shared_queue)
{
    this->device_name = device_name;
    this->is_running = is_pushing;
    
    if (shared_queue) {
        frame_queue = shared_queue;
        use_shared_queue = true;
    } else {
        frame_queue = &local_queue;
        use_shared_queue = false;
    }
}

VideoSource::VideoSource(const char* device_name, std::atomic<bool>* is_pushing, int width, int height, int framerate, VideoFrameQueue<cv::Mat>* shared_queue)
    : VideoSource(device_name, is_pushing, shared_queue)
{
    this->capture_width = width;
    this->capture_height = height;
    this->capture_framerate = framerate;
}

VideoSource::~VideoSource()
{
    if (input_format)
    {
        input_format = nullptr;
    }

    if (format_ctx)
    {
        avformat_close_input(&format_ctx);
        format_ctx = nullptr;
    }

    if (codec_ctx)
    {
        avcodec_free_context(&codec_ctx);
        codec_ctx = nullptr;
    }

    if (hw_device_ctx)
    {
        av_buffer_unref(&hw_device_ctx);
        hw_device_ctx = nullptr;
    }

    if (sws_ctx)
    {
        sws_freeContext(sws_ctx);
        sws_ctx = nullptr;
    }
}

bool VideoSource::Init()
{
    avdevice_register_all();
    
    av_log_set_level(AV_LOG_FATAL);

    input_format = av_find_input_format("dshow");

    format_ctx = avformat_alloc_context();
    if (format_ctx == nullptr)
    {
        std::cerr << "Failed to allocate format context." << std::endl;
        return false;
    }

    AVDictionary* options = nullptr;
    
    std::stringstream ss;
    ss << capture_framerate;
    av_dict_set(&options, "framerate", ss.str().c_str(), 0);
    
    ss.str("");
    ss << capture_width << "x" << capture_height;
    av_dict_set(&options, "video_size", ss.str().c_str(), 0);
    
    av_dict_set(&options, "rtbufsize", "2000000", 0);
    av_dict_set(&options, "vcodec", "mjpeg", 0);
    av_dict_set(&options, "input_format", "mjpeg", 0);
    
    std::cout << "Capture settings: " << ss.str() << " @ " << capture_framerate << "fps" << std::endl;

    printf("Opening video device: %s\n", device_name);
    if (avformat_open_input(&format_ctx, device_name, input_format, &options) < 0)
    {
        std::cerr << "Failed to open input device." << std::endl;
        return false;
    }

    int ret = avformat_find_stream_info(format_ctx, nullptr);
    if (ret < 0)
    {
        std::cerr << "Failed to find stream info." << std::endl;
        return false;
    }

    av_dump_format(format_ctx, 0, device_name, 0);

    video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, 
                                                 -1, -1, nullptr, 0);
    if (video_stream_index < 0) return false;

    AVStream* video_stream = format_ctx->streams[video_stream_index];
    const AVCodecDescriptor* codec_desc = avcodec_descriptor_get(video_stream->codecpar->codec_id);
    std::cout << "Video stream codec: " << (codec_desc ? codec_desc->name : "unknown") 
              << ", codec_id: " << video_stream->codecpar->codec_id << std::endl;

    if (!initDecoder(video_stream)) return false;

    return true;
}

void VideoSource::start()
{
    int ret;
    std::cout << "Video capture started." << std::endl;

    int frame_count = 0;

    while (is_running->load(std::memory_order_relaxed))
    {
        AVPacket* packet = av_packet_alloc();
        ret = av_read_frame(format_ctx, packet);
        if (ret < 0) {
            av_packet_free(&packet);
            if (ret == AVERROR_EOF || !is_running->load()) break;
            continue;
        }

        ret = avcodec_send_packet(codec_ctx, packet);
        if (ret < 0) {
            av_packet_unref(packet);
            av_packet_free(&packet);
            continue;
        }
        
        AVFrame* frame = av_frame_alloc();
        ret = avcodec_receive_frame(codec_ctx, frame);
        if (ret == 0) {
            if (frame_count < 5) {
                std::cout << "Frame " << frame_count << ": format=" << frame->format 
                          << " (CUDA=" << AV_PIX_FMT_CUDA << "), hw_frames_ctx=" 
                          << (frame->hw_frames_ctx ? "YES" : "NO") << std::endl;
                frame_count++;
            }
            cv::Mat mat = HWFrameToCvMat(frame);
            if (!mat.empty()) {
                frame_queue->Push(std::move(mat));
            }
        }
        
        av_frame_free(&frame);
        av_packet_unref(packet);
        av_packet_free(&packet);
    }
}

bool VideoSource::initDecoder(AVStream* video_stream)
{
    int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    if (ret < 0)
    {
        std::cerr << "Failed to create CUDA device context! Error: " << ret << std::endl;
        return false;
    }
    std::cout << "CUDA device context created successfully!" << std::endl;

    const AVCodec* codec = avcodec_find_decoder_by_name("mjpeg_cuvid");
    bool use_hw = true;
    if (!codec)
    {
        std::cerr << "mjpeg_cuvid decoder not found, falling back to software decoder!" << std::endl;
        codec = avcodec_find_decoder(video_stream->codecpar->codec_id);
        use_hw = false;
        if (!codec)
        {
            std::cerr << "Unsupported codec!" << std::endl;
            return false;
        }
    }
    else
    {
        std::cout << "Using hardware decoder: " << codec->name << std::endl;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx)
    {
        std::cerr << "Failed to allocate codec context!" << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0)
    {
        std::cerr << "Failed to copy codec parameters!" << std::endl;
        return false;
    }

    AVDictionary* codec_opts = nullptr;
    if (use_hw)
    {
        codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);
        if (!codec_ctx->hw_device_ctx)
        {
            std::cerr << "Failed to set hw device context!" << std::endl;
            return false;
        }
        std::cout << "Set hw_device_ctx before open" << std::endl;
    }

    if (avcodec_open2(codec_ctx, codec, &codec_opts) < 0)
    {
        std::cerr << "Failed to open codec!" << std::endl;
        av_dict_free(&codec_opts);
        return false;
    }
    av_dict_free(&codec_opts);

    if (use_hw && !codec_ctx->hw_frames_ctx)
    {
        AVBufferRef* hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
        if (!hw_frames_ref)
        {
            std::cerr << "Failed to allocate hw frames context after open!" << std::endl;
            return false;
        }

        AVHWFramesContext* frames_ctx = (AVHWFramesContext*)hw_frames_ref->data;
        frames_ctx->format = AV_PIX_FMT_CUDA;
        frames_ctx->sw_format = AV_PIX_FMT_NV12;
        frames_ctx->width = codec_ctx->coded_width ? codec_ctx->coded_width : codec_ctx->width;
        frames_ctx->height = codec_ctx->coded_height ? codec_ctx->coded_height : codec_ctx->height;
        frames_ctx->initial_pool_size = 4;

        if (av_hwframe_ctx_init(hw_frames_ref) < 0)
        {
            std::cerr << "Failed to init hw frames context after open!" << std::endl;
            av_buffer_unref(&hw_frames_ref);
            return false;
        }

        codec_ctx->hw_frames_ctx = hw_frames_ref;
        std::cout << "HW frames context set after open: " << frames_ctx->width << "x" << frames_ctx->height << std::endl;
    }

    hw_pix_fmt = AV_PIX_FMT_CUDA;

    std::cout << "Decoder opened: " << codec->name << std::endl;
    std::cout << "codec_ctx->hw_frames_ctx: " << (codec_ctx->hw_frames_ctx ? "SET" : "NULL") << std::endl;
    std::cout << "codec_ctx->hw_device_ctx: " << (codec_ctx->hw_device_ctx ? "SET" : "NULL") << std::endl;

    return true;
}

cv::Mat VideoSource::HWFrameToCvMat(AVFrame* frame)
{
    AVFrame* sw_frame = nullptr;
    AVFrame* tmp_frame = frame;

    static bool first_frame = true;
    if (first_frame)
    {
        std::cout << "Frame format: " << frame->format << ", hw_pix_fmt: " << hw_pix_fmt << std::endl;
        first_frame = false;
    }

    if (frame->format == hw_pix_fmt)
    {
        sw_frame = av_frame_alloc();
        if (av_hwframe_transfer_data(sw_frame, frame, 0) < 0)
        {
            std::cerr << "Failed to transfer frame from GPU to CPU!" << std::endl;
            av_frame_free(&sw_frame);
            return cv::Mat();
        }
        tmp_frame = sw_frame;
    }

    if (!sws_ctx) {
        sws_ctx = sws_getContext(
            tmp_frame->width, tmp_frame->height, (AVPixelFormat)tmp_frame->format,
            tmp_frame->width, tmp_frame->height, AV_PIX_FMT_BGR24,
            SWS_BILINEAR, nullptr, nullptr, nullptr);
    }

    cv::Mat mat(tmp_frame->height, tmp_frame->width, CV_8UC3);
    
    uint8_t* dst_data[1] = { mat.data };
    int dst_linesize[1] = { static_cast<int>(mat.step) };
    
    sws_scale(sws_ctx, tmp_frame->data, tmp_frame->linesize, 0, tmp_frame->height,
              dst_data, dst_linesize);

    if (sw_frame)
        av_frame_free(&sw_frame);

    return mat;
}
