#include "WindowCapture.h"
#include <iostream>

using namespace cv;

WindowCapture::WindowCapture() : m_gameHwnd(NULL), m_rows(0), m_cols(0) {
    m_hudTopRatioPercent.store(35);
}
WindowCapture::~WindowCapture() {}

HWND WindowCapture::SelectGameWindow() {
    // 简化选择逻辑：默认选择当前前台窗口
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) {
        std::cerr << "无法获取前台窗口" << std::endl;
        return NULL;
    }
    m_gameHwnd = hwnd;
    std::cout << "已选择窗口: " << m_gameHwnd << std::endl;
    return m_gameHwnd;
}

bool WindowCapture::CaptureGameArea(cv::Mat& output) {
    if (!m_gameHwnd) return false;

    RECT rect;
    if (!GetClientRect(m_gameHwnd, &rect)) return false;

    int width = rect.right - rect.left;
    int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return false;

    HDC hdcScreen = GetDC(NULL);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBitmap = CreateCompatibleBitmap(hdcScreen, width, height);
    HGDIOBJ oldObj = SelectObject(hdcMem, hBitmap);

    // 使用 PrintWindow 捕获客户区（在浏览器渲染下可能返回“看似成功但为空白/黑”）
    BOOL ok = PrintWindow(m_gameHwnd, hdcMem, PW_CLIENTONLY);
    // 校验内容是否有效（统计像素方差/非纯色像素数）
    auto validateBitmap = [&](HDC dc)->bool{
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        cv::Mat tmp(height, width, CV_8UC4);
        GetDIBits(dc, (HBITMAP)hBitmap, 0, height, tmp.data, &bmi, DIB_RGB_COLORS);
        // 粗略判断：非全黑/全白/单色比例要有一定阈值
        cv::Mat bgr; cv::cvtColor(tmp, bgr, cv::COLOR_BGRA2BGR);
        cv::Scalar mean, stddev; cv::meanStdDev(bgr, mean, stddev);
        double sigma = (stddev[0]+stddev[1]+stddev[2])/3.0;
        return sigma > 2.0; // 非常小的方差认为无效
    };
    if (!ok || !validateBitmap(hdcMem)) {
        // 退化方案：BitBlt（大多浏览器窗口只要不启用保护即可拷贝）
        POINT pt = {0, 0};
        ClientToScreen(m_gameHwnd, &pt);
        BitBlt(hdcMem, 0, 0, width, height, hdcScreen, pt.x, pt.y, SRCCOPY);
        m_lastCaptureMethod = L"BitBlt";
    } else {
        m_lastCaptureMethod = L"PW";
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    output.create(height, width, CV_8UC4);
    GetDIBits(hdcMem, hBitmap, 0, height, output.data, &bmi, DIB_RGB_COLORS);

    SelectObject(hdcMem, oldObj);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);

    return true;
}

bool WindowCapture::IdentifyGameBounds(const cv::Mat& screenCapture, cv::Rect& gameRect) {
    if (screenCapture.empty()) return false;

    // 预处理：转灰度、平滑、边缘
    Mat gray;
    if (screenCapture.channels()==4) cvtColor(screenCapture, gray, COLOR_BGRA2GRAY);
    else if (screenCapture.channels()==3) cvtColor(screenCapture, gray, COLOR_BGR2GRAY);
    else gray = screenCapture.clone();
    Mat blur; GaussianBlur(gray, blur, Size(3,3), 0);
    Mat edges; Canny(blur, edges, 50, 150);
    Mat dil; morphologyEx(edges, dil, MORPH_CLOSE, getStructuringElement(MORPH_RECT, Size(3,3)));

    // 查找外部轮廓
    std::vector<std::vector<Point>> contours; std::vector<Vec4i> hierarchy;
    findContours(dil, contours, hierarchy, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    const int W = screenCapture.cols, H = screenCapture.rows;
    const double imgArea = double(W) * double(H);
    double bestScore = 0.0; Rect bestRect;
    for (auto& c : contours) {
        if (c.size() < 4) continue;
        double per = arcLength(c, true);
        std::vector<Point> approx; approxPolyDP(c, approx, 0.02 * per, true);
        if (approx.size() != 4) continue;
        if (!isContourConvex(approx)) continue;
        Rect r = boundingRect(approx);
        // 放宽过滤阈值：网页棋盘在不同缩放下可能较小
        if (r.width < 40 || r.height < 40) continue;
        double area = double(r.area());
        if (area > imgArea * 0.98) continue; // 排除几乎占满整个客户区的矩形
        // 放宽贴边限制：允许接近边缘
        double ar = double(r.width) / double(r.height);
        if (ar < 0.4 || ar > 2.5) continue;

        // 评分：面积 + 区域内边缘密度
        double score = area;
        Rect shrink = r; // 稍微向内收缩以减少外框干扰
        shrink.x += 2; shrink.y += 2; shrink.width = std::max(1, shrink.width-4); shrink.height = std::max(1, shrink.height-4);
        Mat roi = dil(shrink);
        double edgeCount = cv::countNonZero(roi);
        double density = edgeCount / double(shrink.area());
        score *= (0.5 + std::min(1.5, density * 4));
        if (score > bestScore) { bestScore = score; bestRect = r; }
    }

    if (bestScore <= 0.0) return false;
    gameRect = bestRect;
    return true;
}

bool WindowCapture::RefineBoardArea(const cv::Mat& roiImage, cv::Rect& gridRect) {
    using namespace cv;
    if (roiImage.empty()) return false;
    Mat gray;
    if (roiImage.channels()==4) cvtColor(roiImage, gray, COLOR_BGRA2GRAY);
    else if (roiImage.channels()==3) cvtColor(roiImage, gray, COLOR_BGR2GRAY);
    else gray = roiImage.clone();

    int H = gray.rows; int W = gray.cols;

    // 先用 HSV 检测顶部红色数字（七段数码管），得到 HUD 区域底边
    Mat bgr; if (roiImage.channels()==4) cvtColor(roiImage, bgr, COLOR_BGRA2BGR); else if (roiImage.channels()==1) cvtColor(roiImage, bgr, COLOR_GRAY2BGR); else bgr = roiImage;
    Mat hsv; cvtColor(bgr, hsv, COLOR_BGR2HSV);
    Mat mask1, mask2, redMask;
    // 红色在 HSV 中跨 0 和 180，取两段
    inRange(hsv, Scalar(0, 100, 80), Scalar(10, 255, 255), mask1);
    inRange(hsv, Scalar(160, 100, 80), Scalar(180, 255, 255), mask2);
    bitwise_or(mask1, mask2, redMask);
    // 仅考虑上半部分，避免棋盘内颜色干扰
    int topPct = std::clamp(m_hudTopRatioPercent.load(), 10, 70);
    Rect topHalf(0, 0, W, std::max(1, H*topPct/100));
    Mat redTop = redMask(topHalf);
    int hudBottom = -1;
    // 形态学处理，聚合数码管数字块
    Mat ker = getStructuringElement(MORPH_RECT, Size(3,3));
    morphologyEx(redTop, redTop, MORPH_CLOSE, ker);
    // 水平投影，找到红色像素密集的带
    std::vector<int> projR(redTop.rows,0);
    for (int y=0;y<redTop.rows;++y) projR[y] = countNonZero(redTop.row(y));
    int thrR = std::max(5, W/40);
    for (int y=0; y<redTop.rows; ++y) {
        if (projR[y] > thrR) hudBottom = y; // 取最后一个超过阈值的行为更稳
    }
    if (hudBottom >= 0) {
        int start = std::min(H-5, hudBottom + 4); // 在 HUD 下方留一点间隙
        // 初始上下裁剪
        int y0 = start, y1 = H - 1;
        // 左右精裁剪：在 [y0, y1] 之间做 Canny + 纵向投影
        Mat roiGray = gray(Rect(0, y0, W, y1 - y0 + 1));
        Mat e; Canny(roiGray, e, 50, 150);
        std::vector<int> vp(e.cols, 0);
        for (int x=0; x<e.cols; ++x) vp[x] = countNonZero(e.col(x));
        int thrX = std::max(4, (y1 - y0 + 1) / 30);
        int left = 0; while (left < (int)vp.size() && vp[left] < thrX) left++;
        int right = (int)vp.size()-1; while (right > left && vp[right] < thrX) right--;
        // 轻微内缩/外扩以避免吃掉边框
        left = std::max(0, left - 2);
        right = std::min((int)vp.size()-1, right + 2);
        int x0 = left, x1 = right;
        if (x1 - x0 < W/6) { x0 = 0; x1 = W-1; } // 保护：避免退化太窄
        gridRect = Rect(x0, y0, std::max(1, x1 - x0 + 1), std::max(1, y1 - y0 + 1));
        m_lastHudMethod = L"red";
        return true;
    }

    // 回退：边缘投影法
    Mat edges; Canny(gray, edges, 50, 150);
    std::vector<int> proj(edges.rows,0);
    for (int y=0;y<edges.rows;++y) proj[y] = countNonZero(edges.row(y));
    int start = 0, end = H-1;
    auto val = [&](int y){ return (y>=0 && y<H)? proj[y] : 0; };
    auto smooth = [&](int y){ return (val(y-1)+val(y)+val(y+1))/3; };
    int threshHigh = std::max(10, W/20);
    for (int y=std::max(0,H/50); y<H/2; ++y) if (smooth(y) > threshHigh) { start = std::max(0, y-2); break; }
    for (int y=H-1; y>start+20; --y) if (smooth(y) > threshHigh) { end = std::min(H-1, y+2); break; }
    if (end - start < H/6) { m_lastHudMethod = L"none"; return false; }
    // 左右精裁剪：在 [start, end] 之间做 Canny + 纵向投影
    {
        int y0 = start, y1 = end;
        Mat roiGray = gray(Rect(0, y0, W, y1 - y0 + 1));
        Mat e; Canny(roiGray, e, 50, 150);
        std::vector<int> vp(e.cols, 0);
        for (int x=0; x<e.cols; ++x) vp[x] = countNonZero(e.col(x));
        int thrX = std::max(4, (y1 - y0 + 1) / 30);
        int left = 0; while (left < (int)vp.size() && vp[left] < thrX) left++;
        int right = (int)vp.size()-1; while (right > left && vp[right] < thrX) right--;
        left = std::max(0, left - 2);
        right = std::min((int)vp.size()-1, right + 2);
        int x0 = left, x1 = right;
        if (x1 - x0 < W/6) { x0 = 0; x1 = W-1; }
        gridRect = Rect(x0, y0, std::max(1, x1 - x0 + 1), std::max(1, y1 - y0 + 1));
    }
    m_lastHudMethod = L"edges";
    return true;
}

bool WindowCapture::AnalyzeGridLayout(const cv::Mat& gameArea, int& rows, int& cols) {
    cv::Rect inner;
    if (AnalyzeGridLayoutEx(gameArea, rows, cols, inner)) return true;
    // 失败兜底
    rows = 16; cols = 16; return true;
}

static uint64_t hashBits(const cv::Mat& bits) {
    // 将一个小的 2 值图像打包为 64bit 哈希（最多 64 位，否则折叠）
    int total = bits.rows * bits.cols;
    uint64_t h = 1469598103934665603ull; // FNV-1a
    for (int i=0;i<bits.rows;++i) {
        const uchar* p = bits.ptr<uchar>(i);
        for (int j=0;j<bits.cols;++j) {
            uchar b = p[j] ? 1 : 0;
            h ^= b;
            h *= 1099511628211ull;
        }
    }
    // 若像素数>64，前面折叠已经覆盖；此处直接返回
    (void)total;
    return h;
}

bool WindowCapture::ExtractHudTimerSignature(const cv::Mat& roiImage, uint64_t& signature) {
    using namespace cv;
    if (roiImage.empty()) return false;
    Mat bgr; if (roiImage.channels()==4) cvtColor(roiImage, bgr, COLOR_BGRA2BGR); else if (roiImage.channels()==1) cvtColor(roiImage, bgr, COLOR_GRAY2BGR); else bgr = roiImage;
    int H = bgr.rows, W = bgr.cols;
    int topPct = std::clamp(m_hudTopRatioPercent.load(), 10, 70);
    // 仅取上部 topPct% 高度作为 HUD 搜索区
    Rect hudBand(0, 0, W, std::max(1, H*topPct/100));
    Mat hsv; cvtColor(bgr(hudBand), hsv, COLOR_BGR2HSV);
    Mat mask1, mask2, redMask;
    inRange(hsv, Scalar(0, 100, 80), Scalar(10, 255, 255), mask1);
    inRange(hsv, Scalar(160, 100, 80), Scalar(180, 255, 255), mask2);
    bitwise_or(mask1, mask2, redMask);
    // 形态学闭操作聚合数字段
    morphologyEx(redMask, redMask, MORPH_CLOSE, getStructuringElement(MORPH_RECT, Size(3,3)));
    // 粗略找到右上区域作为计时器：取右侧 45% 宽度的列
    int x0 = std::max(0, W*55/100);
    Rect rightTop(x0, 0, W - x0, redMask.rows);
    Mat candidate = redMask(rightTop);
    // 对候选区进行缩放到固定小尺寸并二值化，生成签名
    Mat small; resize(candidate, small, Size(16, 8), 0, 0, INTER_AREA);
    // 二值化到 0/255 -> 0/1
    threshold(small, small, 0, 255, THRESH_OTSU);
    // 轻度腐蚀去躁
    erode(small, small, getStructuringElement(MORPH_RECT, Size(2,1)));
    // 规范化为单通道 0/1
    Mat bits = small > 0;
    signature = hashBits(bits);
    return true;
}

bool WindowCapture::HasHudChanged(const cv::Mat& roiImage) {
    uint64_t sig = 0;
    if (!ExtractHudTimerSignature(roiImage, sig)) return false;
    if (!m_hasHudSignature) { m_lastHudSignature = sig; m_hasHudSignature = true; return true; }
    bool changed = (sig != m_lastHudSignature);
    m_lastHudSignature = sig;
    return changed;
}

bool WindowCapture::AnalyzeGridLayoutEx(const cv::Mat& boardImage, int& rows, int& cols, cv::Rect& innerRect) {
    using namespace cv;
    if (boardImage.empty()) return false;
    Mat gray;
    if (boardImage.channels()==4) cvtColor(boardImage, gray, COLOR_BGRA2GRAY);
    else if (boardImage.channels()==3) cvtColor(boardImage, gray, COLOR_BGR2GRAY);
    else gray = boardImage.clone();

    // 边缘检测并在内部收缩 3px，减少外框影响
    Mat edges; Canny(gray, edges, 40, 120);
    int H = edges.rows, W = edges.cols;
    int margin = std::max(1, std::min(W,H)/100 + 2);
    Rect inner(margin, margin, std::max(1,W-2*margin), std::max(1,H-2*margin));
    Mat e = edges(inner);

    // 水平与垂直投影
    std::vector<int> hp(e.rows,0), vp(e.cols,0);
    for (int y=0; y<e.rows; ++y) hp[y] = countNonZero(e.row(y));
    for (int x=0; x<e.cols; ++x) vp[x] = countNonZero(e.col(x));

    auto estimatePeriod = [](const std::vector<int>& p)->int{
        int n = (int)p.size();
        // 简单自相关：位移 k 的相关性最高的 k 即周期；限制区间 [5, 120]
        int bestK = 0; double bestScore = 0.0;
        int kMin = 5, kMax = std::min(120, n/2);
        for (int k=kMin; k<=kMax; ++k){
            double s = 0.0; int cnt = 0;
            for (int i=0; i+k<n; ++i){ s += double(p[i]) * double(p[i+k]); cnt++; }
            if (cnt>0){ s /= cnt; if (s>bestScore){ bestScore = s; bestK = k; } }
        }
        return bestK;
    };
    int periodY = estimatePeriod(hp);
    int periodX = estimatePeriod(vp);
    if (periodY <= 0 || periodX <= 0) return false;

    // 估计行列数：取投影中峰的数量（以周期为步长采样）
    auto countPeaks = [](const std::vector<int>& p, int step){
        int n = (int)p.size(); int cnt=0;
        for (int i=step/2; i<n; i+=step){
            int left = std::max(0, i - step/2), right = std::min(n-1, i + step/2);
            int mx = 0; for (int j=left;j<=right;++j) mx = std::max(mx, p[j]);
            if (mx > 0) cnt++;
        }
        return std::max(1, cnt);
    };
    int estRows = countPeaks(hp, periodY);
    int estCols = countPeaks(vp, periodX);

    // 将 inner 区域对齐到整周期边界，得到纯棋盘矩形
    int offsetY = (inner.y + periodY/2) % periodY;
    int offsetX = (inner.x + periodX/2) % periodX;
    int y0 = inner.y + (periodY - offsetY) % periodY;
    int x0 = inner.x + (periodX - offsetX) % periodX;
    int hCells = estRows;
    int wCells = estCols;
    int hPx = hCells * periodY;
    int wPx = wCells * periodX;
    // 边界防护
    if (y0 + hPx > H) hCells = std::max(1, (H - y0) / periodY), hPx = hCells*periodY;
    if (x0 + wPx > W) wCells = std::max(1, (W - x0) / periodX), wPx = wCells*periodX;

    rows = hCells; cols = wCells;
    innerRect = Rect(inner.x + (x0 - inner.x), inner.y + (y0 - inner.y), wPx, hPx);
    return true;
}

void WindowCapture::SetHudTopRatioPercent(int p) { m_hudTopRatioPercent.store(p); }
int WindowCapture::GetHudTopRatioPercent() const { return m_hudTopRatioPercent.load(); }
