#pragma once
#include <windows.h>
#include <string>
#include <vector>

struct WindowCandidate {
    HWND hwnd{};
    std::wstring title;
    std::wstring className;
    DWORD pid{};
};

class WindowSelector {
public:
    // 扫描可见顶层窗口，返回候选列表
    static std::vector<WindowCandidate> ScanCandidates();

    // 根据标题/类名关键字进行启发式筛选
    static std::vector<WindowCandidate> FilterMinesweeper(const std::vector<WindowCandidate>& in);

    // 选择：若唯一候选，直接返回；否则返回 NULL 让上层决定（列表 UI）
    static HWND AutoPick();

    // 聚焦即选：读取当前前台窗口（用于热键触发）
    static HWND PickForeground();
};
