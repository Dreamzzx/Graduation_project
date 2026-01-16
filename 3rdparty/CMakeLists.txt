# # 3rd/CMakeLists.txt
# cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

# # 1. 定义第三方库总目标名（对外暴露的统一接口）
# set(3RD_PARTY_TARGET 3rdparty)
# add_library(${3RD_PARTY_TARGET} INTERFACE)

# # 2. 配置：指定需要加载的第三方库列表（按需增删）
# # 格式："库目录名;库目标名"，比如ffmpeg_7_cuda对应目标名ffmpeg_7_cuda
# set(THIRD_PARTY_LIBS
#     "ffmpeg_7_cuda;ffmpeg_7_cuda"  # 库目录;库目标名
# )

# # 3. 遍历加载所有第三方库子目录
# foreach(lib_info ${THIRD_PARTY_LIBS})
#     # 拆分库目录和目标名（按;分割）
#     string(REPLACE ";" " " lib_info_list ${lib_info})
#     list(GET lib_info_list 0 lib_dir)
#     list(GET lib_info_list 1 lib_target)

#     # 检查子目录是否存在（容错：缺失库时提示，不中断编译）
#     set(lib_full_dir ${CMAKE_CURRENT_SOURCE_DIR}/${lib_dir})
#     if(NOT EXISTS ${lib_full_dir}/CMakeLists.txt)
#         message(WARNING "⚠️ 第三方库 ${lib_dir} 缺失：${lib_full_dir}/CMakeLists.txt 不存在，跳过加载")
#         continue()
#     endif()

#     # 加载当前第三方库的CMake（执行子目录的CMakeLists.txt）
#     add_subdirectory(${lib_dir})

#     # 将当前库的依赖关联到总目标（对外暴露）
#     target_link_libraries(${3RD_PARTY_TARGET}
#         INTERFACE ${lib_target}
#     )

#     # 打印日志（确认加载成功）
#     message(STATUS "✅ 第三方库加载成功：${lib_dir} (目标名：${lib_target})")
# endforeach()


# message(STATUS "✅ 所有第三方库加载完成，总目标名：${3RD_PARTY_TARGET}")