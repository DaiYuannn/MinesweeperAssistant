#ifndef WINDOW_CAPTURE_H
#define WINDOW_CAPTURE_H

#include <windows.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <atomic>

class WindowCapture {
public:
    WindowCapture();
    ~WindowCapture();

    HWND SelectGameWindow();
    bool CaptureGameArea(cv::Mat& output);
    bool IdentifyGameBounds(const cv::Mat& screenCapture, cv::Rect& gameRect);
    bool AnalyzeGridLayout(const cv::Mat& gameArea, int& rows, int& cols);
    bool RefineBoardArea(const cv::Mat& roiImage, cv::Rect& gridRect);
    // 新增：更精确的网格布局识别，输出行列数以及裁剪后的纯棋盘内矩形
    bool AnalyzeGridLayoutEx(const cv::Mat& boardImage, int& rows, int& cols, cv::Rect& innerRect);
    // 新增：提取 HUD 计时器的签名；用于检测计时器变化触发重识别
    bool ExtractHudTimerSignature(const cv::Mat& roiImage, uint64_t& signature);
    // 新增：比较 HUD 是否变化（内部保存上一帧签名）
    bool HasHudChanged(const cv::Mat& roiImage);

    void SetGameWindow(HWND hwnd) { m_gameHwnd = hwnd; }
    HWND GetGameWindow() const { return m_gameHwnd; }
    const std::wstring& GetLastCaptureMethod() const { return m_lastCaptureMethod; }
    const std::wstring& GetLastHudMethod() const { return m_lastHudMethod; }
    // HUD 顶部高度比例（百分比，默认 35）
    void SetHudTopRatioPercent(int p);
    int GetHudTopRatioPercent() const;

private:
    HWND m_gameHwnd;
    cv::Rect m_gameRect;
    int m_rows, m_cols;
    std::wstring m_lastCaptureMethod; // "PW" or "BitBlt"
    std::wstring m_lastHudMethod; // "red" or "edges" or "none"
    // HUD 签名缓存
    uint64_t m_lastHudSignature = 0;
    bool m_hasHudSignature = false;
    std::atomic<int> m_hudTopRatioPercent; // 35 by default
};

#endif
