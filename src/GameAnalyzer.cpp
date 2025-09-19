#include "GameAnalyzer.h"
#include <iostream>
#include <atomic>
#include <filesystem>

using namespace cv;

GameAnalyzer::GameAnalyzer() {
    LoadTemplates();
}

static inline bool colorNear(const Vec3b& bgr, const Vec3b& target, int tol) {
    return std::abs(bgr[0]-target[0])<=tol && std::abs(bgr[1]-target[1])<=tol && std::abs(bgr[2]-target[2])<=tol;
}

static int recognizeSimple(const Mat& cell) {
    // 去掉一圈边界，避免格线干扰
    int inset = std::max(1, std::min(cell.cols, cell.rows) / 16);
    Rect inner(inset, inset, std::max(1, cell.cols - 2*inset), std::max(1, cell.rows - 2*inset));
    Mat roi = cell(inner);

    Mat bgr; if (roi.channels()==4) cvtColor(roi, bgr, COLOR_BGRA2BGR); else bgr = roi;
    Mat gray; cvtColor(bgr, gray, COLOR_BGR2GRAY);

    // 判空：低方差 + 非覆盖蓝灰调（非常粗略）
    Scalar mean, stddev; meanStdDev(gray, mean, stddev);
    double var = stddev[0]*stddev[0];
    if (var < 15.0) {
        // 再简单排除“覆盖色”——通常偏中性灰且亮度适中
        double m = mean[0];
        if (!(m>120 && m<200)) return 0;
    }

    // 粗略统计显著色
    int cntBlue=0, cntGreen=0, cntRed=0, total=0;
    for (int y=0;y<bgr.rows;++y){
        const Vec3b* row = bgr.ptr<Vec3b>(y);
        for (int x=0;x<bgr.cols;++x){
            Vec3b p = row[x];
            // 排除很灰的像素
            int maxc = std::max({p[0],p[1],p[2]});
            int minc = std::min({p[0],p[1],p[2]});
            if (maxc - minc < 40) continue;
            // 颜色主导
            if (p[0] > p[1]+20 && p[0] > p[2]+20) cntBlue++;
            else if (p[1] > p[0]+20 && p[1] > p[2]+20) cntGreen++;
            else if (p[2] > p[0]+20 && p[2] > p[1]+20) cntRed++;
            total++;
        }
    }
    if (total>0){
        // 阈值：主色占有率较高才认为识别到了数字
        if (cntBlue > total*0.06) return 1;  // 蓝色
        if (cntGreen > total*0.06) return 2; // 绿色
        if (cntRed > total*0.06) return 3;   // 红色
    }
    return 9; // 未知
}

bool GameAnalyzer::AnalyzeGameState(const cv::Mat& gameImage, GameState& state) {
    if (gameImage.empty()) return false;

    if (state.rows <= 0 || state.cols <= 0) {
        state.rows = 16;
        state.cols = 16;
        state.mineCount = 40;
    }
    state.grid.assign(state.rows, std::vector<int>(state.cols, 9));
    state.remainingMines = state.mineCount;
    state.exploredPercent = 0.0f;
    state.safeCells.clear();
    state.mineCells.clear();

    // 按均匀网格切分并识别
    int W = gameImage.cols, H = gameImage.rows;
    int cellW = std::max(1, W / state.cols);
    int cellH = std::max(1, H / state.rows);
    int known = 0;
    for (int r=0;r<state.rows;++r){
        for (int c=0;c<state.cols;++c){
            int x = c*cellW;
            int y = r*cellH;
            Rect rc(x, y, (c==state.cols-1? W-x : cellW), (r==state.rows-1? H-y : cellH));
            rc &= Rect(0,0,W,H);
            if (rc.width<=0 || rc.height<=0) { state.grid[r][c]=9; continue; }
            int v = recognizeSimple(gameImage(rc));
            state.grid[r][c] = v;
            if (v!=9) known++;
        }
    }
    state.exploredPercent = 100.f * known / float(state.rows*state.cols);
    return true;
}

std::vector<cv::Point> GameAnalyzer::FindSafeMoves(const GameState& state) {
    // 占位：寻找所有已知为 0 的周围未知格，作为安全推荐
    std::vector<cv::Point> out;
    auto inside = [&](int r, int c){ return r>=0 && r<state.rows && c>=0 && c<state.cols; };
    for (int r=0;r<state.rows;++r){
        for (int c=0;c<state.cols;++c){
            if (state.grid[r][c] == 0){
                for (int dr=-1; dr<=1; ++dr){
                    for (int dc=-1; dc<=1; ++dc){
                        if (dr==0 && dc==0) continue;
                        int nr=r+dr, nc=c+dc;
                        if (inside(nr,nc) && state.grid[nr][nc]==9) out.emplace_back(nc,nr);
                    }
                }
            }
        }
    }
    return out;
}

extern std::atomic<bool> g_enableMouseMove;
extern std::atomic<bool> g_enableAutoClick;

void GameAnalyzer::PerformClick(HWND hwnd, int x, int y, bool rightClick) {
    if (!hwnd) return;
    if (!g_enableMouseMove.load()) return; // 用户关闭时不控制鼠标
    // 将客户区坐标转换为屏幕坐标并模拟点击
    POINT pt{ x, y };
    ClientToScreen(hwnd, &pt);

    INPUT inputMove{};
    inputMove.type = INPUT_MOUSE;
    inputMove.mi.dx = LONG(pt.x * (65535.0 / GetSystemMetrics(SM_CXSCREEN)));
    inputMove.mi.dy = LONG(pt.y * (65535.0 / GetSystemMetrics(SM_CYSCREEN)));
    inputMove.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;

    SendInput(1, &inputMove, sizeof(INPUT));
    if (!g_enableAutoClick.load()) return; // 未开启自动点击则只移动

    INPUT clickDown{}; clickDown.type = INPUT_MOUSE;
    INPUT clickUp{};   clickUp.type   = INPUT_MOUSE;
    if (rightClick) {
        clickDown.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
        clickUp.mi.dwFlags   = MOUSEEVENTF_RIGHTUP;
    } else {
        clickDown.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        clickUp.mi.dwFlags   = MOUSEEVENTF_LEFTUP;
    }
    SendInput(1, &clickDown, sizeof(INPUT));
    SendInput(1, &clickUp, sizeof(INPUT));
}

int GameAnalyzer::RecognizeCell(const cv::Mat& cellImage) {
    // 先尝试模板匹配（若已加载）
    int bestDigit = -1; double bestScore = -1.0;
    if (!m_numberTemplates.empty()) {
        // 内圈裁剪，减少格线影响
        int inset = std::max(1, std::min(cellImage.cols, cellImage.rows) / 12);
        Rect inner(inset, inset, std::max(1, cellImage.cols - 2*inset), std::max(1, cellImage.rows - 2*inset));
        Mat roi = cellImage(inner);
        Mat gray; if (roi.channels()==4) cvtColor(roi, gray, COLOR_BGRA2GRAY); else if (roi.channels()==3) cvtColor(roi, gray, COLOR_BGR2GRAY); else gray = roi;
        // 归一化尺寸到模板大小（以“2.png”的尺寸为参考，如果存在）
        Size refSize;
        if ((int)m_numberTemplates.size() >= 3 && !m_numberTemplates[2].empty()) refSize = m_numberTemplates[2].size();
        else {
            for (size_t i=0;i<m_numberTemplates.size();++i) if (!m_numberTemplates[i].empty()){ refSize = m_numberTemplates[i].size(); break; }
        }
        if (refSize.width>0 && refSize.height>0) {
            Mat g; resize(gray, g, refSize, 0, 0, INTER_AREA);
            for (int d=1; d<=8; ++d) {
                if ((int)m_numberTemplates.size()<=d || m_numberTemplates[d].empty()) continue;
                Mat res; matchTemplate(g, m_numberTemplates[d], res, TM_CCOEFF_NORMED);
                double minV, maxV; Point minL, maxL; minMaxLoc(res, &minV, &maxV, &minL, &maxL);
                if (maxV > bestScore) { bestScore = maxV; bestDigit = d; }
            }
        }
        // 阈值判定
        if (bestScore >= 0.60) return bestDigit;
    }
    // 回退简单颜色/方差法
    return recognizeSimple(cellImage);
}

void GameAnalyzer::LoadTemplates() {
    using std::filesystem::exists;
    m_numberTemplates.clear();
    m_numberTemplates.resize(9); // 0..8，其中 1..8 有效
    std::string base = "resources/templates";
    for (int d=1; d<=8; ++d) {
        std::string p1 = base + "/" + std::to_string(d) + ".png";
        std::string p2 = base + "/" + std::to_string(d) + ".bmp";
        Mat img;
        if (exists(p1)) img = imread(p1, IMREAD_GRAYSCALE);
        else if (exists(p2)) img = imread(p2, IMREAD_GRAYSCALE);
        if (!img.empty()) {
            // 轻度阈值以增强对比
            Mat th; threshold(img, th, 0, 255, THRESH_OTSU);
            m_numberTemplates[d] = th;
        }
    }
}
