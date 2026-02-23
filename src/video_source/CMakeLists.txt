# core_pusher/CMakeLists.txt
cmake_minimum_required(VERSION 3.10)

# 定义模块库名称（建议和模块名一致，方便识别）
set(ZPUSHER_MODULE_NAME video_source)

# 收集模块的源码文件（ZPusher.cpp）
file(GLOB ZPUSHER_SOURCES *.cpp)

add_library(${ZPUSHER_MODULE_NAME} STATIC ${ZPUSHER_SOURCES})

target_include_directories(${ZPUSHER_MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 设置C++标准（和主程序保持一致）
target_compile_features(${ZPUSHER_MODULE_NAME} PRIVATE cxx_std_17)