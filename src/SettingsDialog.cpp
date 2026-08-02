// SettingsDialog.cpp - D2D 自绘暗色设置面板 v2
// 无边框窗口 + 自绘标题栏 + 色盘 + 透明度滑块
#include "pch.h"
#include "SettingsDialog.h"
#include "AppConfig.h"
#include "OverlayWindow.h"

#include <d2d1_1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <math.h>
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dwmapi.lib")

// ===================== 主题 =====================
namespace T {
    constexpr uint32_t kBg       = 0xFF1A1B2E;
    constexpr uint32_t kTitle    = 0xFF151627;
    constexpr uint32_t kCard     = 0xFF242640;
    constexpr uint32_t kAccent   = 0xFF6C63FF;
    constexpr uint32_t kAccentDim= 0xFF4A45B0;
    constexpr uint32_t kText     = 0xFFF0F0F5;
    constexpr uint32_t kDim      = 0xFF9CA3AF;
    constexpr uint32_t kBorder   = 0xFF3A3D5C;
    constexpr uint32_t kTogOn    = 0xFF6C63FF;
    constexpr uint32_t kTogOff   = 0xFF4B5563;
    constexpr uint32_t kTrack    = 0xFF374151;
    constexpr float kPad = 20.0f;
    constexpr float kRowH = 38.0f;
}

// ===================== 常量 =====================
static const int KW = 440, KH = 460;
static const int TITLE_H = 38, TAB_H = 38;
static const int CONTENT_Y = TITLE_H + TAB_H + 8;

enum Tab { TAB_SHOW, TAB_LOOK, TAB_COLOR, TAB_TEMP, TAB_NET, TAB_N };
static const wchar_t* kTabs[] = {L"显示", L"外观", L"颜色", L"温度", L"网络"};

// ===================== 色板 =====================
static const uint32_t kTxtColors[] = {
    0xFFFFFFFF, 0xFFF1F5F9, 0xFFE2E8F0, 0xFFFBBF24, 0xFFFB923C, 0xFFF87171,
    0xFFA78BFA, 0xFF818CF8, 0xFF60A5FA, 0xFF34D399, 0xFF4ADE80, 0xFF2DD4BF,
};
static const uint32_t kNetColors[] = {
    0xFFFF8C00, 0xFF00CED1, 0xFF60A5FA, 0xFF4ADE80, 0xFFF87171, 0xFFFFFFFF,
};

// ===================== 状态 =====================
static HWND g_hwnd = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static IDWriteFactory* g_dw = nullptr;
static IDWriteTextFormat* g_f12 = nullptr, *g_f11 = nullptr, *g_f13 = nullptr;
static int g_tab = 0;
static bool g_drag = false;
static int g_dragId = 0;
static ID2D1Bitmap* g_hueRing = nullptr;  // 色盘位图

// 字体列表
static std::vector<std::wstring> g_fonts;
static int g_fontScroll = 0;
static bool g_fontListOpen = false;

// Hit zones
struct Zone { int id; float x, y, w, h; };
static std::vector<Zone> g_zones;

// ===================== 工具 =====================
static D2D1_COLOR_F C(uint32_t a) {
    return D2D1::ColorF(((a>>16)&0xFF)/255.f, ((a>>8)&0xFF)/255.f, (a&0xFF)/255.f, ((a>>24)&0xFF)/255.f);
}
static void RR(float x, float y, float w, float h, float r, uint32_t fill, uint32_t stroke=0) {
    ID2D1SolidColorBrush* br; g_rt->CreateSolidColorBrush(C(fill), &br);
    g_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),r,r), br);
    if (stroke) { br->SetColor(C(stroke)); g_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x,y,x+w,y+h),r,r), br, 1.f); }
    br->Release();
}
static void Txt(const wchar_t* s, float x, float y, uint32_t c, IDWriteTextFormat* f=nullptr) {
    if (!f) f = g_f12;
    ID2D1SolidColorBrush* br; g_rt->CreateSolidColorBrush(C(c), &br);
    g_rt->DrawTextW(s, (UINT32)wcslen(s), f, D2D1::RectF(x,y,x+400,y+24), br);
    br->Release();
}
static void Zone2(int id, float x, float y, float w, float h) { g_zones.push_back({id,x,y,w,h}); }
static int Hit(float mx, float my) {
    for (auto it=g_zones.rbegin(); it!=g_zones.rend(); ++it)
        if (mx>=it->x && mx<=it->x+it->w && my>=it->y && my<=it->y+it->h) return it->id;
    return -1;
}

// ===================== 控件 =====================
static void Toggle(float x, float y, bool on, int id) {
    RR(x, y, 40, 22, 11, on ? T::kTogOn : T::kTogOff);
    ID2D1SolidColorBrush* br; g_rt->CreateSolidColorBrush(C(0xFFFFFFFF), &br);
    float cx = on ? x+29 : x+11;
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, y+11), 8, 8), br);
    br->Release();
    Zone2(id, x-4, y-4, 48, 30);
}
static void Swatch(float x, float y, float sz, uint32_t color, bool sel, int id) {
    RR(x, y, sz, sz, 5, color, sel ? T::kAccent : T::kBorder);
    if (sel) {
        ID2D1SolidColorBrush* br; g_rt->CreateSolidColorBrush(C(T::kAccent), &br);
        D2D1_ROUNDED_RECT r2 = D2D1::RoundedRect(D2D1::RectF(x-2,y-2,x+sz+2,y+sz+2), 7, 7);
        g_rt->DrawRoundedRectangle(&r2, br, 2.f); br->Release();
    }
    Zone2(id, x-3, y-3, sz+6, sz+6);
}
static void Slider(float x, float y, float w, float pct, uint32_t fillC, int id) {
    if (pct<0) pct=0; if (pct>1) pct=1;
    RR(x, y+5, w, 6, 3, T::kTrack);
    if (pct > 0.02f) RR(x, y+5, pct*w, 6, 3, fillC);
    ID2D1SolidColorBrush* br; g_rt->CreateSolidColorBrush(C(0xFFFFFFFF), &br);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+pct*w, y+8), 7, 7), br);
    br->Release();
    Zone2(id, x-8, y-2, w+16, 20);
}
static void Pill(float x, float y, const wchar_t* label, bool sel, int id) {
    float w = 52;
    RR(x, y, w, 26, 7, sel ? T::kAccent : T::kCard, sel ? 0 : T::kBorder);
    float tw = wcslen(label) * 12.f;
    Txt(label, x + (w-tw)/2, y+5, sel ? 0xFFFFFFFF : T::kDim, g_f11);
    Zone2(id, x, y, w, 26);
}

// ===================== 色盘 =====================
static void CreateHueRing() {
    if (g_hueRing || !g_rt) return;
    const int S = 130;
    // 生成色环位图
    std::vector<uint8_t> pixels(S * S * 4);
    float cx = S/2.f, cy = S/2.f;
    float outerR = S/2.f - 2, innerR = S/2.f - 22;
    for (int py = 0; py < S; py++) {
        for (int px = 0; px < S; px++) {
            float dx = px - cx, dy = py - cy;
            float dist = sqrtf(dx*dx + dy*dy);
            int idx = (py * S + px) * 4;
            if (dist >= innerR && dist <= outerR) {
                float angle = atan2f(dy, dx);  // -PI ~ PI
                float hue = (angle + 3.14159265f) / (2*3.14159265f);  // 0~1
                // HSV -> RGB (S=1, V=1)
                float h6 = hue * 6.f;
                int hi = (int)h6 % 6;
                float f = h6 - (int)h6;
                float q = 1-f, t2 = f;
                float r,g,b;
                switch(hi) {
                    case 0: r=1;g=t2;b=0; break;
                    case 1: r=q;g=1;b=0; break;
                    case 2: r=0;g=1;b=t2; break;
                    case 3: r=0;g=q;b=1; break;
                    case 4: r=t2;g=0;b=1; break;
                    default: r=1;g=0;b=q; break;
                }
                pixels[idx+0] = (uint8_t)(r*255);
                pixels[idx+1] = (uint8_t)(g*255);
                pixels[idx+2] = (uint8_t)(b*255);
                pixels[idx+3] = 255;
            } else {
                pixels[idx+3] = 0;  // 透明
            }
        }
    }
    D2D1_BITMAP_PROPERTIES bp = {};
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX = 96; bp.dpiY = 96;
    // 转换 RGBA -> BGRA
    for (int i = 0; i < S*S; i++) {
        std::swap(pixels[i*4+0], pixels[i*4+2]);
    }
    g_rt->CreateBitmap(D2D1::SizeU(S, S), pixels.data(), S*4, bp, &g_hueRing);
}

static void DrawColorWheel(float x, float y) {
    CreateHueRing();
    if (g_hueRing) {
        g_rt->DrawBitmap(g_hueRing, D2D1::RectF(x, y, x+130, y+130));
    }
    // 中心暗圆
    ID2D1SolidColorBrush* br; g_rt->CreateSolidColorBrush(C(T::kBg), &br);
    g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x+65, y+65), 38, 38), br);
    br->Release();
    // 色环点击区域
    Zone2(320, x, y, 130, 130);
}

// ===================== 页面渲染 =====================
static void PageShow(float y) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    struct { const wchar_t* l; bool v; int id; } items[] = {
        {L"CPU 温度", dc.showCpuTemp, 100}, {L"CPU 占用率", dc.showCpuUsage, 101},
        {L"GPU 温度", dc.showGpuTemp, 102}, {L"GPU 占用率", dc.showGpuUsage, 103},
        {L"内存占用", dc.showMemUsage, 104}, {L"网络上行", dc.showNetUp, 105},
        {L"网络下行", dc.showNetDown, 106},
    };
    for (auto& it : items) {
        Txt(it.l, T::kPad+4, y+6, T::kText);
        Toggle(KW - T::kPad - 44, y+2, it.v, it.id);
        // 分隔线
        ID2D1SolidColorBrush* br; g_rt->CreateSolidColorBrush(C(T::kBorder), &br);
        g_rt->DrawLine(D2D1::Point2F(T::kPad, y+T::kRowH), D2D1::Point2F((float)KW-T::kPad, y+T::kRowH), br, 0.5f);
        br->Release();
        y += T::kRowH;
    }
}

static void PageLook(float y) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    // 字体
    Txt(L"显示字体", T::kPad+4, y, T::kDim, g_f11); y += 20;
    RR(T::kPad, y, KW-T::kPad*2, 30, 7, T::kCard, T::kBorder);
    Txt(dc.fontFamily.c_str(), T::kPad+10, y+6, T::kText);
    Txt(L"▾", KW-T::kPad-20, y+6, T::kDim);
    Zone2(200, T::kPad, y, KW-T::kPad*2, 30);
    y += 36;
    // 字体列表
    if (g_fontListOpen && !g_fonts.empty()) {
        float lh = 6 * 22.f;
        RR(T::kPad, y, KW-T::kPad*2, lh, 7, T::kCard, T::kBorder);
        for (int i = 0; i < 6 && g_fontScroll+i < (int)g_fonts.size(); i++) {
            int idx = g_fontScroll + i;
            float iy = y + 2 + i*22.f;
            bool sel = (g_fonts[idx] == dc.fontFamily);
            if (sel) RR(T::kPad+3, iy, KW-T::kPad*2-6, 20, 4, T::kAccentDim);
            Txt(g_fonts[idx].c_str(), T::kPad+10, iy+2, sel ? T::kText : T::kDim, g_f11);
            Zone2(210+idx, T::kPad+3, iy, KW-T::kPad*2-6, 20);
        }
        y += lh + 8;
    }
    // 文字大小
    Txt(L"文字大小", T::kPad+4, y, T::kDim, g_f11); y += 20;
    int sz = (dc.fontSize<=10)?0:(dc.fontSize<=12)?1:2;
    Pill(T::kPad, y, L"小", sz==0, 220); Pill(T::kPad+60, y, L"中", sz==1, 221); Pill(T::kPad+120, y, L"大", sz==2, 222);
    y += 34;
    // 间距
    Txt(L"项目间距", T::kPad+4, y, T::kDim, g_f11); y += 20;
    int sp = (dc.spacingScale<=0.85f)?0:(dc.spacingScale<=1.15f)?1:2;
    Pill(T::kPad, y, L"紧凑", sp==0, 230); Pill(T::kPad+60, y, L"标准", sp==1, 231); Pill(T::kPad+120, y, L"宽松", sp==2, 232);
    y += 36;
    // 透明度
    wchar_t buf[32]; swprintf_s(buf, L"整体透明度: %d%%", (int)(dc.overlayOpacity*100));
    Txt(buf, T::kPad+4, y, T::kDim, g_f11); y += 20;
    Slider(T::kPad, y, KW-T::kPad*2, (dc.overlayOpacity-0.3f)/0.7f, T::kAccent, 250);
    y += 28;
    // 开关
    Txt(L"指标前彩色圆点", T::kPad+4, y+6, T::kText);
    Toggle(KW-T::kPad-44, y+2, dc.showIndicatorDots, 240);
    y += T::kRowH;
    Txt(L"指标间竖线分隔", T::kPad+4, y+6, T::kText);
    Toggle(KW-T::kPad-44, y+2, dc.showSeparator, 241);
}

static void PageColor(float y) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    // 文字色板
    Txt(L"文字颜色", T::kPad+4, y, T::kDim, g_f11); y += 20;
    for (int i = 0; i < 12; i++) {
        float sx = T::kPad + (i%6)*34.f;
        float sy = y + (i/6)*34.f;
        Swatch(sx, sy, 26, kTxtColors[i], kTxtColors[i]==dc.textColor, 300+i);
    }
    y += 74;
    // 色盘
    Txt(L"自定义颜色", T::kPad+4, y, T::kDim, g_f11); y += 20;
    DrawColorWheel(T::kPad, y);
    // 明度条
    float bx = T::kPad + 145;
    Txt(L"明度", bx, y, T::kDim, g_f11);
    // 明度渐变条（用多段矩形模拟）
    for (int i = 0; i < 20; i++) {
        float v = i / 19.f;
        uint32_t gc = 0xFF000000 | ((uint32_t)(v*255)<<16) | ((uint32_t)(v*255)<<8) | (uint32_t)(v*255);
        RR(bx, y+18+i*5.f, 100, 5, 0, gc);
    }
    Zone2(321, bx-4, y+16, 108, 104);
    // 当前色预览
    RR(bx, y+126, 36, 22, 5, dc.textColor, T::kBorder);
    Txt(L"当前色", bx+42, y+129, T::kDim, g_f11);
    y += 156;
    // 网络色
    Txt(L"网络上行色", T::kPad+4, y, T::kDim, g_f11); y += 20;
    for (int i = 0; i < 6; i++) Swatch(T::kPad+i*34.f, y, 26, kNetColors[i], kNetColors[i]==dc.netUpColor, 340+i);
    y += 34;
    Txt(L"网络下行色", T::kPad+4, y, T::kDim, g_f11); y += 20;
    for (int i = 0; i < 6; i++) Swatch(T::kPad+i*34.f, y, 26, kNetColors[i], kNetColors[i]==dc.netDownColor, 350+i);
}

static void PageTemp(float y) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    Txt(L"温度色阶渐变", T::kPad+4, y+6, T::kText);
    Txt(L"低温绿→高温红", T::kPad+120, y+8, T::kDim, g_f11);
    Toggle(KW-T::kPad-44, y+2, dc.tempColorGradient, 400);
    y += T::kRowH + 4;
    // 渐变条
    for (int i = 0; i < 40; i++) {
        float t = i/39.f;
        uint8_t r = (uint8_t)(t < 0.5f ? t*2*255 : 255);
        uint8_t g = (uint8_t)(t < 0.5f ? 200 : (1-(t-0.5f)*2)*200);
        uint32_t gc = 0xFF000000 | ((uint32_t)r<<16) | ((uint32_t)g<<8) | 0x40;
        RR(T::kPad + i*(KW-T::kPad*2)/40.f, y, (KW-T::kPad*2)/40.f+1, 8, 0, gc);
    }
    y += 18;
    // 阈值滑块
    wchar_t buf[32];
    swprintf_s(buf, L"开始变色: %.0f°C", dc.tempLowThreshold);
    Txt(buf, T::kPad+4, y, T::kDim, g_f11); y += 18;
    Slider(T::kPad, y, KW-T::kPad*2, (dc.tempLowThreshold-30)/40.f, 0xFF34D399, 410);
    y += 28;
    swprintf_s(buf, L"全红温度: %.0f°C", dc.tempHighThreshold);
    Txt(buf, T::kPad+4, y, T::kDim, g_f11); y += 18;
    Slider(T::kPad, y, KW-T::kPad*2, (dc.tempHighThreshold-70)/40.f, 0xFFF87171, 411);
}

static void PageNet(float y) {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    Txt(L"上下行异色", T::kPad+4, y+6, T::kText);
    Txt(L"关闭则统一用文字色", T::kPad+100, y+8, T::kDim, g_f11);
    Toggle(KW-T::kPad-44, y+2, dc.netColorSplit, 500);
    y += T::kRowH + 8;
    Txt(L"上行颜色", T::kPad+4, y, T::kDim, g_f11); y += 20;
    for (int i = 0; i < 6; i++) Swatch(T::kPad+i*34.f, y, 26, kNetColors[i], kNetColors[i]==dc.netUpColor, 510+i);
    y += 36;
    Txt(L"下行颜色", T::kPad+4, y, T::kDim, g_f11); y += 20;
    for (int i = 0; i < 6; i++) Swatch(T::kPad+i*34.f, y, 26, kNetColors[i], kNetColors[i]==dc.netDownColor, 520+i);
}

// ===================== 主渲染 =====================
static void Render() {
    if (!g_rt) return;
    g_zones.clear();
    g_rt->BeginDraw();
    g_rt->Clear(C(T::kBg));
    g_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

    // 标题栏
    RR(0, 0, (float)KW, (float)TITLE_H, 0, T::kTitle);
    Txt(L"TaskbarStudio 设置", 14, 10, T::kText, g_f13);
    // 关闭按钮
    RR(KW-36, 6, 26, 26, 6, 0x00000000);
    Txt(L"✕", KW-30, 10, T::kDim);
    Zone2(999, KW-36, 6, 26, 26);

    // Tab 栏
    float tw = (KW - 24.f) / TAB_N;
    for (int i = 0; i < TAB_N; i++) {
        float tx = 12 + i*tw;
        if (i == g_tab) RR(tx+2, TITLE_H+5, tw-4, TAB_H-10, 7, T::kAccent);
        float lw = wcslen(kTabs[i]) * 13.f;
        Txt(kTabs[i], tx + (tw-lw)/2, TITLE_H+12, i==g_tab ? 0xFFFFFFFF : T::kDim);
        Zone2(900+i, tx, TITLE_H+2, tw, TAB_H-4);
    }
    // 分隔线
    ID2D1SolidColorBrush* br; g_rt->CreateSolidColorBrush(C(T::kBorder), &br);
    g_rt->DrawLine(D2D1::Point2F(12, (float)CONTENT_Y-4), D2D1::Point2F(KW-12.f, (float)CONTENT_Y-4), br, 0.5f);
    br->Release();

    // 页面
    float cy = (float)CONTENT_Y + 4;
    switch (g_tab) {
        case TAB_SHOW: PageShow(cy); break;
        case TAB_LOOK: PageLook(cy); break;
        case TAB_COLOR: PageColor(cy); break;
        case TAB_TEMP: PageTemp(cy); break;
        case TAB_NET: PageNet(cy); break;
    }

    g_rt->EndDraw();
}

// ===================== 配置应用 =====================
static void Apply() {
    AppConfig::Instance().Save();
    const DisplayConfig& dc = AppConfig::Instance().Get();
    OverlayConfig oc{};
    oc.showCpuTemp=dc.showCpuTemp; oc.showCpuUsage=dc.showCpuUsage;
    oc.showGpuTemp=dc.showGpuTemp; oc.showGpuUsage=dc.showGpuUsage;
    oc.showMemUsage=dc.showMemUsage; oc.showNetUp=dc.showNetUp; oc.showNetDown=dc.showNetDown;
    oc.fontSize=dc.fontSize; oc.textColor=dc.textColor; oc.accentColor=dc.accentColor;
    oc.showIndicatorDots=dc.showIndicatorDots; oc.tempColorGradient=dc.tempColorGradient;
    oc.netColorSplit=dc.netColorSplit; oc.showSeparator=dc.showSeparator;
    oc.netUpColor=dc.netUpColor; oc.netDownColor=dc.netDownColor;
    oc.fontFamily=dc.fontFamily; oc.tempLowThreshold=dc.tempLowThreshold;
    oc.tempHighThreshold=dc.tempHighThreshold; oc.spacingScale=dc.spacingScale;
    oc.overlayOpacity=dc.overlayOpacity; oc.alignRight=false;
    OverlayWindow::Instance().SetConfig(oc);
}

// ===================== 交互 =====================
static void Click(int id) {
    DisplayConfig dc = AppConfig::Instance().Get();
    bool ch = false;
    if (id == 999) { SettingsDialog::Close(); return; }
    if (id >= 900 && id < 900+TAB_N) { g_tab = id-900; InvalidateRect(g_hwnd,nullptr,FALSE); return; }
    // 显示开关
    if (id >= 100 && id <= 106) {
        bool* f[] = {&dc.showCpuTemp,&dc.showCpuUsage,&dc.showGpuTemp,&dc.showGpuUsage,&dc.showMemUsage,&dc.showNetUp,&dc.showNetDown};
        *f[id-100] = !*f[id-100]; ch = true;
    }
    // 外观
    if (id == 200) { g_fontListOpen = !g_fontListOpen; }
    if (id >= 210 && id < 210+(int)g_fonts.size()) { dc.fontFamily = g_fonts[id-210]; ch = true; }
    if (id >= 220 && id <= 222) { dc.fontSize = (id==220)?10.f:(id==221)?12.f:14.f; ch = true; }
    if (id >= 230 && id <= 232) { dc.spacingScale = (id==230)?0.8f:(id==231)?1.f:1.3f; ch = true; }
    if (id == 240) { dc.showIndicatorDots = !dc.showIndicatorDots; ch = true; }
    if (id == 241) { dc.showSeparator = !dc.showSeparator; ch = true; }
    // 颜色
    if (id >= 300 && id < 312) { dc.textColor = kTxtColors[id-300]; ch = true; }
    if (id >= 340 && id < 346) { dc.netUpColor = kNetColors[id-340]; ch = true; }
    if (id >= 350 && id < 356) { dc.netDownColor = kNetColors[id-350]; ch = true; }
    // 温度
    if (id == 400) { dc.tempColorGradient = !dc.tempColorGradient; ch = true; }
    // 网络
    if (id == 500) { dc.netColorSplit = !dc.netColorSplit; ch = true; }
    if (id >= 510 && id < 516) { dc.netUpColor = kNetColors[id-510]; ch = true; }
    if (id >= 520 && id < 526) { dc.netDownColor = kNetColors[id-520]; ch = true; }

    if (ch) { AppConfig::Instance().Set(dc); Apply(); InvalidateRect(g_hwnd,nullptr,FALSE); }
}

static void DragSlider(int id, float mx, float my) {
    DisplayConfig dc = AppConfig::Instance().Get();
    bool ch = false;
    float x0 = T::kPad, w = KW - T::kPad*2;
    float pct = (mx - x0) / w;
    if (pct<0) pct=0; if (pct>1) pct=1;
    switch (id) {
        case 250: dc.overlayOpacity = 0.3f + pct * 0.7f; ch = true; break;
        case 410: dc.tempLowThreshold = 30.f + pct * 40.f; ch = true; break;
        case 411: dc.tempHighThreshold = 70.f + pct * 40.f; ch = true; break;
        case 321: { // 明度条（垂直）
            float vy = (my - (CONTENT_Y + 238.f)) / 100.f;
            if (vy<0) vy=0; if (vy>1) vy=1;
            uint8_t v = (uint8_t)((1.f-vy)*255);
            dc.textColor = (dc.textColor & 0xFF000000) | ((uint32_t)v<<16) | ((uint32_t)v<<8) | v;
            ch = true; break;
        }
        case 320: { // 色环
            float cx = T::kPad + 65, cy2 = (float)CONTENT_Y + 4 + 20 + 65;
            float dx = mx - cx, dy = my - cy2;
            float dist = sqrtf(dx*dx+dy*dy);
            if (dist > 38 && dist < 65) {
                float angle = atan2f(dy, dx);
                float hue = (angle + 3.14159265f) / (2*3.14159265f);
                float h6 = hue * 6.f; int hi = (int)h6 % 6; float f = h6-(int)h6;
                float r,g,b;
                switch(hi) {
                    case 0: r=1;g=f;b=0; break; case 1: r=1-f;g=1;b=0; break;
                    case 2: r=0;g=1;b=f; break; case 3: r=0;g=1-f;b=1; break;
                    case 4: r=f;g=0;b=1; break; default: r=1;g=0;b=1-f; break;
                }
                dc.textColor = 0xFF000000 | ((uint32_t)(r*255)<<16) | ((uint32_t)(g*255)<<8) | (uint32_t)(b*255);
                ch = true;
            }
            break;
        }
    }
    if (ch) { AppConfig::Instance().Set(dc); Apply(); InvalidateRect(g_hwnd,nullptr,FALSE); }
}

// ===================== 字体枚举 =====================
static int CALLBACK FontCb(const LOGFONTW* lf, const TEXTMETRICW* tm, DWORD type, LPARAM lp) {
    if (!(type & TRUETYPE_FONTTYPE)) return 1;
    if (lf->lfCharSet == SYMBOL_CHARSET || lf->lfFaceName[0] == L'@') return 1;
    if (tm->tmHeight > 0 && tm->tmInternalLeading > tm->tmHeight/3) return 1;
    auto* list = (std::vector<std::wstring>*)lp;
    std::wstring n = lf->lfFaceName;
    if (std::find(list->begin(), list->end(), n) == list->end()) list->push_back(n);
    return 1;
}
static void LoadFonts() {
    g_fonts.clear();
    const wchar_t* rec[] = {L"Inter",L"LXGW WenKai",L"Maple Mono SC",L"MiSans",L"Sarasa UI SC",L"Segoe UI",L"Microsoft YaHei"};
    for (auto* r : rec) g_fonts.push_back(r);
    HDC hdc = GetDC(nullptr);
    LOGFONTW lf{}; lf.lfCharSet = DEFAULT_CHARSET;
    EnumFontFamiliesExW(hdc, &lf, FontCb, (LPARAM)&g_fonts, 0);
    ReleaseDC(nullptr, hdc);
    if (g_fonts.size() > 7) std::sort(g_fonts.begin()+7, g_fonts.end());
}

// ===================== 窗口过程 =====================
static LRESULT CALLBACK WndProc(HWND hw, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: { PAINTSTRUCT ps; BeginPaint(hw,&ps); Render(); EndPaint(hw,&ps); return 0; }
        case WM_LBUTTONDOWN: {
            float mx=(float)LOWORD(lp), my=(float)HIWORD(lp);
            int id = Hit(mx, my);
            if (id==250||id==410||id==411||id==320||id==321) {
                g_drag=true; g_dragId=id; SetCapture(hw); DragSlider(id,mx,my);
            } else if (id>=0) Click(id);
            return 0;
        }
        case WM_MOUSEMOVE: { if (g_drag) DragSlider(g_dragId, (float)LOWORD(lp), (float)HIWORD(lp)); return 0; }
        case WM_LBUTTONUP: { if (g_drag) { g_drag=false; ReleaseCapture(); } return 0; }
        case WM_MOUSEWHEEL: {
            if (g_fontListOpen) {
                g_fontScroll -= GET_WHEEL_DELTA_WPARAM(wp)/120;
                int mx2 = (int)g_fonts.size()-6; if (g_fontScroll<0) g_fontScroll=0; if (g_fontScroll>mx2) g_fontScroll=mx2;
                InvalidateRect(hw,nullptr,FALSE);
            }
            return 0;
        }
        // 拖动窗口（标题栏区域）
        case WM_NCHITTEST: {
            float mx = (float)LOWORD(lp), my = (float)HIWORD(lp);
            // 转换为 client 坐标
            POINT pt = {LOWORD(lp), HIWORD(lp)};
            ScreenToClient(hw, &pt);
            if (pt.y < TITLE_H && pt.x < KW-40) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_CLOSE: SettingsDialog::Close(); return 0;
        case WM_DESTROY:
            if (g_hueRing) { g_hueRing->Release(); g_hueRing=nullptr; }
            if (g_rt) { g_rt->Release(); g_rt=nullptr; }
            if (g_f12) { g_f12->Release(); g_f12=nullptr; }
            if (g_f11) { g_f11->Release(); g_f11=nullptr; }
            if (g_f13) { g_f13->Release(); g_f13=nullptr; }
            if (g_dw) { g_dw->Release(); g_dw=nullptr; }
            g_hwnd = nullptr; return 0;
        default: return DefWindowProcW(hw, msg, wp, lp);
    }
}

// ===================== 公共接口 =====================
void SettingsDialog::Show(HWND owner) {
    if (g_hwnd && IsWindow(g_hwnd)) { SetForegroundWindow(g_hwnd); return; }
    WNDCLASSEXW wc{}; wc.cbSize=sizeof(wc); wc.lpfnWndProc=WndProc;
    wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"TSSettingsV2";
    wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=nullptr;
    RegisterClassExW(&wc);

    int x=(GetSystemMetrics(SM_CXSCREEN)-KW)/2, y=(GetSystemMetrics(SM_CYSCREEN)-KH)/2;
    g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"TSSettingsV2", L"",
        WS_POPUP|WS_VISIBLE, x, y, KW, KH, owner, nullptr, wc.hInstance, nullptr);
    if (!g_hwnd) return;

    // 圆角
    DWORD attr = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &attr, sizeof(attr));
    // 阴影
    MARGINS m = {1,1,1,1};
    DwmExtendFrameIntoClientArea(g_hwnd, &m);

    // D2D
    ID2D1Factory* fac=nullptr;
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &fac);
    RECT rc; GetClientRect(g_hwnd, &rc);
    fac->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
        D2D1::HwndRenderTargetProperties(g_hwnd, D2D1::SizeU(rc.right,rc.bottom)), &g_rt);
    fac->Release();

    // DWrite
    DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dw);
    if (g_dw) {
        g_dw->CreateTextFormat(L"Inter",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,12.f,L"zh-CN",&g_f12);
        g_dw->CreateTextFormat(L"Inter",nullptr,DWRITE_FONT_WEIGHT_NORMAL,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,11.f,L"zh-CN",&g_f11);
        g_dw->CreateTextFormat(L"Inter",nullptr,DWRITE_FONT_WEIGHT_MEDIUM,DWRITE_FONT_STYLE_NORMAL,DWRITE_FONT_STRETCH_NORMAL,13.f,L"zh-CN",&g_f13);
    }
    LoadFonts();
    g_fontListOpen = false; g_fontScroll = 0;
    ShowWindow(g_hwnd, SW_SHOW);
}

void SettingsDialog::Close() { if (g_hwnd) DestroyWindow(g_hwnd); }
bool SettingsDialog::IsOpen() { return g_hwnd && IsWindow(g_hwnd); }
