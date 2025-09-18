#ifndef DISPLAY_WINDOW_H
#define DISPLAY_WINDOW_H

#include <windows.h>
#include <string>
#include "GameState.h"

class DisplayWindow {
public:
    DisplayWindow();
    ~DisplayWindow();

    bool Create();
    void Update(const GameState& state);
    void Render();

    HWND GetHandle() const { return m_hwnd; }

private:
    HWND m_hwnd;
    HDC m_memoryDC;
    HBITMAP m_bitmap;
    int m_width, m_height;
    GameState m_state;

    void ResizeBackBuffer(int w, int h);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
};

#endif
