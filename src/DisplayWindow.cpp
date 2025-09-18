#include "DisplayWindow.h"
#include <sstream>

static const wchar_t* kWndClassName = L"MinesweeperAssistantDisplay";

DisplayWindow::DisplayWindow()
    : m_hwnd(NULL), m_memoryDC(NULL), m_bitmap(NULL), m_width(600), m_height(400) {}

DisplayWindow::~DisplayWindow() {
    if (m_bitmap) DeleteObject(m_bitmap);
    if (m_memoryDC) DeleteDC(m_memoryDC);
}

LRESULT CALLBACK DisplayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE: {
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));

        const wchar_t* title = L"Minesweeper Assistant";
        SetBkMode(hdc, TRANSPARENT);
        DrawTextW(hdc, title, -1, &rc, DT_TOP | DT_CENTER | DT_SINGLELINE);

        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

bool DisplayWindow::Create() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DisplayWindow::WndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = kWndClassName;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(WS_EX_APPWINDOW, kWndClassName, L"Minesweeper Assistant",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, m_width, m_height,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!m_hwnd) return false;

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    HDC hdc = GetDC(m_hwnd);
    m_memoryDC = CreateCompatibleDC(hdc);
    ResizeBackBuffer(m_width, m_height);
    ReleaseDC(m_hwnd, hdc);

    return true;
}

void DisplayWindow::ResizeBackBuffer(int w, int h) {
    if (m_bitmap) { DeleteObject(m_bitmap); m_bitmap = NULL; }
    HDC hdc = GetDC(m_hwnd);
    m_bitmap = CreateCompatibleBitmap(hdc, w, h);
    SelectObject(m_memoryDC, m_bitmap);
    ReleaseDC(m_hwnd, hdc);
}

void DisplayWindow::Update(const GameState& state) {
    m_state = state;
    InvalidateRect(m_hwnd, NULL, FALSE);
}

void DisplayWindow::Render() {
    // 当前绘制逻辑在 WM_PAINT 中完成
}

void DisplayWindow::SetTopMost(bool on) {
    m_topMost = on;
    if (!m_hwnd) return;
    SetWindowPos(m_hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void DisplayWindow::ResizeToFit(int contentW, int contentH, float scale) {
    if (!m_hwnd) return;
    int w = int(contentW * scale);
    int h = int(contentH * scale);
    RECT rc{}; rc.left = 0; rc.top = 0; rc.right = w; rc.bottom = h;
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = rc.right - rc.left;
    int winH = rc.bottom - rc.top;
    SetWindowPos(m_hwnd, NULL, 0, 0, winW, winH, SWP_NOMOVE | SWP_NOZORDER);
    m_width = w; m_height = h;
    ResizeBackBuffer(w, h);
}
