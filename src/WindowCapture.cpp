#include "WindowCapture.h"
#include <iostream>

WindowCapture::WindowCapture() : m_gameHwnd(NULL), m_rows(0), m_cols(0) {}
WindowCapture::~WindowCapture() {}

HWND WindowCapture::SelectGameWindow() {
    // 简化选择逻辑：默认选择当前前台窗口
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        std::cerr << "无法获取前台窗口" << std::endl;
        return NULL;
    }
    m_gameHwnd = hwnd;
    std::cout << "已选择窗口: " << m_gameHwnd << std::endl;
    return m_gameHwnd;
}

bool WindowCapture::CaptureGameArea(cv::Mat& output) {
    if (!m_gameHwnd) return false;

    RECT rect;
    if (!GetClientRect(m_gameHwnd, &rect)) return false;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return false;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    HGDIOBJ oldObj = SelectObject(hdcMem, hBitmap);

    // 使用 PrintWindow 捕获客户区
    BOOL ok = PrintWindow(m_gameHwnd, hdcMem, PW_CLIENTONLY);
    if (!ok) {
        // 退化方案：BitBlt 可能无法从不同进程窗口复制，但尝试一下
        POINT pt = {0, 0};
        ClientToScreen(m_gameHwnd, &pt);
        BitBlt(hdcMem, 0, 0, width, height, hdcScreen, pt.x, pt.y, SRCCOPY);
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    output.create(height, width, CV_8UC4);
    GetDIBits(hdcMem, hBitmap, 0, height, output.data, &bmi, DIB_RGB_COLORS);

    SelectObject(hdcMem, oldObj);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return true;
}

bool WindowCapture::IdentifyGameBounds(const cv::Mat& screenCapture, cv::Rect& gameRect) {
    // 占位：直接返回整幅图像
    if (screenCapture.empty()) return false;
    gameRect = cv::Rect(0, 0, screenCapture.cols, screenCapture.rows);
    return true;
}

bool WindowCapture::AnalyzeGridLayout(const cv::Mat& gameArea, int& rows, int& cols) {
    // 占位：返回默认 16x16
    rows = 16;
    cols = 16;
    return true;
}
