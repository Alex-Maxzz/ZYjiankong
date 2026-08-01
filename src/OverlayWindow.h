// OverlayWindow.h - 任务栏悬浮数据条窗口
// 使用 DirectComposition + Direct2D 实现透明窗口 + 硬件加速渲染
// 显示：CPU温度/占用 GPU温度/占用 内存 网络↑↓
#pragma once
#include "pch.h"

// 显示项配置（哪些指标显示在任务栏上）
struct OverlayConfig {
    bool showCpuTemp  = true;
    bool showCpuUsage = true;
    bool showGpuTemp  = true;
    bool showGpuUsage = true;
    bool showMemUsage = true;
    bool showNetUp    = true;
    bool showNetDown  = true;

    // 外观
    float    fontSize    = 12.0f;       // 紧凑字号
    uint32_t textColor   = 0xFFFFFFFF;  // RGBA 文字颜色
    uint32_t accentColor = 0xFF4A90E2;  // 强调色（箭头/状态点）
    bool     showIndicatorDots = false; // 是否显示彩色状态点

    // 位置
    bool alignRight = true;   // 右对齐（靠托盘区）
};

class OverlayWindow {
public:
    static OverlayWindow& Instance();

    bool Create(const OverlayConfig& config);
    void Destroy();

    // 刷新显示数据（从 Monitor 拉取并重绘）
    void Update();

    // 显示/隐藏控制（全屏时隐藏）
    void Show(bool visible);
    bool IsVisible() const { return m_visible; }

    // 更新配置
    void SetConfig(const OverlayConfig& config);

    // 重新置顶到 Z-order 最顶（防止被 TranslucentTB 等同类置顶窗口遮挡）
    void BringToTop();

    HWND GetHwnd() const { return m_hwnd; }

private:
    OverlayWindow();
    ~OverlayWindow();
    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    // 初始化 D3D11 + D2D1 + DirectComposition
    bool InitD3D();
    bool InitD2D();
    bool InitComposition();
    bool CreateFontsAndBrushes();   // 仅创建字体和画刷（不重建设备）
    void ReleaseAll();

    // 窗口位置：附着任务栏
    void UpdatePosition();
    void OnDpiChanged();

    // 渲染
    void Render();
    void DrawMetrics(ID2D1DeviceContext* ctx, const SystemMetrics& metrics);
    void DrawTextLeft(ID2D1DeviceContext* ctx, const std::wstring& text,
                      ID2D1Brush* brush, IDWriteTextFormat* fmt,
                      float fontSizePx, float padX, float& x, float y);

    // 窗口过程
    static LRESULT CALLBACK WndProcStatic(HWND, UINT, WPARAM, LPARAM);
    LRESULT WndProc(UINT msg, WPARAM wp, LPARAM lp);

    // 工具：格式化网络速率
    // 例：1234 -> "1.2K"，固定宽度防跳位
    static std::wstring FormatRate(uint64_t bytesPerSec);
    static std::wstring FormatTemp(float temp);
    static std::wstring FormatPercent(float pct);

    HWND                    m_hwnd{nullptr};
    OverlayConfig           m_config{};
    bool                    m_inited{false};
    bool                    m_visible{true};

    // D3D11
    ID3D11Device*           m_d3dDevice{nullptr};
    IDXGIDevice*            m_dxgiDevice{nullptr};
    IDXGISwapChain1*        m_swapChain{nullptr};
    ID3D11DeviceContext*    m_d3dContext{nullptr};

    // Direct2D
    ID2D1Factory1*          m_d2dFactory{nullptr};
    ID2D1Device*            m_d2dDevice{nullptr};
    ID2D1DeviceContext*     m_d2dContext{nullptr};
    ID2D1Bitmap1*           m_d2dBitmap{nullptr};    // 渲染目标位图（复用）

    // DirectWrite
    IDWriteFactory*         m_dwriteFactory{nullptr};
    IDWriteTextFormat*      m_textFormat{nullptr};   // 主字体
    IDWriteTextFormat*      m_monoFormat{nullptr};   // 等宽（数字）

    // 画刷
    ID2D1SolidColorBrush*   m_brushText{nullptr};
    ID2D1SolidColorBrush*   m_brushAccent{nullptr};
    ID2D1SolidColorBrush*   m_brushBg{nullptr};

    // DirectComposition
    IDCompositionDevice*    m_dcompDevice{nullptr};
    IDCompositionTarget*    m_dcompTarget{nullptr};
    IDCompositionVisual*    m_dcompVisual{nullptr};

    UINT                    m_dpi{96};
    int                     m_taskbarHeight{48};
};
