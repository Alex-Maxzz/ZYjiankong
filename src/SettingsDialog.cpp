// SettingsDialog.cpp - D2D 自绘暗色设置面板
// 零原生控件，全部 Direct2D 渲染，现代暗色主题
#include "pch.h"
#include "SettingsDialog.h"
#include "AppConfig.h"
#include "OverlayWindow.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <dwmapi.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

// ===================== 主题常量 =====================

namespace Theme {
    constexpr uint32_t kBg         = 0xFF1A1B2E;  // 深蓝黑背景
    constexpr uint32_t kCard       = 0xFF242640;  // 卡片/区块背景
    constexpr uint32_t kAccent     = 0xFF6C63FF;  // 主强调色（紫）
    constexpr uint32_t kAccentDim  = 0xFF4A45B0;  // 强调色暗
    constexpr uint32_t kText       = 0xFFF0F0F5;  // 主文字
    constexpr uint32_t kTextDim    = 0xFF9CA3AF;  // 次文字
    constexpr uint32_t kBorder     = 0xFF3A3D5C;  // 边框
    constexpr uint32_t kToggleOn   = 0xFF6C63FF;  // 开关开
    constexpr uint32_t kToggleOff  = 0xFF4B5563;  // 开关关
    constexpr uint32_t kSliderBg   = 0xFF374151;  // 滑轨背景
    constexpr float    kRadius     = 8.0f;        // 圆角
    constexpr float    kPadX       = 20.0f;       // 水平内边距
    constexpr float    kRowH       = 36.0f;       // 行高
}

// ===================== 尺寸 =====================

static const int kWinW = 440;
static const int kWinH = 420;
static const int kTabH = 40;
static const int kContentY = kTabH + 10;

// ===================== Tab 定义 =====================

enum Tab { TAB_DISPLAY = 0, TAB_APPEAR, TAB_COLOR, TAB_TEMP, TAB_NET, TAB_COUNT };
static const wchar_t* kTabNames[] = {L"显示", L"外观", L"颜色", L"温度", L"网络"};

// ===================== 色板 =====================

struct Swatch { uint32_t argb; };
static const Swatch kTextColors[] = {
    {0xFFFFFFFF}, {0xFFF1F5F9}, {0xFFE2E8F0}, {0xFFFBBF24},
    {0xFFFB923C}, {0xFFF87171}, {0xFFA78BFA}, {0xFF818CF8},
    {0xFF60A5FA}, {0xFF34D399}, {0xFF4ADE80}, {0xFF2DD4BF},
};
static const int kTextColorCount = 12;

static const Swatch kNetColors[] = {
    {0xFFFF8C00}, {0xFF00CED1}, {0xFF60A5FA}, {0xFF4ADE80}, {0xFFF87171}, {0xFFFFFFFF},
};
static const int kNetColorCount = 6;

// ===================== 字体列表 =====================

static std::vector<std::wstring> g_fonts;
static int g_fontScroll = 0;
static const int kFontVisibleRows = 6;

// ===================== 状态 =====================

static HWND g_hwnd = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static IDWriteFactory* g_dwFactory = nullptr;
static IDWriteTextFormat* g_font = nullptr;      // 12px 正文
static IDWriteTextFormat* g_fontSm = nullptr;    // 11px 小字
static IDWriteTextFormat* g_fontTitle = nullptr; // 14px 标题
static int g_activeTab = 0;
static bool g_draggingSlider = false;
static int g_dragSliderId = 0;  // 0=tempLow, 1=tempHigh, 2=R, 3=G, 4=B

// 交互区域缓存（每帧重建）
struct HitZone { int id; float x, y, w, h; };
static std::vector<HitZone> g_zones;

// ===================== 工具函数 =====================

static D2D1_COLOR_F C(uint32_t argb) {
    return D2D1::ColorF(
        ((argb >> 16) & 0xFF) / 255.0f,
        ((argb >> 8) & 0xFF) / 255.0f,
        (argb & 0xFF) / 255.0f,
        ((argb >> 24) & 0xFF) / 255.0f);
}

static void RoundRect(float x, float y, float w, float h, float r, uint32_t fill, uint32_t stroke = 0) {
    auto geom = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r);
    ID2D1SolidColorBrush* br = nullptr;
    g_rt->CreateSolidColorBrush(C(fill), &br);
    g_rt->FillRoundedRectangle(geom, br);
    if (stroke) {
        br->SetColor(C(stroke));
        g_rt->DrawRoundedRectangle(geom, br, 1.0f);
    }
    br->Release();
}

static void Text(const wchar_t* str, float x, float y, uint32_t color, IDWriteTextFormat* fmt = nullptr) {
    if (!fmt) fmt = g_font;
    ID2D1SolidColorBrush* br = nullptr;
    g_rt->CreateSolidColorBrush(C(color), &br);
    D2D1_RECT_F rc = D2D1::RectF(x, y, x + 400, y + 30);
    g_rt->DrawTextW(str, (UINT32)wcslen(str), fmt, rc, br);
    br->Release();
}

static void AddZone(int id, float x, float y, float w, float h) {
    g_zones.push_back({id, x, y, w, h});
}

static int HitTest(float mx, float my) {
    for (auto it = g_zones.rbegin(); it != g_zones.rend(); ++it) {
        if (mx >= it->x && mx <= it->x + it->w && my >= it->y && my <= it->y + it->h)
            return it->id;
    }
    return -1;
}

// ===================== 控件绘制 =====================

// iOS 风格开关
static void DrawToggle(float x, float y, bool on, int zoneId) {
    float w = 40, h = 22, r = 11;
    uint32_t bg = on ? Theme::kToggleOn : Theme::kToggleOff;
    RoundRect(x, y, w, h, r, bg);
    // 圆点
    float dotR = 8;
    float dotX = on ? (x + w - 11 - dotR + r - 3) : (x + 11 - r + 3 + dotR);
    dotX = on ? x + w - 14 : x + 14;
    ID2D1SolidColorBrush* br = nullptr;
    g_rt->CreateSolidColorBrush(C(0xFFFFFFFF), &br);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, y + h / 2), dotR, dotR), br);
    br->Release();
    AddZone(zoneId, x - 2, y - 2, w + 4, h + 4);
}

// 色块
static void DrawSwatch(float x, float y, float size, uint32_t color, bool selected, int zoneId) {
    float r = 4.0f;
    RoundRect(x, y, size, size, r, color, selected ? Theme::kAccent : Theme::kBorder);
    if (selected) {
        // 选中环
        ID2D1SolidColorBrush* br = nullptr;
        g_rt->CreateSolidColorBrush(C(Theme::kAccent), &br);
        D2D1_ROUNDED_RECT ring = D2D1::RoundedRect(
            D2D1::RectF(x - 2, y - 2, x + size + 2, y + size + 2), r + 2, r + 2);
        g_rt->DrawRoundedRectangle(&ring, br, 2.0f);
        br->Release();
    }
    AddZone(zoneId, x - 2, y - 2, size + 4, size + 4);
}

// 水平滑块
static void DrawSlider(float x, float y, float w, float val, float minV, float maxV,
                        uint32_t trackColor, uint32_t fillColor, int zoneId) {
    float h = 6, r = 3, thumbR = 8;
    float pct = (val - minV) / (maxV - minV);
    if (pct < 0) pct = 0; if (pct > 1) pct = 1;
    float fillW = pct * w;
    // 轨道
    RoundRect(x, y + thumbR - h / 2, w, h, r, trackColor);
    // 填充
    if (fillW > r * 2) RoundRect(x, y + thumbR - h / 2, fillW, h, r, fillColor);
    // 滑块
    float tx = x + fillW;
    ID2D1SolidColorBrush* br = nullptr;
    g_rt->CreateSolidColorBrush(C(0xFFFFFFFF), &br);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(tx, y + thumbR), thumbR, thumbR), br);
    br->Release();
    AddZone(zoneId, x - thumbR, y, w + thumbR * 2, thumbR * 2 + 4);
}

// ===================== 页面渲染 =====================

static void RenderDisplayPage(float startY) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    float y = startY;
    struct Item { const wchar_t* label; bool val; int id; };
    Item items[] = {
        {L"CPU 温度", dc.showCpuTemp, 100},
        {L"CPU 占用率", dc.showCpuUsage, 101},
        {L"GPU 温度", dc.showGpuTemp, 102},
        {L"GPU 占用率", dc.showGpuUsage, 103},
        {L"内存占用", dc.showMemUsage, 104},
        {L"网络上行", dc.showNetUp, 105},
        {L"网络下行", dc.showNetDown, 106},
    };
    for (auto& item : items) {
        Text(item.label, Theme::kPadX + 4, y + 4, Theme::kText);
        DrawToggle(kWinW - Theme::kPadX - 44, y, item.val, item.id);
        y += Theme::kRowH;
    }
}

static void RenderAppearPage(float startY) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    float y = startY;

    // 字体选择（简化：显示当前字体 + 上下切换）
    Text(L"显示字体", Theme::kPadX + 4, y + 4, Theme::kTextDim, g_fontSm);
    y += 22;
    RoundRect(Theme::kPadX, y, kWinW - Theme::kPadX * 2, 30, 6, Theme::kCard, Theme::kBorder);
    Text(dc.fontFamily.c_str(), Theme::kPadX + 10, y + 6, Theme::kText);
    AddZone(200, Theme::kPadX, y, kWinW - Theme::kPadX * 2, 30);  // 点击弹出字体列表
    y += 38;

    // 字体列表（可滚动）
    if (g_fontScroll >= 0 && !g_fonts.empty()) {
        float listH = kFontVisibleRows * 24.0f;
        RoundRect(Theme::kPadX, y, kWinW - Theme::kPadX * 2, listH, 6, Theme::kCard, Theme::kBorder);
        for (int i = 0; i < kFontVisibleRows && (g_fontScroll + i) < (int)g_fonts.size(); i++) {
            int idx = g_fontScroll + i;
            float iy = y + 2 + i * 24.0f;
            bool isSel = (g_fonts[idx] == dc.fontFamily);
            if (isSel) {
                RoundRect(Theme::kPadX + 2, iy, kWinW - Theme::kPadX * 2 - 4, 22, 4, Theme::kAccentDim);
            }
            Text(g_fonts[idx].c_str(), Theme::kPadX + 10, iy + 3,
                isSel ? Theme::kText : Theme::kTextDim, g_fontSm);
            AddZone(210 + idx, Theme::kPadX + 2, iy, kWinW - Theme::kPadX * 2 - 4, 22);
        }
        y += listH + 8;
    }

    // 文字大小
    Text(L"文字大小", Theme::kPadX + 4, y + 4, Theme::kTextDim, g_fontSm);
    y += 22;
    const wchar_t* sizes[] = {L"小", L"中", L"大"};
    int sizeIdx = (dc.fontSize <= 10.0f) ? 0 : (dc.fontSize <= 12.0f) ? 1 : 2;
    for (int i = 0; i < 3; i++) {
        float bx = Theme::kPadX + i * 60;
        bool sel = (i == sizeIdx);
        RoundRect(bx, y, 52, 26, 6, sel ? Theme::kAccent : Theme::kCard, sel ? 0 : Theme::kBorder);
        Text(sizes[i], bx + 18, y + 5, sel ? 0xFFFFFFFF : Theme::kTextDim);
        AddZone(220 + i, bx, y, 52, 26);
    }
    y += 34;

    // 项目间距
    Text(L"项目间距", Theme::kPadX + 4, y + 4, Theme::kTextDim, g_fontSm);
    y += 22;
    const wchar_t* sps[] = {L"紧凑", L"标准", L"宽松"};
    int spIdx = (dc.spacingScale <= 0.85f) ? 0 : (dc.spacingScale <= 1.15f) ? 1 : 2;
    for (int i = 0; i < 3; i++) {
        float bx = Theme::kPadX + i * 60;
        bool sel = (i == spIdx);
        RoundRect(bx, y, 52, 26, 6, sel ? Theme::kAccent : Theme::kCard, sel ? 0 : Theme::kBorder);
        Text(sps[i], bx + 12, y + 5, sel ? 0xFFFFFFFF : Theme::kTextDim);
        AddZone(230 + i, bx, y, 52, 26);
    }
    y += 36;

    // 开关
    Text(L"指标前彩色圆点", Theme::kPadX + 4, y + 4, Theme::kText);
    DrawToggle(kWinW - Theme::kPadX - 44, y, dc.showIndicatorDots, 240);
    y += Theme::kRowH;
    Text(L"指标间竖线分隔", Theme::kPadX + 4, y + 4, Theme::kText);
    DrawToggle(kWinW - Theme::kPadX - 44, y, dc.showSeparator, 241);
}

static void RenderColorPage(float startY) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    float y = startY;

    // 文字颜色色板
    Text(L"文字颜色", Theme::kPadX + 4, y, Theme::kTextDim, g_fontSm);
    y += 20;
    float swSize = 26, gap = 6;
    for (int i = 0; i < kTextColorCount; i++) {
        float sx = Theme::kPadX + (i % 6) * (swSize + gap);
        float sy = y + (i / 6) * (swSize + gap);
        bool sel = (kTextColors[i].argb == dc.textColor);
        DrawSwatch(sx, sy, swSize, kTextColors[i].argb, sel, 300 + i);
    }
    y += (swSize + gap) * 2 + 10;

    // RGB 滑块（自定义颜色）
    Text(L"自定义颜色", Theme::kPadX + 4, y, Theme::kTextDim, g_fontSm);
    y += 22;
    uint8_t cr = (dc.textColor >> 16) & 0xFF;
    uint8_t cg = (dc.textColor >> 8) & 0xFF;
    uint8_t cb = dc.textColor & 0xFF;
    float sliderW = kWinW - Theme::kPadX * 2 - 50;
    // R
    Text(L"R", Theme::kPadX + 4, y + 2, 0xFFF87171, g_fontSm);
    DrawSlider(Theme::kPadX + 20, y, sliderW, cr, 0, 255, Theme::kSliderBg, 0xFFEF4444, 320);
    y += 26;
    // G
    Text(L"G", Theme::kPadX + 4, y + 2, 0xFF4ADE80, g_fontSm);
    DrawSlider(Theme::kPadX + 20, y, sliderW, cg, 0, 255, Theme::kSliderBg, 0xFF22C55E, 321);
    y += 26;
    // B
    Text(L"B", Theme::kPadX + 4, y + 2, 0xFF60A5FA, g_fontSm);
    DrawSlider(Theme::kPadX + 20, y, sliderW, cb, 0, 255, Theme::kSliderBg, 0xFF3B82F6, 322);
    y += 30;

    // 预览色块
    RoundRect(Theme::kPadX, y, 40, 24, 4, dc.textColor, Theme::kBorder);
    Text(L"当前文字色预览", Theme::kPadX + 50, y + 5, Theme::kTextDim, g_fontSm);
    y += 34;

    // 网络颜色
    Text(L"网络上行色", Theme::kPadX + 4, y, Theme::kTextDim, g_fontSm);
    y += 20;
    for (int i = 0; i < kNetColorCount; i++) {
        float sx = Theme::kPadX + i * (swSize + gap);
        bool sel = (kNetColors[i].argb == dc.netUpColor);
        DrawSwatch(sx, y, swSize, kNetColors[i].argb, sel, 340 + i);
    }
    y += swSize + gap + 8;

    Text(L"网络下行色", Theme::kPadX + 4, y, Theme::kTextDim, g_fontSm);
    y += 20;
    for (int i = 0; i < kNetColorCount; i++) {
        float sx = Theme::kPadX + i * (swSize + gap);
        bool sel = (kNetColors[i].argb == dc.netDownColor);
        DrawSwatch(sx, y, swSize, kNetColors[i].argb, sel, 350 + i);
    }
    y += swSize + gap + 10;

    Text(L"上下行异色", Theme::kPadX + 4, y + 4, Theme::kText);
    DrawToggle(kWinW - Theme::kPadX - 44, y, dc.netColorSplit, 360);
}

static void RenderTempPage(float startY) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    float y = startY;

    Text(L"温度色阶渐变", Theme::kPadX + 4, y + 4, Theme::kText);
    Text(L"低温绿 → 高温红", Theme::kPadX + 130, y + 6, Theme::kTextDim, g_fontSm);
    DrawToggle(kWinW - Theme::kPadX - 44, y, dc.tempColorGradient, 400);
    y += Theme::kRowH + 8;

    // 低温阈值滑块
    wchar_t buf[32];
    swprintf_s(buf, L"开始变色: %.0f°C", dc.tempLowThreshold);
    Text(buf, Theme::kPadX + 4, y, Theme::kTextDim, g_fontSm);
    y += 20;
    float sliderW = kWinW - Theme::kPadX * 2;
    DrawSlider(Theme::kPadX, y, sliderW, dc.tempLowThreshold, 30, 70, Theme::kSliderBg, 0xFF34D399, 410);
    y += 34;

    // 高温阈值滑块
    swprintf_s(buf, L"全红温度: %.0f°C", dc.tempHighThreshold);
    Text(buf, Theme::kPadX + 4, y, Theme::kTextDim, g_fontSm);
    y += 20;
    DrawSlider(Theme::kPadX, y, sliderW, dc.tempHighThreshold, 70, 110, Theme::kSliderBg, 0xFFF87171, 411);
}

static void RenderNetPage(float startY) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    float y = startY;
    Text(L"上下行异色", Theme::kPadX + 4, y + 4, Theme::kText);
    Text(L"关闭则统一用文字色", Theme::kPadX + 110, y + 6, Theme::kTextDim, g_fontSm);
    DrawToggle(kWinW - Theme::kPadX - 44, y, dc.netColorSplit, 500);
    y += Theme::kRowH + 8;
    Text(L"颜色在[颜色]页设置", Theme::kPadX + 4, y, Theme::kTextDim, g_fontSm);
}

// ===================== 主渲染 =====================

static void Render() {
    if (!g_rt) return;
    g_zones.clear();

    g_rt->BeginDraw();
    g_rt->Clear(C(Theme::kBg));

    // Tab 栏
    float tabW = (float)(kWinW - 20) / TAB_COUNT;
    for (int i = 0; i < TAB_COUNT; i++) {
        float tx = 10 + i * tabW;
        bool active = (i == g_activeTab);
        if (active) {
            RoundRect(tx + 2, 6, tabW - 4, kTabH - 12, 6, Theme::kAccent);
        }
        // Tab 文字居中
        const wchar_t* name = kTabNames[i];
        float textW = (float)wcslen(name) * 12.0f;
        Text(name, tx + (tabW - textW) / 2, 14, active ? 0xFFFFFFFF : Theme::kTextDim);
        AddZone(900 + i, tx, 4, tabW, kTabH - 8);
    }

    // 分隔线
    ID2D1SolidColorBrush* lineBr = nullptr;
    g_rt->CreateSolidColorBrush(C(Theme::kBorder), &lineBr);
    g_rt->DrawLine(D2D1::Point2F(10, (float)kTabH), D2D1::Point2F((float)kWinW - 10, (float)kTabH), lineBr);
    lineBr->Release();

    // 页面内容
    float startY = (float)kContentY + 6;
    switch (g_activeTab) {
        case TAB_DISPLAY: RenderDisplayPage(startY); break;
        case TAB_APPEAR:  RenderAppearPage(startY); break;
        case TAB_COLOR:   RenderColorPage(startY); break;
        case TAB_TEMP:    RenderTempPage(startY); break;
        case TAB_NET:     RenderNetPage(startY); break;
    }

    HRESULT hr = g_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        g_rt->Release(); g_rt = nullptr;
    }
}

// ===================== 配置应用 =====================

static void ApplyConfig() {
    AppConfig::Instance().Save();
    const DisplayConfig& dc = AppConfig::Instance().Get();
    OverlayConfig oc{};
    oc.showCpuTemp = dc.showCpuTemp; oc.showCpuUsage = dc.showCpuUsage;
    oc.showGpuTemp = dc.showGpuTemp; oc.showGpuUsage = dc.showGpuUsage;
    oc.showMemUsage = dc.showMemUsage; oc.showNetUp = dc.showNetUp; oc.showNetDown = dc.showNetDown;
    oc.fontSize = dc.fontSize; oc.textColor = dc.textColor; oc.accentColor = dc.accentColor;
    oc.showIndicatorDots = dc.showIndicatorDots; oc.tempColorGradient = dc.tempColorGradient;
    oc.netColorSplit = dc.netColorSplit; oc.showSeparator = dc.showSeparator;
    oc.netUpColor = dc.netUpColor; oc.netDownColor = dc.netDownColor;
    oc.fontFamily = dc.fontFamily; oc.tempLowThreshold = dc.tempLowThreshold;
    oc.tempHighThreshold = dc.tempHighThreshold; oc.spacingScale = dc.spacingScale;
    oc.alignRight = false;
    OverlayWindow::Instance().SetConfig(oc);
}

// ===================== 交互处理 =====================

static void HandleClick(int zoneId) {
    DisplayConfig dc = AppConfig::Instance().Get();
    bool changed = false;

    // Tab 切换
    if (zoneId >= 900 && zoneId < 900 + TAB_COUNT) {
        g_activeTab = zoneId - 900;
        InvalidateRect(g_hwnd, nullptr, FALSE);
        return;
    }

    // 显示页开关
    if (zoneId >= 100 && zoneId <= 106) {
        bool* flags[] = {&dc.showCpuTemp, &dc.showCpuUsage, &dc.showGpuTemp,
                         &dc.showGpuUsage, &dc.showMemUsage, &dc.showNetUp, &dc.showNetDown};
        *flags[zoneId - 100] = !*flags[zoneId - 100];
        changed = true;
    }

    // 外观页
    if (zoneId == 200) { g_fontScroll = (g_fontScroll < 0) ? 0 : -1; }  // 切换字体列表
    if (zoneId >= 210 && zoneId < 210 + (int)g_fonts.size()) {
        dc.fontFamily = g_fonts[zoneId - 210]; changed = true;
    }
    if (zoneId >= 220 && zoneId <= 222) {
        dc.fontSize = (zoneId == 220) ? 10.0f : (zoneId == 221) ? 12.0f : 14.0f; changed = true;
    }
    if (zoneId >= 230 && zoneId <= 232) {
        dc.spacingScale = (zoneId == 230) ? 0.8f : (zoneId == 231) ? 1.0f : 1.3f; changed = true;
    }
    if (zoneId == 240) { dc.showIndicatorDots = !dc.showIndicatorDots; changed = true; }
    if (zoneId == 241) { dc.showSeparator = !dc.showSeparator; changed = true; }

    // 颜色页
    if (zoneId >= 300 && zoneId < 300 + kTextColorCount) {
        dc.textColor = kTextColors[zoneId - 300].argb; changed = true;
    }
    if (zoneId >= 340 && zoneId < 340 + kNetColorCount) {
        dc.netUpColor = kNetColors[zoneId - 340].argb; changed = true;
    }
    if (zoneId >= 350 && zoneId < 350 + kNetColorCount) {
        dc.netDownColor = kNetColors[zoneId - 350].argb; changed = true;
    }
    if (zoneId == 360) { dc.netColorSplit = !dc.netColorSplit; changed = true; }

    // 温度页
    if (zoneId == 400) { dc.tempColorGradient = !dc.tempColorGradient; changed = true; }

    // 网络页
    if (zoneId == 500) { dc.netColorSplit = !dc.netColorSplit; changed = true; }

    if (changed) {
        AppConfig::Instance().Set(dc);
        ApplyConfig();
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

static void HandleSliderDrag(int zoneId, float mx) {
    DisplayConfig dc = AppConfig::Instance().Get();
    float sliderX = Theme::kPadX + (zoneId >= 320 && zoneId <= 322 ? 20 : 0);
    float sliderW = kWinW - Theme::kPadX * 2 - (zoneId >= 320 && zoneId <= 322 ? 50 : 0);
    float pct = (mx - sliderX) / sliderW;
    if (pct < 0) pct = 0; if (pct > 1) pct = 1;

    bool changed = false;
    switch (zoneId) {
        case 320: { uint8_t v = (uint8_t)(pct * 255); dc.textColor = (dc.textColor & 0xFF00FFFF) | ((uint32_t)v << 16); changed = true; break; }
        case 321: { uint8_t v = (uint8_t)(pct * 255); dc.textColor = (dc.textColor & 0xFFFF00FF) | ((uint32_t)v << 8); changed = true; break; }
        case 322: { uint8_t v = (uint8_t)(pct * 255); dc.textColor = (dc.textColor & 0xFFFFFF00) | v; changed = true; break; }
        case 410: dc.tempLowThreshold = 30.0f + pct * 40.0f; changed = true; break;
        case 411: dc.tempHighThreshold = 70.0f + pct * 40.0f; changed = true; break;
    }
    if (changed) {
        AppConfig::Instance().Set(dc);
        ApplyConfig();
        InvalidateRect(g_hwnd, nullptr, FALSE);
    }
}

// ===================== 字体枚举 =====================

static int CALLBACK FontEnumProc2(const LOGFONTW* lf, const TEXTMETRICW* tm, DWORD type, LPARAM lp) {
    if (!(type & TRUETYPE_FONTTYPE)) return 1;
    if (lf->lfCharSet == SYMBOL_CHARSET) return 1;
    if (lf->lfFaceName[0] == L'@') return 1;
    if (tm->tmHeight > 0 && tm->tmInternalLeading > tm->tmHeight / 3) return 1;
    auto* list = reinterpret_cast<std::vector<std::wstring>*>(lp);
    std::wstring name = lf->lfFaceName;
    if (std::find(list->begin(), list->end(), name) == list->end())
        list->push_back(name);
    return 1;
}

static void LoadFonts() {
    g_fonts.clear();
    const wchar_t* rec[] = {L"Inter", L"LXGW WenKai", L"Maple Mono SC", L"MiSans",
                            L"Sarasa UI SC", L"Segoe UI", L"Microsoft YaHei"};
    for (auto* r : rec) g_fonts.push_back(r);
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf{}; lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(hdc, &lf, FontEnumProc2, (LPARAM)&g_fonts, 0);
    ReleaseDC(nullptr, hdc);
    if (g_fonts.size() > 7) std::sort(g_fonts.begin() + 7, g_fonts.end());
}

// ===================== 窗口过程 =====================

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            Render();
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            float mx = (float)LOWORD(lp), my = (float)HIWORD(lp);
            int id = HitTest(mx, my);
            // 检查是否是滑块
            if ((id >= 320 && id <= 322) || id == 410 || id == 411) {
                g_draggingSlider = true;
                g_dragSliderId = id;
                SetCapture(hwnd);
                HandleSliderDrag(id, mx);
            } else if (id >= 0) {
                HandleClick(id);
            }
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (g_draggingSlider) {
                float mx = (float)LOWORD(lp);
                HandleSliderDrag(g_dragSliderId, mx);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (g_draggingSlider) {
                g_draggingSlider = false;
                ReleaseCapture();
            }
            return 0;
        }
        case WM_MOUSEWHEEL: {
            // 字体列表滚动
            if (g_activeTab == TAB_APPEAR && g_fontScroll >= 0) {
                int delta = GET_WHEEL_DELTA_WPARAM(wp);
                g_fontScroll -= delta / 120;
                int maxScroll = (int)g_fonts.size() - kFontVisibleRows;
                if (g_fontScroll < 0) g_fontScroll = 0;
                if (g_fontScroll > maxScroll) g_fontScroll = maxScroll;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_CLOSE:
            SettingsDialog::Close();
            return 0;
        case WM_DESTROY:
            if (g_rt) { g_rt->Release(); g_rt = nullptr; }
            if (g_font) { g_font->Release(); g_font = nullptr; }
            if (g_fontSm) { g_fontSm->Release(); g_fontSm = nullptr; }
            if (g_fontTitle) { g_fontTitle->Release(); g_fontTitle = nullptr; }
            if (g_dwFactory) { g_dwFactory->Release(); g_dwFactory = nullptr; }
            g_hwnd = nullptr;
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ===================== 公共接口 =====================

void SettingsDialog::Show(HWND owner) {
    if (g_hwnd && IsWindow(g_hwnd)) { SetForegroundWindow(g_hwnd); return; }

    // 注册类
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"TSSettingsD2D";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    // 居中
    int x = (GetSystemMetrics(SM_CXSCREEN) - kWinW) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - kWinH) / 2;

    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"TSSettingsD2D", L"TaskbarStudio 设置",
        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
        x, y, kWinW, kWinH, owner, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) return;

    // 圆角 (Win11)
    DWORD attr = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &attr, sizeof(attr));

    // D2D
    ID2D1Factory* factory = nullptr;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &factory);
    RECT rc; GetClientRect(g_hwnd, &rc);
    factory->CreateHwndRenderTarget(
        D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(rc.right, rc.bottom)),
        &g_rt);
    factory->Release();

    // DirectWrite
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&g_dwFactory));
    if (g_dwFactory) {
        g_dwFactory->CreateTextFormat(L"Inter", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, L"zh-CN", &g_font);
        g_dwFactory->CreateTextFormat(L"Inter", nullptr, DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.0f, L"zh-CN", &g_fontSm);
        g_dwFactory->CreateTextFormat(L"Inter", nullptr, DWRITE_FONT_WEIGHT_MEDIUM,
            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, L"zh-CN", &g_fontTitle);
    }

    LoadFonts();
    g_fontScroll = -1;  // 默认收起字体列表
    ShowWindow(g_hwnd, SW_SHOW);
}

void SettingsDialog::Close() {
    if (g_hwnd) DestroyWindow(g_hwnd);
}

bool SettingsDialog::IsOpen() {
    return g_hwnd && IsWindow(g_hwnd);
}
