#ifndef GAME_ANALYZER_H
#define GAME_ANALYZER_H

#include <windows.h>
#include <opencv2/opencv.hpp>
#include <vector>
#include "GameState.h"

class GameAnalyzer {
public:
    GameAnalyzer();

    bool AnalyzeGameState(const cv::Mat& gameImage, GameState& state);
    std::vector<cv::Point> FindSafeMoves(const GameState& state);
    void PerformClick(HWND hwnd, int x, int y, bool rightClick = false);
    
    // 公用：数字识别（模板匹配优先，失败回退）
    int RecognizeCell(const cv::Mat& cellImage);

private:
    void LoadTemplates();

    std::vector<cv::Mat> m_numberTemplates;
};

#endif
