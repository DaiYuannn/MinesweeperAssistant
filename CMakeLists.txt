cmake_minimum_required(VERSION 3.20)
project(MinesweeperAssistant)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 为 VS Code 提供编译数据库，改进 IntelliSense 头文件解析
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找 OpenCV
find_package(OpenCV REQUIRED)
include_directories(${OpenCV_INCLUDE_DIRS})

# 源文件
set(SRC
    src/main.cpp
    src/WindowCapture.cpp
    src/GameAnalyzer.cpp
    src/DisplayWindow.cpp
    src/WindowSelector.cpp
    src/Logger.cpp
    src/OverlayWindow.cpp
)

# 可执行文件（WIN32 以 GUI 子系统启动，隐藏控制台窗口）
add_executable(MinesweeperAssistant WIN32 ${SRC})

# 链接库
if(OpenCV_FOUND)
    target_link_libraries(MinesweeperAssistant ${OpenCV_LIBS})
endif()

if (WIN32)
    target_link_libraries(MinesweeperAssistant user32 gdi32 Dwmapi)
    # 使用宽字符入口 wWinMain 需要 -municode（MinGW）
    if (MINGW)
        target_link_options(MinesweeperAssistant PRIVATE -municode)
    endif()
    # 全局启用 Unicode 宏，确保调用宽字符版 Win32 API
    target_compile_definitions(MinesweeperAssistant PRIVATE UNICODE _UNICODE)
endif()

# 输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
