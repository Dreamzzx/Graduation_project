cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
    VideoPush 
    VERSION 1.0.0    
    LANGUAGES C CXX  
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src/safe_queue)

# FFMPEG 配置
set(FFMPEG_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/ffmpeg_7_cuda)
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
    opencv_world4120
)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # 可执行文件
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 动态库
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 静态库

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/core_pusher)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/src/video_source)

add_executable(VideoPush main.cpp)

target_link_libraries(VideoPush
    PRIVATE
    core_pusher
    video_source
    ${FFMPEG_LIBS}
    ${OPENCV_LIBS}
    pthread 
)

