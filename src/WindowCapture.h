#ifndef WINDOW_CAPTURE_H
#define WINDOW_CAPTURE_H

#include <windows.h>
#include <opencv2/opencv.hpp>

class WindowCapture {
public:
    WindowCapture();
    ~WindowCapture();

    HWND SelectGameWindow();
    bool CaptureGameArea(cv::Mat& output);
    bool IdentifyGameBounds(const cv::Mat& screenCapture, cv::Rect& gameRect);
    bool AnalyzeGridLayout(const cv::Mat& gameArea, int& rows, int& cols);

    void SetGameWindow(HWND hwnd) { m_gameHwnd = hwnd; }
    HWND GetGameWindow() const { return m_gameHwnd; }

private:
    HWND m_gameHwnd;
    cv::Rect m_gameRect;
    int m_rows, m_cols;
};

#endif
