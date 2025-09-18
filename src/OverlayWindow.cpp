#include "OverlayWindow.h"
#include <dwmapi.h>
#include <algorithm>
#pragma comment(lib, "Dwmapi.lib")

static OverlayWindow* g_overlay = nullptr;

OverlayWindow::OverlayWindow() {}
OverlayWindow::~OverlayWindow() { destroy(); }

LRESULT CALLBACK OverlayWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC hdc = BeginPaint(h, &ps);
        // 半透明遮罩 + 选框边框
        RECT rc; GetClientRect(h, &rc);
        HBRUSH brush = CreateSolidBrush(RGB(0,0,0));
        FillRect(hdc, &rc, brush); DeleteObject(brush);
        if (g_overlay && g_overlay->m_selecting) {
            RECT sel{ g_overlay->m_ptStart.x - g_overlay->m_virtual.left,
                      g_overlay->m_ptStart.y - g_overlay->m_virtual.top,
                      g_overlay->m_ptCur.x   - g_overlay->m_virtual.left,
                      g_overlay->m_ptCur.y   - g_overlay->m_virtual.top };
            // 规范化
            if (sel.right < sel.left) std::swap(sel.left, sel.right);
            if (sel.bottom < sel.top) std::swap(sel.top, sel.bottom);
            HPEN pen = CreatePen(PS_SOLID, 2, RGB(0,255,0));
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            Rectangle(hdc, sel.left, sel.top, sel.right, sel.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
        EndPaint(h, &ps);
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { ShowWindow(h, SW_HIDE); g_overlay->m_done = true; return 0; }
        break;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProc(h, m, w, l);
}

LRESULT CALLBACK OverlayWindow::LLMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0 && g_overlay) {
        auto ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
        if (wParam == WM_LBUTTONDOWN) {
            g_overlay->m_ptStart = ms->pt;
            g_overlay->m_ptCur = ms->pt;
            g_overlay->m_selecting = true;
            InvalidateRect(g_overlay->m_hwnd, NULL, FALSE);
        } else if (wParam == WM_MOUSEMOVE) {
            if (g_overlay->m_selecting) {
                g_overlay->m_ptCur = ms->pt;
                InvalidateRect(g_overlay->m_hwnd, NULL, FALSE);
            }
        } else if (wParam == WM_LBUTTONUP) {
            g_overlay->m_selecting = false;
            // 规范化并判断有效面积
            LONG left   = std::min(g_overlay->m_ptStart.x, g_overlay->m_ptCur.x);
            LONG right  = std::max(g_overlay->m_ptStart.x, g_overlay->m_ptCur.x);
            LONG top    = std::min(g_overlay->m_ptStart.y, g_overlay->m_ptCur.y);
            LONG bottom = std::max(g_overlay->m_ptStart.y, g_overlay->m_ptCur.y);
            if ((right - left) >= 3 && (bottom - top) >= 3) {
                POINT cpt{ (left + right)/2, (top + bottom)/2 };
                HWND hTarget = WindowFromPoint(cpt);
                if (hTarget) {
                    HWND root = GetAncestor(hTarget, GA_ROOT);
                    if (root != g_overlay->m_hwnd) {
                        g_overlay->m_result = root;
                    }
                }
            }
            ShowWindow(g_overlay->m_hwnd, SW_HIDE);
            g_overlay->m_done = true;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void OverlayWindow::destroy() {
    if (m_mouseHook) { UnhookWindowsHookEx(m_mouseHook); m_mouseHook = NULL; }
    if (m_hwnd) { DestroyWindow(m_hwnd); m_hwnd = NULL; }
}

HWND OverlayWindow::SelectBlocking() {
    g_overlay = this;

    // 计算虚拟屏幕
    m_virtual.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    m_virtual.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    m_virtual.right = m_virtual.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    m_virtual.bottom = m_virtual.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"MSA_Overlay"; wc.hCursor = LoadCursor(NULL, IDC_CROSS);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        wc.lpszClassName, L"", WS_POPUP,
        m_virtual.left, m_virtual.top,
        m_virtual.right - m_virtual.left, m_virtual.bottom - m_virtual.top,
        NULL, NULL, GetModuleHandle(NULL), NULL);

    if (!m_hwnd) return NULL;

    // 半透明遮罩
    SetLayeredWindowAttributes(m_hwnd, 0, (BYTE)60, LWA_ALPHA);
    ShowWindow(m_hwnd, SW_SHOW);

    // 低级鼠标钩子
    m_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LLMouseProc, NULL, 0);

    // 模态循环（PeekMessage，避免无法退出）
    m_done = false;
    MSG msg;
    while (!m_done) {
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        Sleep(10);
    }

    destroy();
    HWND ret = m_result;
    g_overlay = nullptr;
    return ret;
}
