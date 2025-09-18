#include "DisplayWindow.h"
#include <sstream>
#include <vector>

static const wchar_t* kWndClassName = L"MinesweeperAssistantDisplay";

DisplayWindow::DisplayWindow()
    : m_hwnd(NULL), m_memoryDC(NULL), m_bitmap(NULL), m_width(260), m_height(480) {}

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

        // 简易网格渲染
        DisplayWindow* self = reinterpret_cast<DisplayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (self && self->m_state.rows > 0 && self->m_state.cols > 0) {
            int gridTop = 30; // 预留标题高度
            int w = (rc.right - rc.left) - 16; // padding
            int h = (rc.bottom - rc.top) - gridTop - 16;
            if (w < 10 || h < 10) { EndPaint(hwnd, &ps); return 0; }
            int cellW = w / self->m_state.cols;
            int cellH = h / self->m_state.rows;
            int startX = rc.left + 8;
            int startY = rc.top + gridTop;

            auto colorNum = [&](int n) -> COLORREF {
                switch (n) {
                case 1: return RGB(0,0,255);
                case 2: return RGB(0,128,0);
                case 3: return RGB(255,0,0);
                case 4: return RGB(0,0,128);
                case 5: return RGB(128,0,0);
                case 6: return RGB(0,128,128);
                case 7: return RGB(0,0,0);
                case 8: return RGB(128,128,128);
                default: return RGB(0,0,0);
                }
            };

            HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(200,200,200));
            HGDIOBJ oldPen = SelectObject(hdc, gridPen);
            // 网格线
            for (int r = 0; r <= self->m_state.rows; ++r) {
                int y = startY + r * cellH;
                MoveToEx(hdc, startX, y, NULL);
                LineTo(hdc, startX + cellW * self->m_state.cols, y);
            }
            for (int c = 0; c <= self->m_state.cols; ++c) {
                int x = startX + c * cellW;
                MoveToEx(hdc, x, startY, NULL);
                LineTo(hdc, x, startY + cellH * self->m_state.rows);
            }

            // 单元格内容
            for (int r = 0; r < self->m_state.rows; ++r) {
                for (int c = 0; c < self->m_state.cols; ++c) {
                    int v = 9;
                    if (r < (int)self->m_state.grid.size() && c < (int)self->m_state.grid[r].size())
                        v = self->m_state.grid[r][c];
                    RECT cell{ startX + c*cellW, startY + r*cellH, startX + (c+1)*cellW, startY + (r+1)*cellH };
                    // 背景色：未知淡灰，旗子淡黄，雷淡红
                    COLORREF bg = RGB(255,255,255);
                    if (v == 9) bg = RGB(245,245,245);
                    else if (v == 10) bg = RGB(255,250,205);
                    else if (v == -1) bg = RGB(255,228,225);
                    HBRUSH b = CreateSolidBrush(bg);
                    FillRect(hdc, &cell, b); DeleteObject(b);

                    if (v >= 1 && v <= 8) {
                        std::wstring t = std::to_wstring(v);
                        SetTextColor(hdc, colorNum(v));
                        DrawTextW(hdc, t.c_str(), -1, &cell, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }
            }

            // 高亮建议
            auto drawHighlight = [&](const std::vector<cv::Point>& pts, COLORREF col) {
                HPEN pen = CreatePen(PS_SOLID, 2, col);
                HGDIOBJ old = SelectObject(hdc, pen);
                HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
                for (auto& p : pts) {
                    if (p.y < 0 || p.y >= self->m_state.rows || p.x < 0 || p.x >= self->m_state.cols) continue;
                    RECT cell{ startX + p.x*cellW, startY + p.y*cellH, startX + (p.x+1)*cellW, startY + (p.y+1)*cellH };
                    Rectangle(hdc, cell.left, cell.top, cell.right, cell.bottom);
                }
                SelectObject(hdc, oldBrush);
                SelectObject(hdc, old);
                DeleteObject(pen);
            };
            drawHighlight(self->m_state.safeCells, RGB(0,200,0));
            drawHighlight(self->m_state.mineCells, RGB(200,0,0));

            SelectObject(hdc, oldPen);
            DeleteObject(gridPen);
        }

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

    // 绑定 this 以便在 WndProc 中使用
    SetWindowLongPtr(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

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

void DisplayWindow::SnapNear(const RECT& target) {
    if (!m_hwnd) return;

    // 计算当前窗口外框尺寸
    RECT self{}; self.left = 0; self.top = 0; self.right = m_width; self.bottom = m_height;
    AdjustWindowRect(&self, WS_OVERLAPPEDWINDOW, FALSE);
    int winW = self.right - self.left;
    int winH = self.bottom - self.top;

    // 首选靠右，其次靠左、下、上
    int pad = 8;
    POINT posCandidates[4] = {
        { target.right + pad, target.top },                         // 右侧
        { target.left - pad - winW, target.top },                   // 左侧
        { target.left, target.bottom + pad },                       // 下方
        { target.left, target.top - pad - winH }                    // 上方
    };

    // 屏幕工作区（主屏）
    RECT wa{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);

    auto clamp = [&](POINT p) {
        if (p.x < wa.left) p.x = wa.left;
        if (p.y < wa.top) p.y = wa.top;
        if (p.x + winW > wa.right) p.x = wa.right - winW;
        if (p.y + winH > wa.bottom) p.y = wa.bottom - winH;
        return p;
    };

    // 选择首个基本不遮挡目标上边的方案
    POINT best = clamp(posCandidates[0]);
    SetWindowPos(m_hwnd, NULL, best.x, best.y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
}
