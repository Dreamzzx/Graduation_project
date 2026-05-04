# SPS/PPS 处理流程详解

## 一、概述

### 1.1 什么是 SPS/PPS

**SPS (Sequence Parameter Set)** 和 **PPS (Picture Parameter Set)** 是 H.264/AVC 视频编码中的关键参数集：

| 参数集 | NAL Type | 作用 |
|--------|----------|------|
| **SPS** | 7 | 序列参数集，包含视频序列的全局参数（分辨率、帧率、编码配置等） |
| **PPS** | 8 | 图像参数集，包含图像级别的编码参数（熵编码模式、量化参数等） |

### 1.2 为什么需要处理 SPS/PPS

1. **解码器初始化**: 解码器必须先获取 SPS/PPS 才能正确解码视频
2. **关键帧依赖**: IDR 帧必须包含或引用 SPS/PPS
3. **RTSP 推流要求**: RTSP 流需要每个 IDR 帧前注入 SPS/PPS
4. **格式转换**: NVENC 编码器输出 AVCC 格式，RTSP 需要 AnnexB 格式

---

## 二、H.264 NAL 单元格式

### 2.1 NAL 单元类型

```
NAL Unit Header (1 byte):
┌─────────────────────────────────────┐
│ forbidden_zero_bit │ nal_ref_idc │ nal_unit_type │
│      (1 bit)       │   (2 bits)  │    (5 bits)   │
└─────────────────────────────────────┘

常用类型:
- Type 1: 非IDR图像片 (Non-IDR Slice)
- Type 5: IDR图像片 (IDR Slice, 关键帧)
- Type 7: SPS (序列参数集)
- Type 8: PPS (图像参数集)
```

### 2.2 两种封装格式

#### AnnexB 格式 (流媒体传输)
```
┌──────────────┬──────────────┬─────────────┬──────────────┬─────────────┐
│ Start Code   │ NAL Header   │ NAL Payload │ Start Code   │ NAL Header  │ ...
│ 00 00 00 01  │ (1 byte)     │ (变长)      │ 00 00 00 01  │ (1 byte)    │
└──────────────┴──────────────┴─────────────┴──────────────┴─────────────┘

特点:
- 使用起始码 (Start Code: 00 00 00 01 或 00 00 01) 分隔 NAL 单元
- 适用于流媒体传输 (RTSP, MPEG-TS)
- 每个关键帧前需要注入 SPS/PPS
```

#### AVCC 格式 (容器封装)
```
┌──────────────┬──────────────┬─────────────┬──────────────┬─────────────┐
│ NAL Length   │ NAL Header   │ NAL Payload │ NAL Length   │ NAL Header  │ ...
│ (4 bytes BE) │ (1 byte)     │ (变长)      │ (4 bytes BE) │ (1 byte)    │
└──────────────┴──────────────┴─────────────┴──────────────┴─────────────┘

特点:
- 使用长度前缀 (4 bytes, Big Endian) 表示 NAL 单元长度
- 适用于容器格式 (MP4, MKV, FLV)
- SPS/PPS 存储在文件头 (extradata)
```

---

## 三、代码实现详解

### 3.1 类成员变量

```cpp
class FFmpegPush {
private:
    // SPS/PPS 数据存储
    uint8_t* sps_data = nullptr;    // SPS 数据 (包含起始码)
    int sps_size = 0;               // SPS 数据大小
    uint8_t* pps_data = nullptr;    // PPS 数据 (包含起始码)
    int pps_size = 0;               // PPS 数据大小
    bool headers_extracted = false; // 是否已成功提取
    
    // 统计
    int idr_count = 0;              // IDR 帧计数
    int sps_pps_injected = 0;       // SPS/PPS 注入计数
};
```

### 3.2 SPS/PPS 提取流程

#### 3.2.1 从 extradata 提取 (推荐方式)

```cpp
void FFmpegPush::extract_sps_pps_from_extradata() {
    // 1. 清理旧数据
    if (sps_data) { av_freep(&sps_data); sps_size = 0; }
    if (pps_data) { av_freep(&pps_data); pps_size = 0; }
    headers_extracted = false;
    
    // 2. 检查 extradata 是否存在
    if (!codec_ctx->extradata || codec_ctx->extradata_size < 7) {
        std::cerr << "[SPS/PPS] No extradata available" << std::endl;
        return;
    }

    uint8_t* data = codec_ctx->extradata;
    int size = codec_ctx->extradata_size;

    // 3. 判断格式并解析
    if (data[0] == 1) {
        // AVCC 格式 (MP4 风格)
        parse_avcc_extradata(data, size);
    }
    else if (data[0] == 0 && data[1] == 0) {
        // AnnexB 格式 (流媒体风格)
        parse_annexb_extradata(data, size);
    }
}
```

#### 3.2.2 解析 AVCC 格式 extradata

```
AVCC extradata 结构:
┌────┬────┬────┬────┬────┬────┬─────────┬────┬─────────┐
│ 01 │ Profile │ Level │ FF │ SPS │ SPS │ SPS │ PPS │ PPS │ ...
│    │  Byte   │ Byte  │    │Cnt │ Len │Data│ Cnt │Data │
└────┴─────────┴───────┴────┴────┴─────┴────┴────┴─────┘
  0     1-2      3      4    5   6-7   ...   N    ...

解析代码:
```

```cpp
if (data[0] == 1) {
    // AVCC 格式
    int sps_count = data[5] & 0x1f;  // SPS 数量
    int pos = 6;
    
    // 提取 SPS
    for (int i = 0; i < sps_count && pos + 2 < size; i++) {
        int len = (data[pos] << 8) | data[pos + 1];  // 读取长度 (Big Endian)
        pos += 2;
        
        if (pos + len <= size && len > 0) {
            // 分配空间: 长度 + 4字节起始码
            sps_data = (uint8_t*)av_malloc(len + 4);
            if (sps_data) {
                // 添加 AnnexB 起始码
                sps_data[0] = 0x00; sps_data[1] = 0x00;
                sps_data[2] = 0x00; sps_data[3] = 0x01;
                // 复制 SPS 数据
                memcpy(sps_data + 4, data + pos, len);
                sps_size = len + 4;
            }
            pos += len;
        }
    }
    
    // 提取 PPS (类似流程)
    int pps_count = data[pos++];
    for (int i = 0; i < pps_count; i++) {
        int len = (data[pos] << 8) | data[pos + 1];
        pos += 2;
        
        if (pos + len <= size && len > 0) {
            pps_data = (uint8_t*)av_malloc(len + 4);
            if (pps_data) {
                pps_data[0] = 0x00; pps_data[1] = 0x00;
                pps_data[2] = 0x00; pps_data[3] = 0x01;
                memcpy(pps_data + 4, data + pos, len);
                pps_size = len + 4;
            }
            pos += len;
        }
    }
}
```

#### 3.2.3 解析 AnnexB 格式 extradata

```cpp
else if (data[0] == 0 && data[1] == 0) {
    // AnnexB 格式
    int pos = 0;
    while (pos < size - 4) {
        // 检测起始码
        int start_code_len = 0;
        if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 1) 
            start_code_len = 3;  // 00 00 01
        else if (data[pos] == 0 && data[pos+1] == 0 && data[pos+2] == 0 && data[pos+3] == 1) 
            start_code_len = 4;  // 00 00 00 01
        
        if (start_code_len > 0) {
            int nal_start = pos + start_code_len;
            int nal_type = data[nal_start] & 0x1f;  // 获取 NAL 类型
            
            // 查找下一个 NAL 单元
            int next_pos = find_next_start_code(data, size, nal_start);
            int nal_len = next_pos - nal_start;
            int total_len = start_code_len + nal_len;
            
            // 根据 NAL 类型保存
            if (nal_type == 7 && !sps_data) {
                sps_data = (uint8_t*)av_malloc(total_len);
                memcpy(sps_data, data + pos, total_len);
                sps_size = total_len;
            }
            else if (nal_type == 8 && !pps_data) {
                pps_data = (uint8_t*)av_malloc(total_len);
                memcpy(pps_data, data + pos, total_len);
                pps_size = total_len;
            }
            pos = next_pos;
        } else {
            pos++;
        }
    }
}
```

### 3.3 从 IDR 帧提取 SPS/PPS (备用方式)

当 extradata 不可用时，从 IDR 帧中提取：

```cpp
void FFmpegPush::handle_idr_frame(int& idr_count, int& sps_pps_injected) {
    idr_count++;
    
    bool frame_has_sps_pps = check_frame_contains_sps_pps();
    
    if (!headers_extracted && frame_has_sps_pps) {
        // 从 AnnexB 格式的 IDR 帧中提取
        int pos = 0;
        while (pos < pkt->size - 4) {
            // 查找起始码
            if (pkt->data[pos] == 0 && pkt->data[pos+1] == 0 && 
                (pkt->data[pos+2] == 1 || (pkt->data[pos+2] == 0 && pkt->data[pos+3] == 1))) {
                
                int start_code_len = (pkt->data[pos+2] == 1) ? 3 : 4;
                int nal_start = pos + start_code_len;
                int nal_type = pkt->data[nal_start] & 0x1f;
                
                // 查找下一个 NAL
                int next_pos = find_next_start_code(pkt->data, pkt->size, nal_start);
                int total_len = next_pos - pos;
                
                // 提取 SPS
                if (nal_type == 7 && !sps_data) {
                    sps_data = (uint8_t*)av_malloc(total_len);
                    memcpy(sps_data, pkt->data + pos, total_len);
                    sps_size = total_len;
                }
                // 提取 PPS
                else if (nal_type == 8 && !pps_data) {
                    pps_data = (uint8_t*)av_malloc(total_len);
                    memcpy(pps_data, pkt->data + pos, total_len);
                    pps_size = total_len;
                }
                pos = next_pos;
            } else {
                pos++;
            }
        }
        headers_extracted = (sps_size > 0 && pps_size > 0);
    }
}
```

### 3.4 SPS/PPS 注入流程

#### 3.4.1 检测 IDR 帧

```cpp
bool FFmpegPush::is_idr_frame(uint8_t* data, int size) {
    if (size < 5) return false;
    
    // AVCC 格式
    if (is_avcc_format(data, size)) {
        int nal_type = data[4] & 0x1f;  // 第5字节是 NAL Header
        return nal_type == 5;           // Type 5 = IDR
    }
    
    // AnnexB 格式
    int offset = 0;
    if (data[0] == 0 && data[1] == 0 && data[2] == 1) 
        offset = 3;
    else if (data[0] == 0 && data[1] == 0 && data[2] == 0 && data[3] == 1) 
        offset = 4;
    
    int nal_type = data[offset] & 0x1f;
    return nal_type == 5;
}
```

#### 3.4.2 检测帧是否包含 SPS/PPS

```cpp
bool FFmpegPush::check_frame_contains_sps_pps() {
    int pos = 0;
    while (pos < pkt->size - 4) {
        // 查找起始码
        if (pkt->data[pos] == 0 && pkt->data[pos+1] == 0 && 
            (pkt->data[pos+2] == 1 || (pkt->data[pos+2] == 0 && pkt->data[pos+3] == 1))) {
            
            int start_code_len = (pkt->data[pos+2] == 1) ? 3 : 4;
            int nal_type = pkt->data[pos + start_code_len] & 0x1f;
            
            if (nal_type == 7 || nal_type == 8) {
                return true;  // 包含 SPS 或 PPS
            }
            pos += start_code_len;
        } else {
            pos++;
        }
    }
    return false;
}
```

#### 3.4.3 注入 SPS/PPS 到 IDR 帧

```cpp
void FFmpegPush::handle_idr_frame(int& idr_count, int& sps_pps_injected) {
    idr_count++;
    
    bool frame_has_sps_pps = check_frame_contains_sps_pps();
    
    // 如果 IDR 帧不包含 SPS/PPS，且我们已经提取到了，则注入
    if (!frame_has_sps_pps && headers_extracted && sps_size > 0 && pps_size > 0) {
        // 计算新大小
        int new_size = sps_size + pps_size + pkt->size;
        
        // 分配新缓冲区
        uint8_t* new_data = (uint8_t*)av_malloc(new_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (new_data) {
            int offset = 0;
            
            // 1. 复制 SPS
            memcpy(new_data + offset, sps_data, sps_size);
            offset += sps_size;
            
            // 2. 复制 PPS
            memcpy(new_data + offset, pps_data, pps_size);
            offset += pps_size;
            
            // 3. 复制原始 IDR 帧
            memcpy(new_data + offset, pkt->data, pkt->size);
            
            // 4. 替换 packet 数据
            av_packet_unref(pkt);
            pkt->data = new_data;
            pkt->size = new_size;
            
            sps_pps_injected++;
            std::cout << "[SPS/PPS] Injected into IDR frame #" << idr_count << std::endl;
        }
    }
}
```

### 3.5 AVCC 到 AnnexB 格式转换

NVENC 编码器可能输出 AVCC 格式，需要转换为 AnnexB：

```cpp
int FFmpegPush::convert_avcc_to_annexb(uint8_t* data, int size, uint8_t** out_data) {
    if (!is_avcc_format(data, size)) {
        // 已经是 AnnexB 格式，直接复制
        *out_data = (uint8_t*)av_malloc(size);
        memcpy(*out_data, data, size);
        return size;
    }
    
    // 检测字节序
    AVCCFormatType endian = detect_avcc_endian(data, size);
    
    // 计算转换后大小
    int total_size = 0;
    int pos = 0;
    while (pos < size - 4) {
        uint32_t nal_len = read_nal_length(data + pos, endian);
        total_size += 4 + nal_len;  // 4字节起始码 + NAL数据
        pos += 4 + nal_len;
    }
    
    // 分配输出缓冲区
    *out_data = (uint8_t*)av_malloc(total_size);
    
    // 转换每个 NAL 单元
    pos = 0;
    int out_pos = 0;
    while (pos < size - 4) {
        uint32_t nal_len = read_nal_length(data + pos, endian);
        
        // 写入起始码 00 00 00 01
        (*out_data)[out_pos++] = 0x00;
        (*out_data)[out_pos++] = 0x00;
        (*out_data)[out_pos++] = 0x00;
        (*out_data)[out_pos++] = 0x01;
        
        // 复制 NAL 数据
        memcpy(*out_data + out_pos, data + pos + 4, nal_len);
        out_pos += nal_len;
        
        pos += 4 + nal_len;
    }
    
    return out_pos;
}
```

---

## 四、完整处理流程

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         SPS/PPS 处理完整流程                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  1. 编码器初始化阶段                                                         │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ avcodec_open2()                                                      │   │
│  │         │                                                            │   │
│  │         ▼                                                            │   │
│  │ codec_ctx->extradata 生成 (AVCC 格式)                                │   │
│  │         │                                                            │   │
│  │         ▼                                                            │   │
│  │ extract_sps_pps_from_extradata()                                     │   │
│  │         │                                                            │   │
│  │         ├─► 解析 AVCC 格式 ─► 提取 SPS/PPS ─► 添加起始码             │   │
│  │         │                                                            │   │
│  │         └─► 解析 AnnexB 格式 ─► 直接复制 SPS/PPS                     │   │
│  │                                                                      │   │
│  │ headers_extracted = true                                             │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  2. 每帧处理阶段                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │ avcodec_receive_packet()                                             │   │
│  │         │                                                            │   │
│  │         ▼                                                            │   │
│  │ 检测格式 (AVCC / AnnexB)                                             │   │
│  │         │                                                            │   │
│  │         ├─► AVCC 格式 ─► convert_avcc_to_annexb()                   │   │
│  │         │                                                            │   │
│  │         ▼                                                            │   │
│  │ is_idr_frame()?                                                      │   │
│  │         │                                                            │   │
│  │         ├─► 是 IDR 帧                                                │   │
│  │         │      │                                                     │   │
│  │         │      ▼                                                     │   │
│  │         │   check_frame_contains_sps_pps()?                          │   │
│  │         │      │                                                     │   │
│  │         │      ├─► 包含 ─► 直接发送                                  │   │
│  │         │      │                                                     │   │
│  │         │      └─► 不包含 ─► 注入 SPS + PPS + 原始帧                 │   │
│  │         │                                                            │   │
│  │         └─► 非 IDR 帧 ─► 直接发送                                    │   │
│  │                                                                      │   │
│  │ av_write_frame()                                                     │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 五、数据结构示例

### 5.1 SPS 数据示例

```
原始 AVCC extradata:
01 64 00 1F FF E1 00 1A 67 64 00 1F AC D9 40 50 05 BB 01 10 00 00 03 00 10 00 00 03 01 E0 80 ...
│  │     │  │  │  │     │  └─────────────────────────────────────────────────────────┘
│  │     │  │  │  │     │                        SPS 数据 (26 bytes)
│  │     │  │  │  │     └─ SPS 长度 = 0x001A = 26
│  │     │  │  │  └─ SPS 数量 = 1
│  │     │  │  └─ FF (保留)
│  │     │  └─ Level = 31 (3.1)
│  │     └─ Profile = 100 (High)
│  └─ 配置版本 = 1

转换后 AnnexB 格式 SPS:
00 00 00 01 67 64 00 1F AC D9 40 50 05 BB 01 10 00 00 03 00 10 00 00 03 01 E0 80
│           │  └────────────────────────────────────────────────────────────────┘
│           │                          SPS 数据 (26 bytes)
│           └─ NAL Header = 0x67 (Type 7 = SPS)
└─ 起始码 00 00 00 01
```

### 5.2 IDR 帧注入前后对比

```
注入前 (不含 SPS/PPS):
┌──────────────────┬──────────────────┬─────────────────────────────┐
│ 00 00 00 01 65   │ Slice Header     │ Slice Data                  │
│ (IDR NAL Header) │                  │                             │
└──────────────────┴──────────────────┴─────────────────────────────┘

注入后:
┌─────────────────┬─────────────────┬─────────────────┬──────────────────┬──────────────────┬─────────────────────────────┐
│ 00 00 00 01 67  │ SPS Data        │ 00 00 00 01 68  │ PPS Data         │ 00 00 00 01 65   │ IDR Slice Data              │
│ (SPS Header)    │                 │ (PPS Header)    │                  │ (IDR Header)     │                             │
└─────────────────┴─────────────────┴─────────────────┴──────────────────┴──────────────────┴─────────────────────────────┘
```

---

## 六、关键函数总结

| 函数 | 作用 | 调用时机 |
|------|------|----------|
| `extract_sps_pps_from_extradata()` | 从编码器 extradata 提取 SPS/PPS | 编码器初始化后 |
| `is_idr_frame()` | 检测是否为 IDR 帧 | 每个编码包 |
| `check_frame_contains_sps_pps()` | 检测帧是否已包含 SPS/PPS | IDR 帧处理时 |
| `handle_idr_frame()` | 处理 IDR 帧，注入 SPS/PPS | IDR 帧处理时 |
| `is_avcc_format()` | 检测是否为 AVCC 格式 | 格式判断时 |
| `convert_avcc_to_annexb()` | AVCC 转 AnnexB 格式 | AVCC 格式帧处理时 |

---

## 七、常见问题

### 7.1 为什么 RTSP 推流需要每个 IDR 帧包含 SPS/PPS？

RTSP 是流式传输，客户端可能随时接入。如果客户端从非 IDR 帧开始接收，没有 SPS/PPS 就无法解码。每个 IDR 帧包含 SPS/PPS 确保客户端随时可以开始解码。

### 7.2 NVENC 编码器的 extradata 什么时候可用？

NVENC 编码器在 `avcodec_open2()` 后生成 extradata。但有时需要编码几帧后才能正确生成，因此代码中有从 IDR 帧提取的备用方案。

### 7.3 为什么需要 AVCC 到 AnnexB 转换？

- **NVENC 输出**: 可能是 AVCC 格式（长度前缀）
- **RTSP 要求**: AnnexB 格式（起始码）
- **转换**: 将长度前缀替换为起始码

---

## 八、调试日志示例

```
[SPS/PPS] Extradata size=56, first bytes: 1 64 0 1f
[SPS/PPS] Parsing AVCC extradata
[SPS/PPS] Extracted SPS from AVCC, len=26
[SPS/PPS] Extracted PPS from AVCC, len=8
[SPS/PPS] Successfully extracted SPS(30 bytes), PPS(12 bytes)
[Push] SPS/PPS extracted: YES

... 推流过程中 ...

[Push] Found IDR frame #1, size=45678
[SPS/PPS] Frame contains SPS/PPS: NO
[SPS/PPS] Injected into IDR frame #1

[Push] Found IDR frame #25, size=42156
[SPS/PPS] Frame contains SPS/PPS: NO
[SPS/PPS] Injected into IDR frame #25
```
