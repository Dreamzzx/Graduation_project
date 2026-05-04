#include "video_push.h"
#include <chrono>

FFmpegPush::FFmpegPush()
{
}

FFmpegPush::~FFmpegPush()
{
}

bool FFmpegPush::Init(const char* url, VideoFrameQueue<FrameData>& frame_queue_ptr, 
             int width, int height, int bitrate, int fps) {
    this->push_url = url;
    this->frame_queue_ptr = &frame_queue_ptr;
    this->video_width = width;
    this->video_height = height;
    this->video_bitrate = bitrate;
    this->video_fps = fps;
    this->frame_duration = 90000 / fps;

    std::cout << "[Push] Starting stream to: " << push_url << std::endl;
    std::cout << "[Push] Resolution: " << video_width << "x" << video_height 
              << ", Bitrate: " << video_bitrate << std::endl;

    std::cout << "[1/7] Network init..." << std::endl;
    avformat_network_init();

    std::cout << "[2/7] CUDA init..." << std::endl;
    if (!init_hwaccel()) return false;

    std::cout << "[3/7] Format context..." << std::endl;
    if (!init_format_ctx()) return false;

    std::cout << "[4/7] Video stream..." << std::endl;
    if (!init_video_stream()) return false;

    std::cout << "[5/7] Codec context..." << std::endl;
    if (!init_codec_ctx()) return false;

    std::cout << "[6/7] Swscale..." << std::endl;
    if (!init_swscale_ctx()) return false;

    if (fmt_ctx->nb_streams > 0) {
        AVStream* s = fmt_ctx->streams[0];
        DEBUG_LOG("Stream 0 codecpar: " << s->codecpar->codec_id 
            << " (H264=" << AV_CODEC_ID_H264 << ")"
            << ", width=" << s->codecpar->width
            << ", height=" << s->codecpar->height);
    }
    std::cout << "Init success!" << std::endl;
    return true;
}

bool FFmpegPush::init_format_ctx(){
    if (avformat_alloc_output_context2(&fmt_ctx, nullptr, "rtsp", push_url.c_str()) < 0) {
        std::cerr << "Create format context failed:" << push_url << std::endl;
        return false;
    }

    if (strcmp(fmt_ctx->oformat->name, "rtsp") != 0) {
        std::cerr << "Not RTSP format: " << fmt_ctx->oformat->name << std::endl;
        return false;
    }

    av_dump_format(fmt_ctx, 0, push_url.c_str(), 1);
    return true;
}

bool FFmpegPush::init_hwaccel(){
    int ret = av_hwdevice_ctx_create(&hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, 0);
    if(ret < 0){
        std::cerr << "Create hw_device_ctx failed:" << push_url << std::endl;
        return false;
    }
    return true;
}

bool FFmpegPush::init_codec_ctx(){
    codec_ctx = avcodec_alloc_context3(codec);
    if(!codec_ctx){
        std::cerr << "Create codec context failed:" << push_url << std::endl;
        return false;
    }

    codec_ctx->codec_id = AV_CODEC_ID_H264;
    codec_ctx->bit_rate = video_bitrate;
    codec_ctx->width = video_width;
    codec_ctx->height = video_height;
    codec_ctx->time_base = { 1, 90000 };
    codec_ctx->framerate = { video_fps, 1 };
    codec_ctx->max_b_frames = 0;
    codec_ctx->pix_fmt = AV_PIX_FMT_CUDA;
    codec_ctx->gop_size = video_fps;
    codec_ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    codec_ctx->hw_device_ctx = av_buffer_ref(hw_device_ctx);

    AVDictionary *codec_opts = nullptr;

    if (strcmp(codec->name, "h264_nvenc") == 0) {
        av_opt_set(codec_ctx->priv_data, "preset", "p1", 0);
        av_opt_set(codec_ctx->priv_data, "tune", "ull", 0);
        av_opt_set(codec_ctx->priv_data, "rc", "cbr", 0);
        av_opt_set(codec_ctx->priv_data, "delay", "0", 0);
        av_opt_set(codec_ctx->priv_data, "repeat_header", "1", 0);
        av_opt_set(codec_ctx->priv_data, "forced-idr", "1", 0);
        av_opt_set(codec_ctx->priv_data, "zerolatency", "1", 0);
        av_opt_set(codec_ctx->priv_data, "gpu_copy", "on", 0);
        std::cout << "[NVENC] Configured for ultra-low latency (preset=p1, tune=ull)" << std::endl;
    }
    else if (strcmp(codec->name, "libx264") == 0) {
        av_opt_set(codec_ctx->priv_data, "preset", "fast", 0);
        av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);
        av_opt_set(codec_ctx->priv_data, "profile", "baseline", 0);
    }

    hw_frames_ctx = av_hwframe_ctx_alloc(codec_ctx->hw_device_ctx);
    if (!hw_frames_ctx) {
        std::cerr << "Create hw_frames_ctx failed:" << push_url << std::endl;
        return false;
    }

    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)hw_frames_ctx->data;
    frames_ctx->format = AV_PIX_FMT_CUDA;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = 1920;
    frames_ctx->height = 1080;
    frames_ctx->initial_pool_size = 2;   

    if (av_hwframe_ctx_init(hw_frames_ctx) < 0) {
        std::cerr << "Failed to initialize hardware frame context" << std::endl;
        return false;
    }
    
    codec_ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ctx);
    if (!codec_ctx->hw_frames_ctx) {
        std::cerr << "Failed to bind hw_frames_ctx" << std::endl;
        av_buffer_unref(&hw_frames_ctx);
        av_buffer_unref(&hw_device_ctx);
        avcodec_free_context(&codec_ctx);
        return false;
    }

    if (avcodec_open2(codec_ctx, codec, &codec_opts) < 0) {
        std::cerr << "Open codec failed:" << push_url << std::endl;
        return false;
    }
    av_dict_free(&codec_opts);

    DEBUG_LOG("After avcodec_open2: extradata_size=" << codec_ctx->extradata_size);
    if (codec_ctx->extradata && codec_ctx->extradata_size > 0) {
        DEBUG_LOG("extradata first bytes: " << std::hex << std::showbase);
        for (int i = 0; i < std::min(codec_ctx->extradata_size, 20); i++) {
            std::cout << (int)codec_ctx->extradata[i] << " ";
        }
        std::cout << std::dec << std::endl;
    }

    if (avcodec_parameters_from_context(video_stream->codecpar, codec_ctx) < 0) {
        std::cerr << "Copy codec parameters to stream failed:" << push_url << std::endl;
        return false;
    }

    video_stream->time_base = codec_ctx->time_base;
    video_stream->avg_frame_rate = codec_ctx->framerate;
    video_stream->r_frame_rate = codec_ctx->framerate;

    extract_sps_pps_from_extradata();

    return true;
}

bool FFmpegPush::init_video_stream(){
    codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!codec) {
        std::cerr << "Codec h264_nvenc not found." << std::endl;
        return false;
    }
    
    for(int i = 0 ;;i++){
        const AVCodecHWConfig *config = avcodec_get_hw_config(codec, i);
        if(!config) break;
        if (config->device_type == AV_HWDEVICE_TYPE_CUDA) {
            std::cout << "NVENC supports CUDA hardware acceleration" << std::endl;
            break;
        }
    }

    video_stream = avformat_new_stream(fmt_ctx, nullptr);
    if (!video_stream) {
        std::cerr << "Create video stream failed:" << push_url << std::endl;
        return false;
    }

    video_stream->id = fmt_ctx->nb_streams - 1;
    return true;
}

bool FFmpegPush::init_swscale_ctx(){
    sws_ctx = sws_getContext(
        video_width, video_height, AV_PIX_FMT_BGR24,
        video_width, video_height, AV_PIX_FMT_NV12,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);

    if(sws_ctx == nullptr){
        std::cerr << "Create swscale context failed:" << push_url << std::endl;
        return false;
    }
    return true;
}

void FFmpegPush::start() {
    if (!init_frames()) return;
    if (!write_rtsp_header()) return;
    initialized = true;
    std::cout << "[FFmpegPush] Initialized and ready" << std::endl;
}

void FFmpegPush::pushFrame(FrameData frame_data) {
    if (!initialized) {
        return;
    }
    
    cv::Mat bgr_frame = std::move(frame_data.frame);

    if (bgr_frame.empty() ||
        bgr_frame.cols != video_width ||
        bgr_frame.rows != video_height) {
        std::cerr << "Invalid frame size: "
            << bgr_frame.cols << "x" << bgr_frame.rows
            << ", expected " << video_width << "x" << video_height << std::endl;
        return;
    }

    if (!convert_bgr_to_nv12(bgr_frame)) return;

    auto now = std::chrono::steady_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    if (start_time_ms == 0) {
        start_time_ms = now_ms;
    }
    
    int64_t elapsed_ms = now_ms - start_time_ms;
    current_pts = elapsed_ms * 90;
    frame_count++;

    if (!encode_and_send_frame(current_pts, frame_count)) return;

    process_packets(current_pts, idr_count, sps_pps_injected);
}

bool FFmpegPush::init_frames() {
    sw_frame = av_frame_alloc();
    hw_frame = av_frame_alloc();
    pkt = av_packet_alloc();
    if (!sw_frame || !hw_frame || !pkt) {
        std::cerr << "Failed to allocate frames/packet" << std::endl;
        return false;
    }
    
    std::cout << "[Push] Starting stream to: " << push_url << std::endl;
    std::cout << "[Push] Resolution: " << video_width << "x" << video_height 
              << ", Bitrate: " << video_bitrate << std::endl;
    std::cout << "[Push] SPS/PPS extracted: " << (headers_extracted ? "YES" : "NO") << std::endl;

    sw_frame->format = AV_PIX_FMT_NV12;
    sw_frame->width = video_width;
    sw_frame->height = video_height;
    if (av_frame_get_buffer(sw_frame, 32) < 0) {
        std::cerr << "Failed to allocate sw_frame buffer" << std::endl;
        return false;
    }

    hw_frame->format = AV_PIX_FMT_CUDA;
    hw_frame->width = video_width;
    hw_frame->height = video_height;
    if (av_hwframe_get_buffer(hw_frames_ctx, hw_frame, 0) < 0) {
        std::cerr << "Failed to allocate hw_frame buffer" << std::endl;
        return false;
    }
    DEBUG_LOG("hw_frame allocated: format=" << hw_frame->format 
              << ", width=" << hw_frame->width << ", height=" << hw_frame->height);
    return true;
}

bool FFmpegPush::write_rtsp_header() {
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", "tcp", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0);
    av_dict_set(&opts, "max_delay", "0", 0);
    av_dict_set(&opts, "buffer_size", "1024000", 0);

    int max_retries = 3;
    int retry_delay_ms = 2000;
    for (int attempt = 1; attempt <= max_retries; attempt++) {
        int ret = avformat_write_header(fmt_ctx, &opts);
        if (ret >= 0) {
            av_dict_free(&opts);
            return true;
        }
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Write header failed (attempt " << attempt << "/" << max_retries << "): " << errbuf << std::endl;
        if (attempt < max_retries) {
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay_ms));
        }
    }
    av_dict_free(&opts);
    return false;
}

bool FFmpegPush::convert_bgr_to_nv12(const cv::Mat& bgr_frame) {
    const uint8_t* src_data[1] = { bgr_frame.data };
    int src_linesize[1] = { bgr_frame.step[0] };

    if (av_frame_make_writable(sw_frame) < 0) {
        std::cerr << "sw_frame not writable" << std::endl;
        return false;
    }

    sws_scale(sws_ctx, src_data, src_linesize, 0, video_height,
        sw_frame->data, sw_frame->linesize);

    if (av_hwframe_transfer_data(hw_frame, sw_frame, 0) < 0) {
        std::cerr << "Failed to transfer data to GPU" << std::endl;
        return false;
    }
    return true;
}

bool FFmpegPush::encode_and_send_frame(int64_t pts, int frame_count) {
    hw_frame->pts = pts;
    hw_frame->pkt_dts = pts;

    int ret = avcodec_send_frame(codec_ctx, hw_frame);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        std::cerr << "Error sending frame to encoder: " << errbuf << std::endl;
        return false;
    }
    
    if (frame_count <= 3) {
        DEBUG_LOG("Frame #" << frame_count << " sent to encoder, pts=" << hw_frame->pts);
    }
    return true;
}

void FFmpegPush::process_packets(int64_t pts, int& idr_count, int& sps_pps_injected) {
    int ret = 0;
    while (ret >= 0) {
        ret = avcodec_receive_packet(codec_ctx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            if (frame_count <= 3 && ret == AVERROR(EAGAIN)) {
                DEBUG_LOG("Encoder needs more frames (EAGAIN)");
            }
            break;
        }
        else if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            std::cerr << "Error encoding: " << errbuf << std::endl;
            break;
        }
        
        DEBUG_LOG("Received packet: size=" << pkt->size 
                  << ", pts=" << pkt->pts << ", dts=" << pkt->dts
                  << ", flags=" << pkt->flags);

        if (pkt->pts == AV_NOPTS_VALUE) pkt->pts = pts;
        if (pkt->dts == AV_NOPTS_VALUE) pkt->dts = pkt->pts;
        av_packet_rescale_ts(pkt, src_time_base, video_stream->time_base);
        pkt->stream_index = video_stream->index;

        bool was_avcc = is_avcc_format(pkt->data, pkt->size);
        if (was_avcc) {
            uint8_t* annexb_data = nullptr;
            int annexb_size = convert_avcc_to_annexb(pkt->data, pkt->size, &annexb_data);
            if (annexb_size > 0 && annexb_data) {
                av_packet_unref(pkt);
                av_new_packet(pkt, annexb_size);
                memcpy(pkt->data, annexb_data, annexb_size);
                av_free(annexb_data);
                if (idr_count < 3) {
                    DEBUG_LOG("Converted AVCC to AnnexB, new size=" << annexb_size);
                }
            }
        }
        
        if (idr_count < 3) {
            DEBUG_LOG("Packet size=" << pkt->size 
                      << " bytes: " << std::hex
                      << (int)pkt->data[0] << " " << (int)pkt->data[1] << " "
                      << (int)pkt->data[2] << " " << (int)pkt->data[3] << " "
                      << (int)pkt->data[4] << std::dec);
        }
        
        if (is_idr_frame(pkt->data, pkt->size)) {
            handle_idr_frame(idr_count, sps_pps_injected);
        }

        av_write_frame(fmt_ctx, pkt);
        av_packet_unref(pkt);
    }
}

bool FFmpegPush::check_frame_contains_sps_pps() {
    int pos = 0;
    while (pos < pkt->size - 4) {
        if (pkt->data[pos] == 0 && pkt->data[pos+1] == 0 && 
            (pkt->data[pos+2] == 1 || (pkt->data[pos+2] == 0 && pkt->data[pos+3] == 1))) {
            int start_code_len = (pkt->data[pos+2] == 1) ? 3 : 4;
            int nal_type = pkt->data[pos + start_code_len] & 0x1f;
            if (nal_type == 7 || nal_type == 8) return true;
        }
        pos++;
    }
    return false;
}

void FFmpegPush::handle_idr_frame(int& idr_count, int& sps_pps_injected) {
    idr_count++;
    DEBUG_LOG("Found IDR frame #" << idr_count << ", size=" << pkt->size);
    
    bool frame_has_sps_pps = check_frame_contains_sps_pps();
    DEBUG_LOG("Frame contains SPS/PPS: " << (frame_has_sps_pps ? "YES" : "NO"));
    
    if (!headers_extracted) {
        extract_sps_pps_from_extradata();
        
        if (!headers_extracted && frame_has_sps_pps) {
            DEBUG_LOG("Trying to extract SPS/PPS from AnnexB frame...");
            int pos = 0;
            while (pos < pkt->size - 4) {
                if (pkt->data[pos] == 0 && pkt->data[pos+1] == 0 && 
                    (pkt->data[pos+2] == 1 || (pkt->data[pos+2] == 0 && pkt->data[pos+3] == 1))) {
                    int start_code_len = (pkt->data[pos+2] == 1) ? 3 : 4;
                    int nal_start = pos + start_code_len;
                    int nal_type = pkt->data[nal_start] & 0x1f;
                    
                    int next_pos = nal_start;
                    while (next_pos < pkt->size - 3) {
                        if (pkt->data[next_pos] == 0 && pkt->data[next_pos+1] == 0 &&
                            (pkt->data[next_pos+2] == 1 || (pkt->data[next_pos+2] == 0 && pkt->data[next_pos+3] == 1))) {
                            break;
                        }
                        next_pos++;
                    }
                    int nal_len = next_pos - nal_start;
                    int total_len = start_code_len + nal_len;
                    
                    if (nal_type == 7 && !sps_data) {
                        sps_data = (uint8_t*)av_malloc(total_len);
                        if (sps_data) {
                            memcpy(sps_data, pkt->data + pos, total_len);
                            sps_size = total_len;
                            std::cout << "[SPS/PPS] Extracted SPS from IDR frame, size=" << sps_size << std::endl;
                        }
                    }
                    else if (nal_type == 8 && !pps_data) {
                        pps_data = (uint8_t*)av_malloc(total_len);
                        if (pps_data) {
                            memcpy(pps_data, pkt->data + pos, total_len);
                            pps_size = total_len;
                            std::cout << "[SPS/PPS] Extracted PPS from IDR frame, size=" << pps_size << std::endl;
                        }
                    }
                    pos = next_pos;
                } else {
                    pos++;
                }
            }
            headers_extracted = (sps_size > 0 && pps_size > 0);
        }
    }
    
    DEBUG_LOG("headers_extracted=" << headers_extracted 
              << ", sps_size=" << sps_size << ", pps_size=" << pps_size);
    
    if (!frame_has_sps_pps && headers_extracted && sps_size > 0 && pps_size > 0) {
        int new_size = sps_size + pps_size + pkt->size;
        
        AVPacket* new_pkt = av_packet_alloc();
        av_new_packet(new_pkt, new_size);
        
        memcpy(new_pkt->data, sps_data, sps_size);
        memcpy(new_pkt->data + sps_size, pps_data, pps_size);
        memcpy(new_pkt->data + sps_size + pps_size, pkt->data, pkt->size);
        
        new_pkt->pts = pkt->pts;
        new_pkt->dts = pkt->dts;
        new_pkt->duration = pkt->duration;
        new_pkt->stream_index = pkt->stream_index;
        new_pkt->flags = pkt->flags | AV_PKT_FLAG_KEY;
        
        av_packet_unref(pkt);
        av_packet_ref(pkt, new_pkt);
        av_packet_free(&new_pkt);
        
        sps_pps_injected++;
        std::cout << "[Push] Injected SPS/PPS before IDR frame #" << idr_count << std::endl;
    } else if (!headers_extracted) {
        std::cerr << "[Push] WARNING: IDR frame without SPS/PPS!" << std::endl;
    }
}

void FFmpegPush::extract_sps_pps_from_extradata() {
    if (sps_data) { av_freep(&sps_data); sps_size = 0; }
    if (pps_data) { av_freep(&pps_data); pps_size = 0; }
    headers_extracted = false;
    
    if (!codec_ctx->extradata || codec_ctx->extradata_size < 7) {
        std::cerr << "[SPS/PPS] No extradata available, size=" 
                  << (codec_ctx->extradata ? codec_ctx->extradata_size : 0) << std::endl;
        return;
    }

    uint8_t* data = codec_ctx->extradata;
    int size = codec_ctx->extradata_size;

    std::cout << "[SPS/PPS] Extradata size=" << size << ", first bytes: "
              << std::hex << (int)data[0] << " " << (int)data[1] << " " 
              << (int)data[2] << " " << (int)data[3] << std::dec << std::endl;

    if (data[0] == 1) {
        std::cout << "[SPS/PPS] Parsing AVCC extradata" << std::endl;
        int sps_count = data[5] & 0x1f;
        int pos = 6;
        
        for (int i = 0; i < sps_count && pos + 2 < size; i++) {
            int len = (data[pos] << 8) | data[pos + 1];
            pos += 2;
            if (pos + len <= size && len > 0) {
                sps_data = (uint8_t*)av_malloc(len + 4);
                if (sps_data) {
                    sps_data[0] = 0x00; sps_data[1] = 0x00;
                    sps_data[2] = 0x00; sps_data[3] = 0x01;
                    memcpy(sps_data + 4, data + pos, len);
                    sps_size = len + 4;
                    std::cout << "[SPS/PPS] Extracted SPS from AVCC, len=" << len << std::endl;
                }
                pos += len;
            }
        }
        
        if (pos < size) {
            int pps_count = data[pos++];
            for (int i = 0; i < pps_count && pos + 2 < size; i++) {
                int len = (data[pos] << 8) | data[pos + 1];
                pos += 2;
                if (pos + len <= size && len > 0) {
                    pps_data = (uint8_t*)av_malloc(len + 4);
                    if (pps_data) {
                        pps_data[0] = 0x00; pps_data[1] = 0x00;
                        pps_data[2] = 0x00; pps_data[3] = 0x01;
                        memcpy(pps_data + 4, data + pos, len);
                        pps_size = len + 4;
                        std::cout << "[SPS/PPS] Extracted PPS from AVCC, len=" << len << std::endl;
                    }
                    pos += len;
                }
            }
        }
    }
    else if (data[0] == 0 && data[1] == 0 && (data[2] == 1 || (data[2] == 0 && data[3] == 1))) {
        std::cout << "[SPS/PPS] Parsing AnnexB extradata" << std::endl;
        int pos = 0;
        while (pos < size - 4) {
            int start_code_len = 0;
            if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1) start_code_len = 3;
            else if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 0 && data[pos+3] == 1) start_code_len = 4;
            
            if (start_code_len > 0) {
                int nal_start = pos + start_code_len;
                if (nal_start >= size) break;
                
                int nal_type = data[nal_start] & 0x1f;
                
                int next_pos = nal_start;
                while (next_pos < size - 3) {
                    if (data[next_pos] == 0 && data[next_pos+1] == 0 &&
                        (data[next_pos+2] == 1 || (data[next_pos+2] == 0 && data[next_pos+3] == 1))) break;
                    next_pos++;
                }
                int nal_len = (next_pos >= size - 3) ? (size - nal_start) : (next_pos - nal_start);
                int total_len = start_code_len + nal_len;
                
                if (nal_type == 7 && !sps_data) {
                    sps_data = (uint8_t*)av_malloc(total_len);
                    if (sps_data) { memcpy(sps_data, data + pos, total_len); sps_size = total_len; }
                    std::cout << "[SPS/PPS] Extracted SPS from AnnexB, size=" << sps_size << std::endl;
                }
                else if (nal_type == 8 && !pps_data) {
                    pps_data = (uint8_t*)av_malloc(total_len);
                    if (pps_data) { memcpy(pps_data, data + pos, total_len); pps_size = total_len; }
                    std::cout << "[SPS/PPS] Extracted PPS from AnnexB, size=" << pps_size << std::endl;
                }
                pos = next_pos;
            } else {
                pos++;
            }
        }
    }
    else {
        std::cerr << "[SPS/PPS] Unknown extradata format, first byte=" << (int)data[0] << std::endl;
        return;
    }

    headers_extracted = (sps_size > 0 && pps_size > 0);
    if (headers_extracted) {
        std::cout << "[SPS/PPS] Successfully extracted SPS(" << sps_size
            << " bytes), PPS(" << pps_size << " bytes)" << std::endl;
    } else {
        std::cerr << "[SPS/PPS] Failed to extract. sps_size=" << sps_size << ", pps_size=" << pps_size << std::endl;
    }
}

bool FFmpegPush::is_idr_frame(uint8_t* data, int size) {
    if (size < 5) return false;
    
    if (is_avcc_format(data, size)) {
        int nal_type = data[4] & 0x1f;
        return nal_type == 5;
    }
    
    int offset = 0;
    if (data[0] == 0 && data[1] == 0 && data[2] == 1) offset = 3;
    else if (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) offset = 4;
    int nal_type = data[offset] & 0x1f;
    return nal_type == 5;
}

bool FFmpegPush::is_avcc_format(uint8_t* data, int size) {
    if (size < 8) return false;
    
    if ((data[0] == 0 && data[1] == 0 && data[2] == 1) ||
        (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1)) {
        return false;
    }
    
    uint32_t first_len_be = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    
    if (first_len_be > 0 && first_len_be + 4 <= (uint32_t)size && first_len_be < 0x100000) {
        int nal_type = data[4] & 0x1f;
        if (nal_type >= 1 && nal_type <= 31) return true;
    }
    return false;
}

typedef enum { AVCC_UNKNOWN, AVCC_BIG_ENDIAN, AVCC_LITTLE_ENDIAN } AVCCFormatType;

static AVCCFormatType detect_avcc_endian(uint8_t* data, int size) {
    uint32_t first_len_be = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    uint32_t first_len_le = data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    
    if (first_len_be > 0 && first_len_be < (uint32_t)size && first_len_be < 0x100000) {
        int nal_type = data[4] & 0x1f;
        if (nal_type >= 1 && nal_type <= 31) return AVCC_BIG_ENDIAN;
    }
    
    if (first_len_le > 0 && first_len_le < (uint32_t)size && first_len_le < 0x100000) {
        int nal_type = data[7] & 0x1f;
        if (nal_type >= 1 && nal_type <= 31) return AVCC_LITTLE_ENDIAN;
    }
    return AVCC_UNKNOWN;
}

static inline uint32_t read_nal_length(uint8_t* data, AVCCFormatType endian) {
    if (endian == AVCC_LITTLE_ENDIAN) return data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

int FFmpegPush::convert_avcc_to_annexb(uint8_t* data, int size, uint8_t** out_data) {
    if (!is_avcc_format(data, size)) {
        *out_data = (uint8_t*)av_malloc(size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (*out_data) {
            memcpy(*out_data, data, size);
            memset(*out_data + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            return size;
        }
        return 0;
    }
    
    AVCCFormatType endian = detect_avcc_endian(data, size);
    if (endian == AVCC_UNKNOWN) {
        std::cerr << "[AVCC] Unknown AVCC endian format" << std::endl;
        return 0;
    }
    
    std::cout << "[AVCC] Detected " << (endian == AVCC_BIG_ENDIAN ? "Big Endian" : "Little Endian") << " format" << std::endl;
    
    int total_size = 0;
    int pos = 0;
    int nal_count = 0;
    
    while (pos < size - 4) {
        uint32_t nal_len = read_nal_length(data + pos, endian);
        if (nal_len == 0 || nal_len > (uint32_t)(size - pos - 4)) break;
        total_size += 4 + nal_len;
        pos += 4 + nal_len;
        nal_count++;
    }
    
    if (total_size == 0 || nal_count == 0) {
        std::cerr << "[AVCC] No valid NAL units found" << std::endl;
        return 0;
    }
    
    *out_data = (uint8_t*)av_malloc(total_size + AV_INPUT_BUFFER_PADDING_SIZE);
    if (!*out_data) {
        std::cerr << "[AVCC] Failed to allocate output buffer" << std::endl;
        return 0;
    }
    
    pos = 0;
    int out_pos = 0;
    while (pos < size - 4) {
        uint32_t nal_len = read_nal_length(data + pos, endian);
        if (nal_len == 0 || nal_len > (uint32_t)(size - pos - 4)) break;
        
        (*out_data)[out_pos++] = 0x00;
        (*out_data)[out_pos++] = 0x00;
        (*out_data)[out_pos++] = 0x00;
        (*out_data)[out_pos++] = 0x01;
        
        memcpy(*out_data + out_pos, data + pos + 4, nal_len);
        out_pos += nal_len;
        
        pos += 4 + nal_len;
    }
    
    memset(*out_data + out_pos, 0, total_size + AV_INPUT_BUFFER_PADDING_SIZE - out_pos);
    
    return out_pos;
}

bool FFmpegPush::frame_contains_sps_pps(uint8_t* data, int size) {
    if (is_avcc_format(data, size)) {
        AVCCFormatType endian = detect_avcc_endian(data, size);
        int pos = 0;
        while (pos < size - 4) {
            uint32_t nal_len = read_nal_length(data + pos, endian);
            if (nal_len == 0 || nal_len > (uint32_t)(size - pos - 4)) break;
            int nal_type = data[pos + 4] & 0x1f;
            if (nal_type == 7 || nal_type == 8) return true;
            pos += 4 + nal_len;
        }
        return false;
    }
    
    int pos = 0;
    while (pos < size - 4) {
        int start_code_len = 0;
        if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1) start_code_len = 3;
        else if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 0 && data[pos+3] == 1) start_code_len = 4;
        
        if (start_code_len > 0) {
            int nal_start = pos + start_code_len;
            if (nal_start >= size) break;
            int nal_type = data[nal_start] & 0x1f;
            if (nal_type == 7 || nal_type == 8) return true;
            
            int next_pos = nal_start;
            while (next_pos < size - 3) {
                if (data[next_pos] == 0 && data[next_pos+1] == 0 && 
                    (data[next_pos+2] == 1 || (data[next_pos+2] == 0 && data[next_pos+3] == 1))) break;
                next_pos++;
            }
            pos = next_pos;
        } else {
            pos++;
        }
    }
    return false;
}

void FFmpegPush::extract_sps_pps_from_frame(uint8_t* data, int size) {
    if (sps_data && pps_data) return;
    
    if (is_avcc_format(data, size)) {
        std::cout << "[SPS/PPS] Detected AVCC format, size=" << size << std::endl;
        AVCCFormatType endian = detect_avcc_endian(data, size);
        int pos = 0;
        while (pos < size - 4) {
            uint32_t nal_len = read_nal_length(data + pos, endian);
            if (nal_len == 0 || nal_len > (uint32_t)(size - pos - 4)) break;
            
            int nal_type = data[pos + 4] & 0x1f;
            
            if (nal_type == 7 && !sps_data) {
                sps_data = (uint8_t*)av_malloc(nal_len + 4);
                if (sps_data) {
                    sps_data[0] = 0x00; sps_data[1] = 0x00;
                    sps_data[2] = 0x00; sps_data[3] = 0x01;
                    memcpy(sps_data + 4, data + pos + 4, nal_len);
                    sps_size = nal_len + 4;
                    std::cout << "[SPS/PPS] Extracted SPS from AVCC frame, size=" << sps_size << std::endl;
                }
            }
            else if (nal_type == 8 && !pps_data) {
                pps_data = (uint8_t*)av_malloc(nal_len + 4);
                if (pps_data) {
                    pps_data[0] = 0x00; pps_data[1] = 0x00;
                    pps_data[2] = 0x00; pps_data[3] = 0x01;
                    memcpy(pps_data + 4, data + pos + 4, nal_len);
                    pps_size = nal_len + 4;
                    std::cout << "[SPS/PPS] Extracted PPS from AVCC frame, size=" << pps_size << std::endl;
                }
            }
            pos += 4 + nal_len;
        }
    } else {
        int pos = 0;
        while (pos < size - 4) {
            int start_code_len = 0;
            if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1) start_code_len = 3;
            else if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 0 && data[pos+3] == 1) start_code_len = 4;
            
            if (start_code_len > 0) {
                int nal_start = pos + start_code_len;
                if (nal_start >= size) break;
                
                int nal_type = data[nal_start] & 0x1f;
                
                int next_pos = nal_start;
                while (next_pos < size - 3) {
                    if (data[next_pos] == 0 && data[next_pos+1] == 0 && 
                        (data[next_pos+2] == 1 || (data[next_pos+2] == 0 && data[next_pos+3] == 1))) break;
                    next_pos++;
                }
                
                int nal_len = next_pos - nal_start;
                
                if (nal_type == 7 && !sps_data) {
                    sps_data = (uint8_t*)av_malloc(nal_len + start_code_len);
                    if (sps_data) {
                        memcpy(sps_data, data + pos, nal_len + start_code_len);
                        sps_size = nal_len + start_code_len;
                        std::cout << "[SPS/PPS] Extracted SPS from AnnexB frame, size=" << sps_size << std::endl;
                    }
                }
                else if (nal_type == 8 && !pps_data) {
                    pps_data = (uint8_t*)av_malloc(nal_len + start_code_len);
                    if (pps_data) {
                        memcpy(pps_data, data + pos, nal_len + start_code_len);
                        pps_size = nal_len + start_code_len;
                        std::cout << "[SPS/PPS] Extracted PPS from AnnexB frame, size=" << pps_size << std::endl;
                    }
                }
                pos = next_pos;
            } else {
                pos++;
            }
        }
    }
    
    headers_extracted = (sps_size > 0 && pps_size > 0);
}
