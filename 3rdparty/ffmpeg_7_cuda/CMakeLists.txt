cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

set(FFMPEG_TARGET_NAME ffmpeg_7_cuda)

# 接口库
add_library(${FFMPEG_TARGET_NAME} INTERFACE)

set(FFMPEG_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/include)
set(FFMPEG_LIB ${CMAKE_CURRENT_SOURCE_DIR}/lib)

file(GLOB FFMPEG_LIBS ${FFMPEG_LIB}/*.lib)

target_include_directories(${FFMPEG_TARGET_NAME} INTERFACE ${FFMPEG_INCLUDE})
target_link_directories(${FFMPEG_TARGET_NAME} INTERFACE ${FFMPEG_LIB})
target_link_libraries(${FFMPEG_TARGET_NAME} INTERFACE ${FFMPEG_LIBS})