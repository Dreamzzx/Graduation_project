cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
    VideoPush 
    VERSION 1.0.0    
    LANGUAGES C CXX  
)
find_package(CUDAToolkit REQUIRED)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/safe_queue)

# FFMPEG 配置
set(FFMPEG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg_7.1_cuda)
include_directories(${FFMPEG_ROOT}/include)
link_directories(${FFMPEG_ROOT}/lib)
set(FFMPEG_LIBS
    avcodec  # 对应avcodec-xx.dll
    avformat     # 对应avformat-61.dll
    avutil    # 对应avutil-59.dll
    avfilter     # 对应avfilter-10.dll
    swscale     # 对应swscale-8.dll
    swresample    # 对应swresample-5.dll
    avdevice    # 对应avdevice-59.dll
)

# OpenCV 配置
set(OPENCV_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/opencv)
include_directories(${OPENCV_ROOT}/include)
link_directories(${OPENCV_ROOT}/lib)
set(OPENCV_LIBS
    opencv_world4120d
)

# #TensorRT 配置
# set(TENSORRT_ROOT D:/TensorRT-10.15.1.29)
# include_directories(${TENSORRT_ROOT}/include)
# link_directories(${TENSORRT_ROOT}/lib)
# set(TENSORRT_LIBS
#     nvinfer_10
#     nvinfer_plugin_10
#     nvonnxparser_10
# )

# onnxruntime 配置
set(ONNXRUNTIME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/onnxruntime)
include_directories(${ONNXRUNTIME_ROOT}/include)
link_directories(${ONNXRUNTIME_ROOT}/lib)
set(ONNXRUNTIME_LIBS
    onnxruntime
    onnxruntime_providers_cuda
    onnxruntime_providers_shared
)

# # libtorch 配置
# set(LIBTORCH_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/libtorch)
# include_directories(${LIBTORCH_ROOT}/include)
# include_directories(${LIBTORCH_ROOT}/include/torch/csrc/api/include)
# link_directories(${LIBTORCH_ROOT}/lib)
# set(LIBTORCH_LIBS
#     torch
#     torch_cpu
#     torch_cuda
#     c10
# )

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # 可执行文件
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 动态库
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 静态库

# 头文件
include_directories(
    ${CMAKE_SOURCE_DIR}/src/core_zpusher
    ${CMAKE_SOURCE_DIR}/src/video_source
    ${CMAKE_SOURCE_DIR}/src/core_onnx
    ${CMAKE_SOURCE_DIR}/src/core_thread
    ${CMAKE_SOURCE_DIR}/src/safe_queue
    ${CMAKE_SOURCE_DIR}/src/core_push
    ${CMAKE_SOURCE_DIR}/src/core_recorder
)

add_executable(VideoPush main.cpp
    src/core_zpusher/Zpusher.cpp
    src/video_source/VideoSource.cpp
    src/core_onnx/yolov11_onnx.cpp
    src/core_thread/ThreadPool.cpp
    src/core_push/video_push.cpp
    src/core_config/Config.cpp
    src/core_recorder/VideoRecorder.cpp
)

target_link_libraries(VideoPush
    PRIVATE
    ${FFMPEG_LIBS}
    ${OPENCV_LIBS}
    ${ONNXRUNTIME_LIBS}
    CUDA::cudart
)

