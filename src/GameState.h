#pragma once
#include <vector>

// -1: 地雷, 0-8: 数字, 9: 未打开, 10: 旗子
struct GameState {
    int rows = 0;
    int cols = 0;
    int mineCount = 0;
    std::vector<std::vector<int>> grid;
    int remainingMines = 0;
    float exploredPercent = 0.0f;
};
