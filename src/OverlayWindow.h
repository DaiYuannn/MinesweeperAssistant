#pragma once
#include <windows.h>
#include <atomic>

class OverlayWindow {
public:
    OverlayWindow();
    ~OverlayWindow();

    // 阻塞选择：创建全屏覆盖层，拖拽框选后返回选中窗口（顶级祖先）
    HWND SelectBlocking();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK LLMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    void drawSelection();
    void destroy();

    HWND m_hwnd{};
    HHOOK m_mouseHook{};
    POINT m_ptStart{};
    POINT m_ptCur{};
    std::atomic<bool> m_selecting{false};
    std::atomic<bool> m_done{false};
    HWND m_result{};
    RECT m_virtual{}; // 虚拟屏幕矩形
};
