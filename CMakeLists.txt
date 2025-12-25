
cmake_minimum_required(VERSION 3.10 FATAL_ERROR)

project(
    VideoPush  # 替换为你的项目名（比如VideoDemo）
    VERSION 1.0.0    # 项目版本（可选）
    LANGUAGES C CXX  # 支持的语言
)

# 3. 全局配置
# 3.1 设置C++标准（按需改：11/14/17/20，建议17），强制生效
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # 禁用编译器扩展（比如GNU扩展）

# 3.2 设置编译警告（提高代码健壮性，可选但推荐）
if(MSVC)  # Windows VS编译器
    add_compile_options(/W4 /WX)  # W4=最高警告级别，WX=警告视为错误
else()    # Linux/macOS (GCC/Clang)
    add_compile_options(-Wall -Wextra -Werror -O2)  # 全警告+警告转错误+优化
endif()

# 3.3 设置输出目录（统一二进制/库文件路径，避免混乱）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)  # 可执行文件
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 动态库
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)  # 静态库

file(GLOB_RECURSE PROJECT_SOURCES
    src/main.cpp
)

# 4.3 创建可执行程序（替换为你的程序名，比如demo_app）
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# 4.4 链接库（如果依赖第三方库/自定义库，比如：pthread、opencv、自己编译的库）
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    pthread  # 示例：链接线程库（Linux）
    # opencv_core opencv_highgui  # 示例：链接OpenCV（需先find_package(OpenCV)）
    # ${CMAKE_CURRENT_SOURCE_DIR}/lib/libxxx.so  # 示例：链接自定义动态库
)

