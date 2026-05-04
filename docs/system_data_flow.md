# 智能视频分析系统数据流分析图

## 1. 系统整体架构图

```mermaid
graph TB
    subgraph "数据采集层"
        A[视频设备<br/>摄像头/RTSP] --> B[VideoSource<br/>视频采集模块]
    end
    
    subgraph "数据处理层"
        B --> C[VideoFrameQueue&lt;cv::Mat&gt;<br/>采集队列]
        C --> D[YOLOv11Detector<br/>目标检测推理引擎]
        D --> E[VideoFrameQueue&lt;FrameData&gt;<br/>检测队列]
    end
    
    subgraph "数据输出层"
        E --> F[FFmpegPush<br/>RTSP推流模块]
        E --> G[VideoRecorder<br/>视频录制模块]
        F --> H[MediaMTX<br/>RTSP服务器]
        G --> I[本地存储<br/>recordings/]
    end
    
    subgraph "Web管理层"
        J[Flask Web Server<br/>app.py] --> K[前端界面]
        J --> L[API接口]
        L --> M[流管理]
        L --> N[设备管理]
        L --> O[模型管理]
        L --> P[系统监控]
    end
    
    H --> Q[HLS流<br/>http://localhost:8888]
    Q --> K
```

## 2. 核心推理模块数据流图

```mermaid
graph LR
    subgraph "输入预处理"
        A[原始帧<br/>cv::Mat] --> B[图像缩放<br/>Letterbox]
        B --> C[填充Padding<br/>640x640]
        C --> D[归一化<br/>1/255.0]
        D --> E[通道转换<br/>BGR→RGB]
        E --> F[张量构建<br/>1x3x640x640]
    end
    
    subgraph "模型推理"
        F --> G{推理后端选择}
        G -->|ONNX Runtime| H[YOLOv11Detector<br/>ONNX]
        G -->|LibTorch| I[YOLOv11Detector<br/>Torch]
        G -->|TensorRT| J[YOLOv11RT<br/>TRT]
        H --> K[GPU/CPU推理]
        I --> K
        J --> K
    end
    
    subgraph "后处理"
        K --> L[输出解析<br/>8400个锚点]
        L --> M[置信度过滤<br/>conf_thresh]
        M --> N[类别过滤<br/>target_classes]
        N --> O[NMS非极大值抑制<br/>nms_thresh]
        O --> P[坐标映射<br/>还原到原图]
        P --> Q[检测结果<br/>Detection]
    end
```

## 3. 多线程数据流图

```mermaid
graph TB
    subgraph "ThreadPool 线程池"
        direction TB
        T1[Thread 1<br/>视频采集线程]
        T2[Thread 2<br/>推理检测线程]
        T3[Thread 3<br/>帧处理线程]
        T4[Thread 4<br/>录制监控线程]
    end
    
    subgraph "共享队列"
        Q1[capture_queue<br/>VideoFrameQueue&lt;cv::Mat&gt;]
        Q2[detect_queue<br/>VideoFrameQueue&lt;FrameData&gt;]
    end
    
    subgraph "数据流"
        V[VideoSource] -->|Push| Q1
        Q1 -->|Pop| T2
        T2 -->|Push| Q2
        Q2 -->|Pop| T3
        T3 -->|Push| F[FFmpegPush]
        T3 -->|Notify| R[VideoRecorder]
    end
    
    T1 -.->|控制| V
    T4 -.->|监控| R
```

## 4. 推理引擎类图

```mermaid
classDiagram
    class YOLOv11Detector_ONNX {
        -Ort::Env env
        -Ort::Session* session
        -Ort::MemoryInfo* memory_info
        -int input_size
        -float conf_thresh
        -float nms_thresh
        -vector~string~ class_names
        -set~int~ target_class_ids
        -VideoFrameQueue* input_queue
        -VideoFrameQueue* output_queue
        +Init() bool
        +detect(cv::Mat) vector~Detection~
        +draw_detections() cv::Mat
        +start() void
        -preprocess() vector~float~
        -postprocess() vector~Detection~
    }
    
    class YOLOv11Detector_Torch {
        -torch::jit::script::Module model
        -torch::Device device
        -vector~string~ class_names
        -int input_size
        -float conf_thresh
        -float nms_thresh
        +YOLOv11Detector()
        +detect(cv::Mat) vector~Detection~
    }
    
    class YOLOv11RT {
        -nvinfer1::ICudaEngine* engine
        -nvinfer1::IExecutionContext* context
        -void* device_input_buffer
        -void* device_output_buffer
        -Logger logger
        +init() bool
        -buildEngine() bool
        -loadEngine() bool
        -preprocess() void
        -postprocess() vector~Detection~
    }
    
    class Detection {
        +cv::Rect bbox
        +float confidence
        +int class_id
        +string class_name
    }
    
    class FrameData {
        +cv::Mat frame
        +vector~Detection~ detections
        +int64_t timestamp_ms
        +int64_t frame_id
        +bool has_person
    }
    
    YOLOv11Detector_ONNX --> Detection : 生成
    YOLOv11Detector_Torch --> Detection : 生成
    YOLOv11RT --> Detection : 生成
    FrameData --> Detection : 包含
```

## 5. Web API数据流图

```mermaid
sequenceDiagram
    participant 前端
    participant Flask
    participant StreamManager
    participant VideoPush
    participant MediaMTX
    
    前端->>Flask: POST /api/streams (创建流配置)
    Flask->>StreamManager: save_stream()
    StreamManager-->>Flask: 配置已保存
    
    前端->>Flask: POST /api/streams/{id}/start
    Flask->>StreamManager: start_stream_process()
    StreamManager->>VideoPush: 启动VideoPush.exe
    VideoPush->>MediaMTX: RTSP推流
    MediaMTX-->>前端: HLS流可用
    
    前端->>Flask: GET /api/streams/{id}/status
    Flask->>StreamManager: get_stream_status()
    StreamManager-->>Flask: 运行状态
    
    前端->>Flask: POST /api/streams/{id}/stop
    Flask->>StreamManager: stop_stream_process()
    StreamManager->>VideoPush: 终止进程
```

## 6. 配置数据流图

```mermaid
graph TB
    subgraph "配置加载"
        A[config.json] --> B[Config::load]
        B --> C[CaptureConfig]
        B --> D[PushConfig]
        B --> E[DetectorConfig]
        B --> F[RecorderConfig]
    end
    
    subgraph "模块初始化"
        C --> G[VideoSource初始化]
        D --> H[FFmpegPush初始化]
        E --> I[YOLOv11Detector初始化]
        F --> J[VideoRecorder初始化]
    end
    
    subgraph "运行时参数"
        G --> K[设备名称/分辨率/帧率]
        H --> L[RTSP URL/码率/分辨率]
        I --> M[模型路径/类别/阈值]
        J --> N[输出目录/超时时间]
    end
```

## 7. 完整系统时序图

```mermaid
sequenceDiagram
    participant User as 用户
    participant Web as Web界面
    participant API as Flask API
    participant ZP as ZPusher
    participant VS as VideoSource
    participant Det as Detector
    participant Push as FFmpegPush
    participant Rec as Recorder
    participant Media as MediaMTX
    
    User->>Web: 配置流参数
    Web->>API: POST /api/streams
    API->>ZP: Init(config_path)
    ZP->>VS: 初始化视频源
    ZP->>Det: 初始化检测器
    ZP->>Push: 初始化推流器
    ZP->>Rec: 初始化录制器
    
    User->>Web: 启动流
    Web->>API: POST /api/streams/{id}/start
    API->>ZP: start_Push()
    
    loop 视频处理循环
        VS->>VS: 采集帧
        VS->>Det: Push到capture_queue
        Det->>Det: 推理检测
        Det->>Push: Push到detect_queue
        Push->>Push: 编码帧
        Push->>Media: RTSP推流
        Media->>Web: HLS播放
        
        alt 检测到目标
            Det->>Rec: has_person=true
            Rec->>Rec: 开始录制
        else 未检测到目标
            Det->>Rec: has_person=false
            Rec->>Rec: 停止录制
        end
    end
```

## 8. 数据结构关系图

```mermaid
erDiagram
    FRAME_DATA {
        cv::Mat frame
        vector~Detection~ detections
        int64_t timestamp_ms
        int64_t frame_id
        bool has_person
    }
    
    DETECTION {
        cv::Rect bbox
        float confidence
        int class_id
        string class_name
    }
    
    CAPTURE_CONFIG {
        string device_name
        int video_width
        int video_height
        int framerate
    }
    
    PUSH_CONFIG {
        string rtsp_url
        int video_width
        int video_height
        int video_bitrate
        int frame_rate
    }
    
    DETECTOR_CONFIG {
        bool enabled
        string model_path
        vector~string~ classes
        float confidence_threshold
        int input_width
    }
    
    RECORDER_CONFIG {
        bool enabled
        string output_dir
        int person_leave_timeout_ms
    }
    
    FRAME_DATA ||--o{ DETECTION : contains
    CAPTURE_CONFIG ||--|| VideoSource : configures
    PUSH_CONFIG ||--|| FFmpegPush : configures
    DETECTOR_CONFIG ||--|| YOLOv11Detector : configures
    RECORDER_CONFIG ||--|| VideoRecorder : configures
```

## 说明

### 系统架构概述

该智能视频分析系统采用**生产者-消费者模式**，通过**多线程**和**共享队列**实现高效的视频处理流水线：

1. **视频采集线程**：使用FFmpeg从视频设备采集帧，支持硬件加速解码
2. **推理检测线程**：使用YOLOv11模型进行目标检测，支持ONNX/LibTorch/TensorRT三种后端
3. **帧处理线程**：将检测结果绘制到帧上，并推送到输出队列
4. **推流线程**：使用FFmpeg将处理后的帧编码并推送到RTSP服务器
5. **录制线程**：根据检测结果自动录制视频

### 数据流特点

- **异步处理**：各模块通过队列解耦，实现异步并行处理
- **帧丢弃策略**：队列满时自动丢弃旧帧，保证实时性
- **硬件加速**：支持CUDA加速的编解码和推理
- **灵活配置**：通过JSON配置文件灵活调整各模块参数
