#include "VideoSource.h"

VideoSource::VideoSource(const char *device_name, std::atomic<bool> *is_pushing)
{
    this->device_name = device_name;
    this->is_running = is_pushing;
}

VideoSource::~VideoSource()
{
    if(input_format)
    {
        input_format = nullptr;
    }

    if(format_ctx)
    {
        avformat_close_input(&format_ctx);
        format_ctx = nullptr;
    }
}

bool VideoSource::Init()
{
    avdevice_register_all();

    input_format = av_find_input_format("dshow");   // 使用DirectShow

    format_ctx = avformat_alloc_context();
    if (format_ctx == nullptr)
    {
        std::cerr << "Failed to allocate format context." << std::endl;
        return false;
    }

    // 设置读取摄像头缓冲区
    AVDictionary *options = nullptr;
    av_dict_set(&options, "video_size", "640x480", 0);
    av_dict_set(&options, "framerate", "15", 0);

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

    // 调试信息
    av_dump_format(format_ctx, 0, device_name, 0);

    video_stream_index = av_find_best_stream(format_ctx, AVMEDIA_TYPE_VIDEO, 
                                                 -1, -1, nullptr, 0);
    if (video_stream_index < 0) return false;

    // 初始化解码器
    if (!initDecoder(format_ctx->streams[video_stream_index])) return false;


    return true;
}

void VideoSource::start()
{
    int ret;
    std::cout << "Video capture started." << std::endl;

    while (is_running->load(std::memory_order_relaxed))
    {
        AVPacket *packet = av_packet_alloc();
        ret = av_read_frame(format_ctx, packet);
        if (ret < 0){
            av_packet_free(&packet);
            if (ret == AVERROR_EOF || !is_running->load()) break;
            continue;
        }

        // 解码回调给ZPusher
        ret = avcodec_send_packet(codec_ctx, packet);
        if(ret < 0){
            std::cerr << "Error sending packet to decoder: " << av_err2str(ret) << std::endl;
            av_packet_unref(packet);
            av_packet_free(&packet);
            continue;
        }
        
        while(ret >=0){
            AVFrame *frame = av_frame_alloc();
            ret = avcodec_receive_frame(codec_ctx, frame);
            if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                av_frame_free(&frame);
                break;
            }else if(ret < 0){
                std::cerr << "Error receiving frame from decoder: " << av_err2str(ret) << std::endl;
                av_frame_free(&frame);
                break;
            }

            // 这里可以将解码后的帧传递给ZPusher进行推流
            // 例如：video_source->pushFrame(frame);

            av_frame_free(&frame);
        }

        av_packet_unref(packet);
        av_packet_free(&packet);
    }
}

bool VideoSource::initDecoder(AVStream *video_stream)
{
    // const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
    // if (!decoder)  {
    //     std::cerr << "Failed to find decoder." << std::endl;
    //     return false;
    // }

    // 使用CUDA加速的解码器
    const AVCodec *decoder = avcodec_find_decoder_by_name("h264_cuvid");
    if (!decoder)  {
        std::cerr << "Failed to find CUDA decoder." << std::endl;
        return false;
    }

    codec_ctx = avcodec_alloc_context3(decoder);
    if (!codec_ctx) {
        std::cerr << "Failed to allocate codec context." << std::endl;
        return false;
    }

    if (avcodec_parameters_to_context(codec_ctx, video_stream->codecpar) < 0) {
        std::cerr << "Failed to copy codec parameters to context." << std::endl;
        avcodec_free_context(&codec_ctx);
        return false;
    }

    // 绑定CUDA设备上下文
    AVBufferRef *hw_device_ctx = nullptr;
    if (av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0) < 0) {
        std::cerr << "Failed to create CUDA device context." << std::endl;
        avcodec_free_context(&codec_ctx);
        return false;
    }

    codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx); 
    av_buffer_unref(&hw_device_ctx);   

    if (avcodec_open2(codec_ctx, decoder, nullptr) < 0) {
        std::cerr << "Failed to open codec." << std::endl;
        avcodec_free_context(&codec_ctx);
        return false;
    }

    return true;
}