#include "GameAnalyzer.h"
#include <iostream>

GameAnalyzer::GameAnalyzer() {
    LoadTemplates();
}

bool GameAnalyzer::AnalyzeGameState(const cv::Mat& gameImage, GameState& state) {
    if (gameImage.empty()) return false;

    // 占位：初始化网格为未知
    if (state.rows <= 0 || state.cols <= 0) {
        state.rows = 16;
        state.cols = 16;
        state.mineCount = 40;
    }
    state.grid.assign(state.rows, std::vector<int>(state.cols, 9));
    state.remainingMines = state.mineCount;
    state.exploredPercent = 0.0f;

    return true;
}

std::vector<cv::Point> GameAnalyzer::FindSafeMoves(const GameState& state) {
    // 占位：当前不做自动点击，返回空
    return {};
}

void GameAnalyzer::PerformClick(HWND hwnd, int x, int y, bool rightClick) {
    if (!hwnd) return;
    // 将客户区坐标转换为屏幕坐标并模拟点击
    POINT pt{ x, y };
    ClientToScreen(hwnd, &pt);

    INPUT inputMove{};
    inputMove.type = INPUT_MOUSE;
    inputMove.mi.dx = LONG(pt.x * (65535.0 / GetSystemMetrics(SM_CXSCREEN)));
    inputMove.mi.dy = LONG(pt.y * (65535.0 / GetSystemMetrics(SM_CYSCREEN)));
    inputMove.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

    // 仅移动鼠标，避免误点击
    SendInput(1, &inputMove, sizeof(INPUT));
}

int GameAnalyzer::RecognizeCell(const cv::Mat& /*cellImage*/) {
    // 占位：返回未知
    return 9;
}

void GameAnalyzer::LoadTemplates() {
    // TODO: 从 resources/templates 加载模板
    m_numberTemplates.clear();
}
