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
)

# 可执行文件
add_executable(MinesweeperAssistant ${SRC})

# 链接库
if(OpenCV_FOUND)
    target_link_libraries(MinesweeperAssistant ${OpenCV_LIBS})
endif()

if (WIN32)
    target_link_libraries(MinesweeperAssistant user32 gdi32)
endif()

# 输出目录
set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
