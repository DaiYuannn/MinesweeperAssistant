#include "WindowCapture.h"
#include "GameAnalyzer.h"
#include "DisplayWindow.h"
#include "WindowSelector.h"
#include "OverlayWindow.h"
#include "Logger.h"
#include <thread>
#include <atomic>
#include <iostream>
#include <sstream>
#include <mutex>
#include <opencv2/opencv.hpp>
#include <windows.h>
#include <random>
#include <algorithm>

std::atomic<bool> g_running(false);
std::atomic<bool> g_enableMouseMove(false); // 默认不控制鼠标
std::atomic<double> g_captureFps(0.0);
std::atomic<double> g_analyzeMs(0.0);
std::atomic<DWORD> g_lastRelayoutTick(0);
const DWORD kRelayoutMinIntervalMs = 600; // 节流最小间隔
// 自动点击控制
std::atomic<bool> g_enableAutoClick(false);
std::atomic<int> g_clickIntervalMs(200);      // 基础间隔 ms
std::atomic<int> g_clickRandomMs(50);         // 间隔随机抖动 ±ms
std::atomic<int> g_clickPosJitterPx(1);       // 点击坐标抖动 ±px
std::atomic<DWORD> g_lastClickTick(0);

void CaptureThread(WindowCapture& capture, cv::Mat& gameImage, std::mutex& imageMutex) {
    using clock = std::chrono::steady_clock;
    auto lastReport = clock::now();
    int frames = 0;
    while (g_running) {
        cv::Mat frame;
        if (capture.CaptureGameArea(frame)) {
            std::lock_guard<std::mutex> lock(imageMutex);
            gameImage = frame.clone();
            frames++;
        }
        auto now = clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastReport).count();
        if (ms >= 1000) {
            g_captureFps.store(frames * 1000.0 / ms);
            frames = 0;
            lastReport = now;
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
    // 上一帧识别结果用于投票
    GameState prevState = state;

    bool snapped = false;
    cv::Rect lastRegion;
    SIZE lastClientSize{0,0};

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
            // 选择 ROI（如有）
            cv::Rect roiToUse(0, 0, currentImage.cols, currentImage.rows);
            if (lastRegion.width > 0 && lastRegion.height > 0) {
                // 防越界
                cv::Rect imgRect(0,0,currentImage.cols,currentImage.rows);
                roiToUse = lastRegion & imgRect;
            }

            cv::Mat imgForAnalysis = currentImage(roiToUse).clone();
            // 细化棋盘区域，剔除顶部 HUD（雷数/计时器）
            cv::Rect gridRect;
            bool refined = capture.RefineBoardArea(imgForAnalysis, gridRect);
            if (refined) {
                // 叠加到客户区坐标
                gridRect.x += roiToUse.x;
                gridRect.y += roiToUse.y;
                // 用细化后的区域替代 roiToUse
                cv::Rect imgRect2(0,0,currentImage.cols,currentImage.rows);
                roiToUse = (gridRect & imgRect2);
                imgForAnalysis = currentImage(roiToUse).clone();
            }

            // 触发：HUD 变化 或 窗口客户区尺寸变化，且满足节流
            static bool firstLayout = true;
            bool hudChanged = capture.HasHudChanged(currentImage);
            // 窗口尺寸变化监测
            HWND hw = capture.GetGameWindow();
            RECT crc{}; GetClientRect(hw, &crc);
            SIZE curSize{ crc.right-crc.left, crc.bottom-crc.top };
            bool sizeChanged = (curSize.cx != lastClientSize.cx || curSize.cy != lastClientSize.cy);
            if (sizeChanged) lastClientSize = curSize;

            DWORD now = GetTickCount();
            DWORD lastTick = g_lastRelayoutTick.load();
            bool throttled = (now - lastTick < kRelayoutMinIntervalMs);

            if (!throttled && (firstLayout || hudChanged || sizeChanged)) {
                int rows=0, cols=0; cv::Rect inner;
                if (capture.AnalyzeGridLayoutEx(imgForAnalysis, rows, cols, inner) && rows>0 && cols>0) {
                    // 转回客户区坐标
                    inner.x += roiToUse.x;
                    inner.y += roiToUse.y;
                    // 更新 ROI 与 state 行列
                    cv::Rect imgRect2(0,0,currentImage.cols,currentImage.rows);
                    roiToUse = (inner & imgRect2);
                    imgForAnalysis = currentImage(roiToUse).clone();
                    state.rows = rows; state.cols = cols;
                    firstLayout = false;
                    g_lastRelayoutTick.store(now);
                }
            }

            auto t0 = std::chrono::steady_clock::now();
            if (analyzer.AnalyzeGameState(imgForAnalysis, state)) {
                // 多帧投票：若本帧识别为未知(9)，上一帧非未知，则沿用上一帧；若两帧不一致且都非未知，保留上一帧（保守）
                if (prevState.rows == state.rows && prevState.cols == state.cols) {
                    for (int r=0;r<state.rows;++r){
                        for (int c=0;c<state.cols;++c){
                            int cur = state.grid[r][c];
                            int prv = prevState.grid[r][c];
                            if (cur == 9 && prv != 9) state.grid[r][c] = prv;
                            else if (cur != 9 && prv != 9 && cur != prv) state.grid[r][c] = prv; // 稳定优先
                        }
                    }
                }
                prevState = state;
                auto t1 = std::chrono::steady_clock::now();
                g_analyzeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());
                display.Update(state);

                auto safeMoves = analyzer.FindSafeMoves(state);
                state.safeCells = safeMoves; // 供渲染高亮
                // 自动点击：每个周期最多点击一个安全格；遵守间隔与随机抖动
                if (!safeMoves.empty() && g_enableAutoClick.load()) {
                    DWORD now = GetTickCount();
                    int base = std::max(0, g_clickIntervalMs.load());
                    int jitter = std::max(0, g_clickRandomMs.load());
                    static thread_local std::mt19937 rng{ std::random_device{}() };
                    std::uniform_int_distribution<int> dj(-jitter, jitter);
                    int eff = base + (jitter>0 ? dj(rng) : 0);
                    if (now - g_lastClickTick.load() >= (DWORD)std::max(0, eff)) {
                        cv::Point move = safeMoves.front();
                        int cellW = imgForAnalysis.cols / state.cols;
                        int cellH = imgForAnalysis.rows / state.rows;
                        int localX = move.x * cellW + cellW / 2;
                        int localY = move.y * cellH + cellH / 2;
                        int posJ = std::max(0, g_clickPosJitterPx.load());
                        if (posJ > 0) {
                            std::uniform_int_distribution<int> jp(-posJ, posJ);
                            localX = std::clamp(localX + jp(rng), move.x*cellW + 1, (move.x+1)*cellW - 1);
                            localY = std::clamp(localY + jp(rng), move.y*cellH + 1, (move.y+1)*cellH - 1);
                        }
                        int x = roiToUse.x + localX;
                        int y = roiToUse.y + localY;
                        analyzer.PerformClick(capture.GetGameWindow(), x, y);
                        g_lastClickTick.store(now);
                    }
                }

                // 更新状态栏：窗口信息 + FPS/耗时 + 网格信息
                HWND h = capture.GetGameWindow();
                wchar_t title[256]{}; GetWindowTextW(h, title, 255);
                wchar_t cls[128]{}; GetClassNameW(h, cls, 127);
                RECT rcClient{}; GetClientRect(h, &rcClient);
                int cellW = (state.cols>0)? (imgForAnalysis.cols / state.cols) : 0;
                int cellH = (state.rows>0)? (imgForAnalysis.rows / state.rows) : 0;
                std::wstringstream ss;
                ss.setf(std::ios::fixed); ss.precision(1);
                             ss << L"窗口: " << title << L"  类: " << cls
                   << L"  客户区: " << (rcClient.right-rcClient.left) << L"x" << (rcClient.bottom-rcClient.top)
                                 << L"\nROI: " << roiToUse.width << L"x" << roiToUse.height
                             << L"  Capture: " << capture.GetLastCaptureMethod()
                                 << L"  HUD: " << capture.GetLastHudMethod()
                             << L"  Grid: " << state.rows << L"x" << state.cols
                             << L"  Cell: " << cellW << L"x" << cellH
                              << L"  Auto: " << (g_enableAutoClick.load()? L"ON" : L"OFF")
                              << L"  Intv: " << g_clickIntervalMs.load() << L"±" << g_clickRandomMs.load() << L"ms"
                              << L"  Jit: ±" << g_clickPosJitterPx.load() << L"px"
                             << L"  Mouse: " << (g_enableMouseMove.load()? L"ON" : L"OFF")
                             << L"  FPS: " << g_captureFps.load() << L"  分析: " << g_analyzeMs.load() << L" ms  (F8 选择 | F9 鼠标 | F10 自动 | F11/F12 间隔 | F6/F7 随机 | F3/F4 坐标抖动 | +/- HUD%)";
                display.SetStatusText(ss.str());
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {

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
    // 未能选择游戏窗口
        // 仍然展示辅助窗口，便于验证 UI
        if (!display.Create()) {
            return 1;
        }
        display.SetTopMost(true);
        display.SetStatusText(L"状态: 未绑定窗口 (F8 选择窗口)");
    } else {
        capture.SetGameWindow(gameHwnd);
        if (!display.Create()) {
            // 未能创建显示窗口
            return 1;
        }
        display.SetTopMost(true);

        // 显示目标窗口信息
        wchar_t title[256]{}; GetWindowTextW(gameHwnd, title, 255);
        wchar_t cls[128]{}; GetClassNameW(gameHwnd, cls, 127);
        RECT rcClient{}; GetClientRect(gameHwnd, &rcClient);
        std::wstringstream ss;
        ss << L"窗口: " << title << L"  类: " << cls
           << L"  客户区: " << (rcClient.right-rcClient.left) << L"x" << (rcClient.bottom-rcClient.top)
           << L"  (F8 重新选择)";
        display.SetStatusText(ss.str());

    cv::Mat gameImage;
        std::mutex imageMutex;

        g_running = true;
        std::thread captureThread(CaptureThread, std::ref(capture), std::ref(gameImage), std::ref(imageMutex));
        std::thread analysisThread(AnalysisThread, std::ref(capture), std::ref(analyzer),
                                  std::ref(display), std::ref(gameImage), std::ref(imageMutex));

        // 不再根据目标窗口尺寸自动调整显示窗口；保持小窗模式

    // 注册热键 F8：手动拖拽选择游戏窗口；F9：切换鼠标控制
        RegisterHotKey(NULL, 1, 0, VK_F8);
    RegisterHotKey(NULL, 2, 0, VK_F9);
    // 注册 + / - 用于调整 HUD 顶部高度比例
    RegisterHotKey(NULL, 3, 0, VK_OEM_PLUS);
    RegisterHotKey(NULL, 4, 0, VK_OEM_MINUS);
    // 自动点击相关热键
    RegisterHotKey(NULL, 5, 0, VK_F10); // 开关自动点击
    RegisterHotKey(NULL, 6, 0, VK_F11); // 间隔 -
    RegisterHotKey(NULL, 7, 0, VK_F12); // 间隔 +
    RegisterHotKey(NULL, 8, 0, VK_F6);  // 随机 -
    RegisterHotKey(NULL, 9, 0, VK_F7);  // 随机 +
    RegisterHotKey(NULL, 10, 0, VK_F3); // 坐标抖动 -
    RegisterHotKey(NULL, 11, 0, VK_F4); // 坐标抖动 +

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
                    wchar_t t[256]{}; GetWindowTextW(selected, t, 255);
                    wchar_t c[128]{}; GetClassNameW(selected, c, 127);
                    RECT cr{}; GetClientRect(selected, &cr);
                    std::wstringstream s2;
                    s2 << L"窗口: " << t << L"  类: " << c
                       << L"  客户区: " << (cr.right-cr.left) << L"x" << (cr.bottom-cr.top)
                       << L"  (F8 重新选择)";
                    display.SetStatusText(s2.str());
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
            } else if (msg.message == WM_HOTKEY && msg.wParam == 2) {
                // 切换鼠标控制
                bool now = !g_enableMouseMove.load();
                g_enableMouseMove.store(now);
                // 触发状态栏更新（AnalysisThread 周期性刷新会覆盖，但这里也可轻触）
                display.SetStatusText(L"鼠标控制已切换，等待刷新...");
            } else if (msg.message == WM_HOTKEY && (msg.wParam == 3 || msg.wParam == 4)) {
                // HUD 顶部高度比例调整
                int cur = capture.GetHudTopRatioPercent();
                if (msg.wParam == 3) cur = std::min(70, cur + 5);
                else cur = std::max(10, cur - 5);
                capture.SetHudTopRatioPercent(cur);
                // 轻触状态提示
                std::wstringstream s;
                s << L"HUD 顶部比例: " << cur << L"% (等待刷新)";
                display.SetStatusText(s.str());
            } else if (msg.message == WM_HOTKEY && msg.wParam == 5) {
                // 自动点击开关
                bool v = !g_enableAutoClick.load(); g_enableAutoClick.store(v);
                std::wstringstream s; s << L"自动点击: " << (v? L"ON" : L"OFF");
                display.SetStatusText(s.str());
            } else if (msg.message == WM_HOTKEY && (msg.wParam == 6 || msg.wParam == 7)) {
                int cur = g_clickIntervalMs.load();
                if (msg.wParam == 6) cur = std::max(50, cur - 50);
                else cur = std::min(2000, cur + 50);
                g_clickIntervalMs.store(cur);
                std::wstringstream s; s << L"点击间隔: " << cur << L" ms"; display.SetStatusText(s.str());
            } else if (msg.message == WM_HOTKEY && (msg.wParam == 8 || msg.wParam == 9)) {
                int cur = g_clickRandomMs.load();
                if (msg.wParam == 8) cur = std::max(0, cur - 10);
                else cur = std::min(1000, cur + 10);
                g_clickRandomMs.store(cur);
                std::wstringstream s; s << L"随机间隔: ±" << cur << L" ms"; display.SetStatusText(s.str());
            } else if (msg.message == WM_HOTKEY && (msg.wParam == 10 || msg.wParam == 11)) {
                int cur = g_clickPosJitterPx.load();
                if (msg.wParam == 10) cur = std::max(0, cur - 1);
                else cur = std::min(10, cur + 1);
                g_clickPosJitterPx.store(cur);
                std::wstringstream s; s << L"坐标抖动: ±" << cur << L" px"; display.SetStatusText(s.str());
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
    UnregisterHotKey(NULL, 2);
    UnregisterHotKey(NULL, 3);
    UnregisterHotKey(NULL, 4);
    UnregisterHotKey(NULL, 5);
    UnregisterHotKey(NULL, 6);
    UnregisterHotKey(NULL, 7);
    UnregisterHotKey(NULL, 8);
    UnregisterHotKey(NULL, 9);
    UnregisterHotKey(NULL, 10);
    UnregisterHotKey(NULL, 11);

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
