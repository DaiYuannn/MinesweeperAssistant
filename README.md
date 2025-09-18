# MinesweeperAssistant

基于 C++/CMake/OpenCV 的 Windows 扫雷助手（骨架项目）。

## 先决条件
- Windows 10/11
- MSYS2（安装 mingw-w64-x86_64-toolchain、cmake、opencv）
- VS Code 及扩展：C/C++、CMake、CMake Tools

## 本地构建
1. 打开 VS Code，确保 MSYS2/mingw64 的 `bin` 在 PATH 中。
2. 配置与构建：
   - 使用 CMake Tools 扩展选择生成器为 `MSYS Makefiles`，构建目录 `${workspaceFolder}/build`。
   - 点击“配置”然后“构建”。

也可手动：
```
# 在 cmd.exe 中（推荐）
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -- -j %NUMBER_OF_PROCESSORS%

# 或在 MSYS2 bash 中
mkdir -p build && cd build
cmake -G "MSYS Makefiles" ..
make -j$(nproc)
```

生成的可执行文件位于 `build/bin/MinesweeperAssistant.exe`。

## 运行
双击或在终端运行 `build/bin/MinesweeperAssistant.exe`。首次会选择当前前台窗口作为“游戏窗口”，并展示一个简单的状态窗口。

## 目录结构
```
MinesweeperAssistant/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── WindowCapture.cpp
│   ├── WindowCapture.h
│   ├── GameAnalyzer.cpp
│   ├── GameAnalyzer.h
│   ├── DisplayWindow.cpp
│   ├── DisplayWindow.h
│   └── GameState.h
├── resources/
│   └── templates/
└── build/
```

## 备注
- 当前项目提供基础框架，图像识别与自动操作逻辑为占位实现，后续可逐步完善。
- 如需打包，请确保 OpenCV 对应的 DLL 与可执行文件放在同一目录。
