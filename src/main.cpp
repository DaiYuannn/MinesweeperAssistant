#include "WindowCapture.h"
#include "GameAnalyzer.h"
#include "DisplayWindow.h"
#include <thread>
#include <atomic>
#include <iostream>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <windows.h>

std::atomic<bool> g_running(false);

void CaptureThread(WindowCapture& capture, cv::Mat& gameImage, std::mutex& imageMutex) {
    while (g_running) {
        cv::Mat frame;
        if (capture.CaptureGameArea(frame)) {
            std::lock_guard<std::mutex> lock(imageMutex);
            gameImage = frame.clone();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void AnalysisThread(WindowCapture& capture, GameAnalyzer& analyzer,
                   DisplayWindow& display, cv::Mat& gameImage, std::mutex& imageMutex) {
    GameState state;
    state.rows = 16;
    state.cols = 16;
    state.mineCount = 40;

    while (g_running) {
        cv::Mat currentImage;
        {
            std::lock_guard<std::mutex> lock(imageMutex);
            if (!gameImage.empty()) {
                currentImage = gameImage.clone();
            }
        }

        if (!currentImage.empty()) {
            if (analyzer.AnalyzeGameState(currentImage, state)) {
                display.Update(state);

                auto safeMoves = analyzer.FindSafeMoves(state);
                for (const auto& move : safeMoves) {
                    int cellW = currentImage.cols / state.cols;
                    int cellH = currentImage.rows / state.rows;
                    int x = move.x * cellW + cellW / 2;
                    int y = move.y * cellH + cellH / 2;
                    analyzer.PerformClick(capture.GetGameWindow(), x, y);
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int main() {
    // 控制台使用 UTF-8，避免中文乱码
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    WindowCapture capture;
    GameAnalyzer analyzer;
    DisplayWindow display;

    // 选择游戏窗口（当前为前台窗口）
    HWND gameHwnd = capture.SelectGameWindow();
    if (!gameHwnd) {
        std::cerr << "未能选择游戏窗口" << std::endl;
        // 仍然展示辅助窗口，便于验证 UI
        if (!display.Create()) {
            return 1;
        }
    } else {
        capture.SetGameWindow(gameHwnd);
        if (!display.Create()) {
            std::cerr << "未能创建显示窗口" << std::endl;
            return 1;
        }

        cv::Mat gameImage;
        std::mutex imageMutex;

        g_running = true;
        std::thread captureThread(CaptureThread, std::ref(capture), std::ref(gameImage), std::ref(imageMutex));
        std::thread analysisThread(AnalysisThread, std::ref(capture), std::ref(analyzer),
                                  std::ref(display), std::ref(gameImage), std::ref(imageMutex));

        // 消息循环
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // 清理
        g_running = false;
        captureThread.join();
        analysisThread.join();

        return 0;
    }

    // 简单消息循环（未启动线程情况下）
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}
