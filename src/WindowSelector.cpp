#include "WindowSelector.h"
#include "Logger.h"
#include <psapi.h>
#include <string>

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
    const std::wstring keys[] = {L"Minesweeper", L"扫雷", L"Winmine", L"Mine", L"踩地雷"};
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

static std::wstring GetProcessBaseName(DWORD pid) {
    std::wstring name;
    HANDLE h = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (h) {
        wchar_t buf[MAX_PATH]{};
        if (GetModuleBaseNameW(h, NULL, buf, MAX_PATH) > 0) {
            name = buf;
        }
        CloseHandle(h);
    }
    return name;
}

static bool IsExplorerWindow(const WindowCandidate& c) {
    // 资源管理器常见类名 & 进程名
    if (contains_nocase(c.className, L"CabinetWClass") || contains_nocase(c.className, L"ExploreWClass"))
        return true;
    auto base = GetProcessBaseName(c.pid);
    return contains_nocase(base, L"explorer.exe");
}

HWND WindowSelector::AutoPick() {
    auto all = ScanCandidates();
    if (all.empty()) return NULL;

    DWORD selfPid = GetCurrentProcessId();
    // 过滤掉本进程的窗口
    std::vector<WindowCandidate> filtered;
    filtered.reserve(all.size());
    for (const auto& c : all) {
        if (c.pid == selfPid) continue;
        filtered.push_back(c);
    }
    if (filtered.empty()) return NULL;

    // 如果顶层是资源管理器，跳过它
    size_t startIdx = 0;
    if (IsExplorerWindow(filtered[0])) {
        LOGI("Top window is Explorer, will prefer the next visible window");
        startIdx = 1;
    }

    // 优先匹配关键字（覆盖浏览器标题/远程桌面标题包含扫雷词）
    auto ms = FilterMinesweeper(filtered);
    for (size_t i = startIdx; i < filtered.size(); ++i) {
        // 若该窗口在匹配列表中，则直接选它
        const auto& w = filtered[i];
        for (const auto& m : ms) {
            if (m.hwnd == w.hwnd) {
                LOGI("AutoPick matched by title/class at z-index=" + std::to_string(i));
                return GetAncestor(w.hwnd, GA_ROOT);
            }
        }
    }

    // 若无匹配，但首个为 Explorer，返回其后第一个可见窗口作为兜底（用户期望“第二个是游戏”）
    if (startIdx == 1 && filtered.size() >= 2) {
        LOGI("AutoPick fallback to second top window");
        return GetAncestor(filtered[1].hwnd, GA_ROOT);
    }

    LOGI("AutoPick none candidates after filtering");
    return NULL;
}

HWND WindowSelector::PickForeground() {
    HWND h = GetForegroundWindow();
    if (h) return GetAncestor(h, GA_ROOT);
    return NULL;
}
