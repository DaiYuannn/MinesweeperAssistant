#include "WindowCapture.h"
#include <iostream>

using namespace cv;

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
    if (screenCapture.empty()) return false;

    // 预处理：转灰度、平滑、边缘
    Mat gray; cvtColor(screenCapture, gray, COLOR_BGRA2GRAY);
    Mat blur; GaussianBlur(gray, blur, Size(3,3), 0);
    Mat edges; Canny(blur, edges, 50, 150);
    Mat dil; morphologyEx(edges, dil, MORPH_CLOSE, getStructuringElement(MORPH_RECT, Size(3,3)));

    // 查找外部轮廓
    std::vector<std::vector<Point>> contours; std::vector<Vec4i> hierarchy;
    findContours(dil, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    const int W = screenCapture.cols, H = screenCapture.rows;
    const double imgArea = double(W) * double(H);
    double bestScore = 0.0; Rect bestRect;
    for (auto& c : contours) {
        if (c.size() < 4) continue;
        double per = arcLength(c, true);
        std::vector<Point> approx; approxPolyDP(c, approx, 0.02 * per, true);
        if (approx.size() != 4) continue;
        if (!isContourConvex(approx)) continue;
        Rect r = boundingRect(approx);
        // 过滤过小/过大/贴边的矩形
        if (r.width < 80 || r.height < 80) continue;
        double area = double(r.area());
        if (area > imgArea * 0.90) continue; // 排除几乎占满整个客户区的矩形
        // 距离边缘至少几像素，避免整幅图
        if (r.x < 5 || r.y < 5 || r.br().x > W - 5 || r.br().y > H - 5) continue;
        double ar = double(r.width) / double(r.height);
        if (ar < 0.5 || ar > 2.0) continue;

        // 评分：面积越大越好（但不占满），更接近正方形略加分
        double score = area * (1.0 - std::abs(ar - 1.0) * 0.1);
        if (score > bestScore) { bestScore = score; bestRect = r; }
    }

    if (bestScore <= 0.0) return false;
    gameRect = bestRect;
    return true;
}

bool WindowCapture::AnalyzeGridLayout(const cv::Mat& gameArea, int& rows, int& cols) {
    // 占位：返回默认 16x16
    rows = 16;
    cols = 16;
    return true;
}
