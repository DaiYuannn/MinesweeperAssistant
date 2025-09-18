#include "WindowSelector.h"
#include "Logger.h"
#include <psapi.h>

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto list = reinterpret_cast<std::vector<WindowCandidate>*>(lParam);
    if (!IsWindowVisible(hwnd)) return TRUE;
    // 跳过不可见/大小为0
    RECT rc{}; GetWindowRect(hwnd, &rc);
    if (rc.right - rc.left <= 0 || rc.bottom - rc.top <= 0) return TRUE;

    wchar_t title[512]{}; GetWindowTextW(hwnd, title, 512);
    if (wcslen(title) == 0) return TRUE;

    wchar_t cls[256]{}; GetClassNameW(hwnd, cls, 256);
    DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);

    WindowCandidate c{};
    c.hwnd = hwnd;
    c.title = title;
    c.className = cls;
    c.pid = pid;
    list->push_back(std::move(c));
    return TRUE;
}

std::vector<WindowCandidate> WindowSelector::ScanCandidates() {
    std::vector<WindowCandidate> out;
    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&out));
    LOGI("ScanCandidates count=" + std::to_string(out.size()));
    return out;
}

static bool contains_nocase(const std::wstring& s, const std::wstring& key) {
    if (key.empty()) return true;
    std::wstring a = s, b = key;
    for (auto& ch : a) ch = towlower(ch);
    for (auto& ch : b) ch = towlower(ch);
    return a.find(b) != std::wstring::npos;
}

std::vector<WindowCandidate> WindowSelector::FilterMinesweeper(const std::vector<WindowCandidate>& in) {
    std::vector<WindowCandidate> out;
    // 常见关键字（可后续配置化）
    const std::wstring keys[] = {L"Minesweeper", L"扫雷", L"Winmine", L"Mine"};
    for (const auto& c : in) {
        for (const auto& k : keys) {
            if (contains_nocase(c.title, k) || contains_nocase(c.className, k)) {
                out.push_back(c);
                break;
            }
        }
    }
    LOGI("FilterMinesweeper matched=" + std::to_string(out.size()));
    return out;
}

HWND WindowSelector::AutoPick() {
    auto all = ScanCandidates();
    auto ms = FilterMinesweeper(all);
    if (ms.size() == 1) {
        LOGI("AutoPick hit one candidate");
        return GetAncestor(ms[0].hwnd, GA_ROOT);
    }
    LOGI("AutoPick none or multi candidates");
    return NULL;
}

HWND WindowSelector::PickForeground() {
    HWND h = GetForegroundWindow();
    if (h) return GetAncestor(h, GA_ROOT);
    return NULL;
}
