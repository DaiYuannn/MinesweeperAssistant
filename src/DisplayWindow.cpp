#include "DisplayWindow.h"
#include <sstream>
#include <vector>
#include <algorithm>

static const wchar_t* kWndClassName = L"MinesweeperAssistantDisplay";

DisplayWindow::DisplayWindow()
    : m_hwnd(NULL), m_memoryDC(NULL), m_bitmap(NULL), m_width(260), m_height(480) {}

DisplayWindow::~DisplayWindow() {
    if (m_bitmap) DeleteObject(m_bitmap);
    if (m_memoryDC) DeleteDC(m_memoryDC);
}

void DisplayWindow::AutoAdjustForStatus() {
    if (!m_hwnd) return;
    HDC hdc = GetDC(m_hwnd);
    RECT rcClient{}; GetClientRect(m_hwnd, &rcClient);
    const int padX = 8, padTop = 4;
    RECT statusRc{ rcClient.left + padX, rcClient.top + padTop, rcClient.right - padX, rcClient.bottom };

    // 选择一个适中的字体测量换行高度
    HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"");
    HFONT old = (HFONT)SelectObject(hdc, hFont);
    RECT measure = statusRc;
    std::wstring status = m_statusText.empty() ? L"状态: 未绑定窗口" : m_statusText;
    DrawTextW(hdc, status.c_str(), -1, &measure, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    SelectObject(hdc, old);
    DeleteObject(hFont);
    ReleaseDC(m_hwnd, hdc);

    int statusHeight = (measure.bottom - measure.top) + 8; // 文本高度 + 一点间距

    // 目标高度 = 状态栏 + 最小网格区域(>200)
    int desiredClientH = std::max(260, statusHeight + 220);
    // 限制最大高度，避免窗口过大
    int maxClientH = 600;
    desiredClientH = std::min(desiredClientH, maxClientH);

    // 如果需要增加高度，则调整窗口
    RECT current{}; GetClientRect(m_hwnd, &current);
    int currentH = current.bottom - current.top;
    if (desiredClientH > currentH) {
        RECT adj{ 0,0, m_width, desiredClientH };
        AdjustWindowRect(&adj, WS_OVERLAPPEDWINDOW, FALSE);
        int winW = adj.right - adj.left;
        int winH = adj.bottom - adj.top;
        SetWindowPos(m_hwnd, NULL, 0, 0, winW, winH, SWP_NOMOVE | SWP_NOZORDER);
        m_height = desiredClientH;
        ResizeBackBuffer(m_width, m_height);
    }
}

LRESULT CALLBACK DisplayWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        return 1; // 自行擦除，避免闪烁
    }
    case WM_SIZE: {
        // 调整后备缓冲大小匹配新的客户区尺寸
        DisplayWindow* self = reinterpret_cast<DisplayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        if (self && self->m_hwnd) {
            RECT rc; GetClientRect(hwnd, &rc);
            int w = rc.right - rc.left;
            int h = rc.bottom - rc.top;
            if (w>0 && h>0) {
                self->m_width = w; self->m_height = h;
                self->ResizeBackBuffer(w, h);
            }
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        // 使用内存 DC 进行绘制
        DisplayWindow* self = reinterpret_cast<DisplayWindow*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
        HDC ddc = (self && self->m_memoryDC) ? self->m_memoryDC : hdc;
        FillRect(ddc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
        // 取 this 指针（上方已取）

    SetBkMode(ddc, TRANSPARENT);
    // 状态栏文本（优先换行，次选缩小字体）
        const int padX = 8, padTop = 4;
        RECT statusRc{ rc.left + padX, rc.top + padTop, rc.right - padX, rc.bottom };
        std::wstring status = (self && !self->m_statusText.empty()) ? self->m_statusText : L"状态: 未绑定窗口";

        // 基准字体高度从 18 逐步降低到 11
    HFONT hOldFont = (HFONT)SelectObject(ddc, GetStockObject(DEFAULT_GUI_FONT));
        HFONT hFontFit = NULL;
        SIZE textSize{};
        int chosenHeight = 14; // 默认
        const int availWidth = statusRc.right - statusRc.left;
        for (int h = 18; h >= 11; --h) {
            HFONT hTry = CreateFontW(-h, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"");
            HFONT hPrev = (HFONT)SelectObject(ddc, hTry);
            // 使用换行布局测量高度
            RECT measureRc = statusRc;
            DrawTextW(ddc, status.c_str(), -1, &measureRc, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
            textSize.cx = (measureRc.right - measureRc.left);
            textSize.cy = (measureRc.bottom - measureRc.top);
            SelectObject(ddc, hPrev);
            if (textSize.cx <= availWidth) { hFontFit = hTry; chosenHeight = h; break; }
            DeleteObject(hTry);
        }
        if (!hFontFit) {
            // 没有找到合适宽度的字体，则采用最小高度并依赖省略号
            hFontFit = CreateFontW(-11, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"");
        }
    HFONT hPrevFont = (HFONT)SelectObject(ddc, hFontFit);
        // 计算文本高度
    TEXTMETRIC tm{}; GetTextMetricsW(ddc, &tm);
    // 用多行绘制，计算实际高度
    RECT drawRc{ statusRc.left, statusRc.top, statusRc.right, statusRc.bottom };
    DrawTextW(ddc, status.c_str(), -1, &drawRc, DT_LEFT | DT_WORDBREAK | DT_CALCRECT);
    int statusHeight = (drawRc.bottom - drawRc.top);
    // 实际绘制
    DrawTextW(ddc, status.c_str(), -1, &drawRc, DT_LEFT | DT_WORDBREAK);
        // 恢复字体并清理
    SelectObject(ddc, hPrevFont);
        DeleteObject(hFontFit);

        // 简易网格渲染
        if (self && self->m_state.rows > 0 && self->m_state.cols > 0) {
            // 根据状态栏高度动态让出空间
            int gridTop = padTop + (int)std::max(22, (int)statusHeight + 6);
            int w = (rc.right - rc.left) - 16; // padding
            int h = (rc.bottom - rc.top) - gridTop - 16;
            if (w < 10 || h < 10) { EndPaint(hwnd, &ps); return 0; }
            // 计算单元格像素大小：优先适配可用空间，再限制最大像素大小以避免在小盘面时过大
            int cols = self->m_state.cols;
            int rows = self->m_state.rows;
            int fitSize = std::min(w / std::max(1, cols), h / std::max(1, rows));
            int maxCell = 24; // 最大单元像素，避免 9x9 时过大
            int cell = std::max(1, std::min(fitSize, maxCell));
            int gridW = cell * cols;
            int gridH = cell * rows;
            // 居中显示网格
            int startX = rc.left + 8 + (w - gridW) / 2;
            int startY = rc.top + gridTop + (h - gridH) / 2;

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
            HGDIOBJ oldPen = SelectObject(ddc, gridPen);
            // 网格线
            for (int r = 0; r <= self->m_state.rows; ++r) {
                int y = startY + r * cell;
                MoveToEx(ddc, startX, y, NULL);
                LineTo(ddc, startX + cell * self->m_state.cols, y);
            }
            for (int c = 0; c <= self->m_state.cols; ++c) {
                int x = startX + c * cell;
                MoveToEx(ddc, x, startY, NULL);
                LineTo(ddc, x, startY + cell * self->m_state.rows);
            }

            // 单元格内容
            for (int r = 0; r < self->m_state.rows; ++r) {
                for (int c = 0; c < self->m_state.cols; ++c) {
                    int v = 9;
                    if (r < (int)self->m_state.grid.size() && c < (int)self->m_state.grid[r].size())
                        v = self->m_state.grid[r][c];
                    RECT cellRc{ startX + c*cell, startY + r*cell, startX + (c+1)*cell, startY + (r+1)*cell };
                    // 背景色：未知淡灰，旗子淡黄，雷淡红
                    COLORREF bg = RGB(255,255,255);
                    if (v == 9) bg = RGB(245,245,245);
                    else if (v == 10) bg = RGB(255,250,205);
                    else if (v == -1) bg = RGB(255,228,225);
                    HBRUSH b = CreateSolidBrush(bg);
                    FillRect(ddc, &cellRc, b); DeleteObject(b);

                    if (v >= 1 && v <= 8) {
                        std::wstring t = std::to_wstring(v);
                        SetTextColor(ddc, colorNum(v));
                        DrawTextW(ddc, t.c_str(), -1, &cellRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                    }
                }
            }

            // 高亮建议
            auto drawHighlight = [&](const std::vector<cv::Point>& pts, COLORREF col) {
                HPEN pen = CreatePen(PS_SOLID, 2, col);
                HGDIOBJ old = SelectObject(ddc, pen);
                HGDIOBJ oldBrush = SelectObject(ddc, GetStockObject(HOLLOW_BRUSH));
                for (auto& p : pts) {
                    if (p.y < 0 || p.y >= self->m_state.rows || p.x < 0 || p.x >= self->m_state.cols) continue;
                    RECT cellHi{ startX + p.x*cell, startY + p.y*cell, startX + (p.x+1)*cell, startY + (p.y+1)*cell };
                    Rectangle(ddc, cellHi.left, cellHi.top, cellHi.right, cellHi.bottom);
                }
                SelectObject(ddc, oldBrush);
                SelectObject(ddc, old);
                DeleteObject(pen);
            };
            drawHighlight(self->m_state.safeCells, RGB(0,200,0));
            drawHighlight(self->m_state.mineCells, RGB(200,0,0));

            SelectObject(ddc, oldPen);
            DeleteObject(gridPen);
        }

        // 将内存缓冲拷贝到前台
        if ((self && self->m_memoryDC) && (ddc != hdc)) {
            BitBlt(hdc, 0, 0, rc.right-rc.left, rc.bottom-rc.top, ddc, 0, 0, SRCCOPY);
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
