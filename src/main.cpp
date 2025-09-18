#include "WindowCapture.h"
#include "GameAnalyzer.h"
#include "DisplayWindow.h"
#include "WindowSelector.h"
#include "OverlayWindow.h"
#include "Logger.h"
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

    bool snapped = false;
    cv::Rect lastRegion;

    while (g_running) {
        cv::Mat currentImage;
        {
            std::lock_guard<std::mutex> lock(imageMutex);
            if (!gameImage.empty()) {
                currentImage = gameImage.clone();
            }
        }

        if (!currentImage.empty()) {
            // 尝试识别操作区域并吸附（仅首次或区域变化较大时）
            if (!snapped) {
                cv::Rect region;
                if (capture.IdentifyGameBounds(currentImage, region)) {
                    // 将 region 转为屏幕坐标：当前 CaptureGameArea 获取的是客户区图像，坐标相对客户区
                    POINT clientTopLeft{0,0}; ClientToScreen(capture.GetGameWindow(), &clientTopLeft);
                    RECT screenRect{ clientTopLeft.x + region.x, clientTopLeft.y + region.y,
                                     clientTopLeft.x + region.x + region.width,
                                     clientTopLeft.y + region.y + region.height };
                    display.SnapNear(screenRect);
                    snapped = true;
                    lastRegion = region;
                }
            }
            if (analyzer.AnalyzeGameState(currentImage, state)) {
                display.Update(state);

                auto safeMoves = analyzer.FindSafeMoves(state);
                    state.safeCells = safeMoves; // 供渲染高亮
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

    // 里程碑1：启动自动识别，失败则前台窗口作为候选
    HWND gameHwnd = WindowSelector::AutoPick();
    if (!gameHwnd) {
        LOGI("AutoPick 未命中，使用前台窗口作为候选");
        gameHwnd = WindowSelector::PickForeground();
    }
    if (!gameHwnd) {
        std::cerr << "未能选择游戏窗口" << std::endl;
        // 仍然展示辅助窗口，便于验证 UI
        if (!display.Create()) {
            return 1;
        }
        display.SetTopMost(true);
    } else {
        capture.SetGameWindow(gameHwnd);
        if (!display.Create()) {
            std::cerr << "未能创建显示窗口" << std::endl;
            return 1;
        }
        display.SetTopMost(true);

    cv::Mat gameImage;
        std::mutex imageMutex;

        g_running = true;
        std::thread captureThread(CaptureThread, std::ref(capture), std::ref(gameImage), std::ref(imageMutex));
        std::thread analysisThread(AnalysisThread, std::ref(capture), std::ref(analyzer),
                                  std::ref(display), std::ref(gameImage), std::ref(imageMutex));

        // 不再根据目标窗口尺寸自动调整显示窗口；保持小窗模式

        // 注册热键 F8：手动拖拽选择游戏窗口
        RegisterHotKey(NULL, 1, 0, VK_F8);

        // 消息循环
        MSG msg;
        while (GetMessage(&msg, NULL, 0, 0)) {
            if (msg.message == WM_HOTKEY && msg.wParam == 1) {
                // 暂停抓取，弹出覆盖层
                g_running = false;
                captureThread.join();
                analysisThread.join();

                OverlayWindow overlay;
                HWND selected = overlay.SelectBlocking();
                if (selected) {
                    capture.SetGameWindow(selected);
                    // 重新启动线程
                    g_running = true;
                    captureThread = std::thread(CaptureThread, std::ref(capture), std::ref(gameImage), std::ref(imageMutex));
                    analysisThread = std::thread(AnalysisThread, std::ref(capture), std::ref(analyzer),
                                                std::ref(display), std::ref(gameImage), std::ref(imageMutex));
                } else {
                    // 未选择则恢复线程继续
                    g_running = true;
                    captureThread = std::thread(CaptureThread, std::ref(capture), std::ref(gameImage), std::ref(imageMutex));
                    analysisThread = std::thread(AnalysisThread, std::ref(capture), std::ref(analyzer),
                                                std::ref(display), std::ref(gameImage), std::ref(imageMutex));
                }
            } else {
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }

    // 清理
        g_running = false;
        captureThread.join();
        analysisThread.join();
    UnregisterHotKey(NULL, 1);

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
