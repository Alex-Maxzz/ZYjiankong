// OverlayWindow.cpp - 任务栏悬浮数据条实现
// 透明窗口 + DirectComposition + Direct2D 硬件加速渲染
#include "pch.h"
#include "Monitor.h"
#include "OverlayWindow.h"

#include <dcomp.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <dwrite.h>

#pragma comment(lib, "dcomp.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

static const wchar_t* kOverlayClassName = L"TaskbarStudioOverlay";
static const wchar_t* kOverlayTitle     = L"TaskbarStudio";

// ============================================================
//  构造/析构
// ============================================================

OverlayWindow& OverlayWindow::Instance() {
    static OverlayWindow inst;
    return inst;
}

OverlayWindow::OverlayWindow() {}

OverlayWindow::~OverlayWindow() {
    Destroy();
}

// ============================================================
//  窗口创建
// ============================================================

bool OverlayWindow::Create(const OverlayConfig& config) {
    m_config = config;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &OverlayWindow::WndProcStatic;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kOverlayClassName;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&wc);

    // WS_EX_NOREDIRECTIONBITMAP: 透明窗口，必须配合 DirectComposition
    // WS_EX_TRANSPARENT: 鼠标穿透
    // WS_EX_TOOLWINDOW: 不在任务栏/Alt+Tab 显示
    // WS_EX_TOPMOST: 置顶
    DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_TRANSPARENT |
                    WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    DWORD style   = WS_POPUP;

    m_hwnd = CreateWindowExW(exStyle, kOverlayClassName, kOverlayTitle,
        style, 0, 0, 1, 1, nullptr, nullptr, wc.hInstance, this);
    if (!m_hwnd) return false;

    SetWindowLongPtrW(m_hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));

    m_dpi = GetDpiForWindow(m_hwnd);

    if (!InitD3D()) { ReleaseAll(); return false; }
    if (!InitD2D()) { ReleaseAll(); return false; }
    if (!InitComposition()) { ReleaseAll(); return false; }

    UpdatePosition();
    ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
    BringToTop();   // 启动后立即置顶（防止被先启动的 TranslucentTB 遮挡）

    m_inited = true;
    return true;
}

void OverlayWindow::BringToTop() {
    if (!m_hwnd) return;
    // HWND_TOPMOST 会把窗口放在所有 TOPMOST 窗口的最上面
    // SWP_NOACTIVATE 不抢焦点，SWP_NOMOVE|SWP_NOSIZE 保持位置和尺寸
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void OverlayWindow::Destroy() {
    // 先销毁窗口（阻止后续消息分发），再释放 COM 资源
    if (m_hwnd) {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    ReleaseAll();
    UnregisterClassW(kOverlayClassName, GetModuleHandleW(nullptr));
}

// ============================================================
//  D3D11 初始化
// ============================================================

bool OverlayWindow::InitD3D() {
    D3D_FEATURE_LEVEL fl;
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
        D3D11_SDK_VERSION, &m_d3dDevice, &fl, &m_d3dContext);
    if (FAILED(hr) || !m_d3dDevice) {
        // 回退 WARP 软件渲染
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, 0,
            D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr, 0,
            D3D11_SDK_VERSION, &m_d3dDevice, &fl, &m_d3dContext);
        if (FAILED(hr)) return false;
    }

    hr = m_d3dDevice->QueryInterface(__uuidof(IDXGIDevice),
        reinterpret_cast<void**>(&m_dxgiDevice));
    return SUCCEEDED(hr);
}

bool OverlayWindow::InitD2D() {
    D2D1_FACTORY_OPTIONS options = {};
    options.debugLevel = D2D1_DEBUG_LEVEL_NONE;
    HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
        options, &m_d2dFactory);
    if (FAILED(hr)) return false;

    hr = m_d2dFactory->CreateDevice(m_dxgiDevice, &m_d2dDevice);
    if (FAILED(hr)) return false;

    hr = m_d2dDevice->CreateDeviceContext(
        D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_d2dContext);
    if (FAILED(hr)) return false;

    // DirectWrite
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
        __uuidof(IDWriteFactory),
        reinterpret_cast<IUnknown**>(&m_dwriteFactory));
    if (FAILED(hr)) return false;

    return CreateFontsAndBrushes();
}

bool OverlayWindow::CreateFontsAndBrushes() {
    if (!m_dwriteFactory || !m_d2dContext) return false;

    // 主字体（紧凑无衬线）
    if (m_textFormat) { m_textFormat->Release(); m_textFormat = nullptr; }
    m_dwriteFactory->CreateTextFormat(
        L"Segoe UI Variable", nullptr,
        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_config.fontSize * m_dpi / 96.0f, L"zh-CN", &m_textFormat);

    // 等宽数字字体（防跳位）
    if (m_monoFormat) { m_monoFormat->Release(); m_monoFormat = nullptr; }
    m_dwriteFactory->CreateTextFormat(
        L"Cascadia Mono", nullptr,
        DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL,
        m_config.fontSize * m_dpi / 96.0f, L"en-US", &m_monoFormat);
    if (!m_monoFormat) {
        m_dwriteFactory->CreateTextFormat(
            L"Consolas", nullptr,
            DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            m_config.fontSize * m_dpi / 96.0f, L"en-US", &m_monoFormat);
    }

    // 画刷（仅创建一次，颜色变化时 SetColor）
    if (!m_brushText) {
        m_d2dContext->CreateSolidColorBrush(UintToColorF(m_config.textColor), &m_brushText);
    }
    if (!m_brushAccent) {
        m_d2dContext->CreateSolidColorBrush(UintToColorF(m_config.accentColor), &m_brushAccent);
    }
    if (!m_brushBg) {
        D2D1_COLOR_F cBg = { 0, 0, 0, 0 };
        m_d2dContext->CreateSolidColorBrush(cBg, &m_brushBg);
    }
    if (!m_brushNetUp) {
        m_d2dContext->CreateSolidColorBrush(UintToColorF(m_config.netUpColor), &m_brushNetUp);
    }
    if (!m_brushNetDown) {
        m_d2dContext->CreateSolidColorBrush(UintToColorF(m_config.netDownColor), &m_brushNetDown);
    }
    if (!m_brushSeparator) {
        m_d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.2f), &m_brushSeparator);
    }

    return m_textFormat && m_monoFormat && m_brushText && m_brushAccent &&
           m_brushNetUp && m_brushNetDown;
}

bool OverlayWindow::InitComposition() {
    HRESULT hr = DCompositionCreateDevice(m_dxgiDevice,
        __uuidof(IDCompositionDevice),
        reinterpret_cast<void**>(&m_dcompDevice));
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateTargetForHwnd(m_hwnd, TRUE, &m_dcompTarget);
    if (FAILED(hr)) return false;

    hr = m_dcompDevice->CreateVisual(&m_dcompVisual);
    if (FAILED(hr)) return false;

    // 创建交换链
    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width       = 1;
    scd.Height      = 1;
    scd.Format      = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc  = { 1, 0 };
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 2;
    scd.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    scd.AlphaMode   = DXGI_ALPHA_MODE_PREMULTIPLIED;
    scd.Flags       = 0;  // 不做 GDI 绘制，无需 GDI_COMPATIBLE

    IDXGIFactory2* dxgiFactory = nullptr;
    IDXGIAdapter* adapter = nullptr;
    m_dxgiDevice->GetAdapter(&adapter);
    if (adapter) {
        adapter->GetParent(__uuidof(IDXGIFactory2),
            reinterpret_cast<void**>(&dxgiFactory));
        adapter->Release();
    }
    if (!dxgiFactory) return false;

    hr = dxgiFactory->CreateSwapChainForComposition(m_d3dDevice, &scd, nullptr, &m_swapChain);
    dxgiFactory->Release();
    if (FAILED(hr)) return false;

    m_dcompVisual->SetContent(m_swapChain);
    m_dcompTarget->SetRoot(m_dcompVisual);
    m_dcompDevice->Commit();
    return true;
}

void OverlayWindow::ReleaseAll() {
    // 释放顺序：依赖方在前
    if (m_d2dBitmap)    { m_d2dBitmap->Release();    m_d2dBitmap = nullptr; }
    if (m_brushBg)      { m_brushBg->Release();      m_brushBg = nullptr; }
    if (m_brushSeparator){ m_brushSeparator->Release(); m_brushSeparator = nullptr; }
    if (m_brushNetDown) { m_brushNetDown->Release();  m_brushNetDown = nullptr; }
    if (m_brushNetUp)   { m_brushNetUp->Release();    m_brushNetUp = nullptr; }
    if (m_brushAccent)  { m_brushAccent->Release();  m_brushAccent = nullptr; }
    if (m_brushText)    { m_brushText->Release();    m_brushText = nullptr; }
    if (m_monoFormat)   { m_monoFormat->Release();   m_monoFormat = nullptr; }
    if (m_textFormat)   { m_textFormat->Release();   m_textFormat = nullptr; }
    if (m_dwriteFactory){ m_dwriteFactory->Release(); m_dwriteFactory = nullptr; }
    if (m_dcompVisual)  { m_dcompVisual->Release();  m_dcompVisual = nullptr; }
    if (m_dcompTarget)  { m_dcompTarget->Release();  m_dcompTarget = nullptr; }
    if (m_dcompDevice)  { m_dcompDevice->Release();  m_dcompDevice = nullptr; }
    if (m_d2dContext)   { m_d2dContext->Release();   m_d2dContext = nullptr; }
    if (m_d2dDevice)    { m_d2dDevice->Release();    m_d2dDevice = nullptr; }
    if (m_d2dFactory)   { m_d2dFactory->Release();   m_d2dFactory = nullptr; }
    if (m_swapChain)    { m_swapChain->Release();    m_swapChain = nullptr; }
    if (m_d3dContext)   { m_d3dContext->Release();   m_d3dContext = nullptr; }
    if (m_dxgiDevice)   { m_dxgiDevice->Release();   m_dxgiDevice = nullptr; }
    if (m_d3dDevice)    { m_d3dDevice->Release();    m_d3dDevice = nullptr; }
    m_inited = false;
}

// ============================================================
//  窗口位置：附着任务栏
// ============================================================

void OverlayWindow::UpdatePosition() {
    HWND hTaskbar = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!hTaskbar) return;

    RECT rc;
    if (!GetWindowRect(hTaskbar, &rc)) return;

    m_taskbarHeight = rc.bottom - rc.top;

    // 动态宽度：根据启用的显示项估算内容宽度
    float fontSizePx = m_config.fontSize * m_dpi / 96.0f;
    float charWidth = fontSizePx * 0.62f;   // 等宽字体近似字符宽度
    float padX = 8.0f * m_dpi / 96.0f;
    float itemGap = padX * 1.2f;
    float dotR = fontSizePx * 0.18f;
    float dotGap = padX * 0.3f;
    float sepGap = padX * 0.8f;

    float contentW = 0.0f;
    int groups = 0;

    // CPU 组
    bool hasCpu = m_config.showCpuTemp || m_config.showCpuUsage;
    if (hasCpu) {
        if (m_config.showIndicatorDots) contentW += dotR * 2 + dotGap;
        if (m_config.showCpuTemp)  contentW += 6 * charWidth;  // "CPU047°"
        if (m_config.showCpuUsage) contentW += 4 * charWidth;  // "078%"
        contentW += itemGap;
        groups++;
    }

    // GPU 组
    bool hasGpu = m_config.showGpuTemp || m_config.showGpuUsage;
    if (hasGpu) {
        if (m_config.showIndicatorDots) contentW += dotR * 2 + dotGap;
        if (m_config.showGpuTemp)  contentW += 6 * charWidth;
        if (m_config.showGpuUsage) contentW += 4 * charWidth;
        contentW += itemGap;
        groups++;
    }

    // 内存
    if (m_config.showMemUsage) {
        if (m_config.showIndicatorDots) contentW += dotR * 2 + dotGap;
        contentW += 7 * charWidth;  // "RAM078%"
        contentW += itemGap;
        groups++;
    }

    // 网络
    bool hasNet = m_config.showNetUp || m_config.showNetDown;
    if (hasNet) {
        if (m_config.showIndicatorDots) contentW += dotR * 2 + dotGap;
        if (m_config.showNetUp)   contentW += 6 * charWidth;  // "↑1.2K "
        if (m_config.showNetDown) contentW += 6 * charWidth;  // "↓1.2K "
        contentW += itemGap;
        groups++;
    }

    // 分隔符宽度
    if (m_config.showSeparator && groups > 1) {
        contentW += (groups - 1) * sepGap;
    }

    int width = static_cast<int>(contentW + padX * 2);
    if (width < 20) width = 20;
    int height = m_taskbarHeight;

    // 固定在最左边，紧贴任务栏左边缘
    int x = rc.left;
    int y = rc.top;

    SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, width, height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void OverlayWindow::OnDpiChanged() {
    UINT newDpi = GetDpiForWindow(m_hwnd);
    if (newDpi == m_dpi) return;
    m_dpi = newDpi;
    // 重建字体（仅释放字体，不重建设备）
    if (m_textFormat) { m_textFormat->Release(); m_textFormat = nullptr; }
    if (m_monoFormat) { m_monoFormat->Release(); m_monoFormat = nullptr; }
    CreateFontsAndBrushes();
}

// ============================================================
//  渲染
// ============================================================

void OverlayWindow::Update() {
    if (!m_inited) return;
    if (!m_visible) return;  // 隐藏时跳过渲染
    Render();
}

void OverlayWindow::Show(bool visible) {
    if (m_visible == visible) return;
    m_visible = visible;
    if (m_hwnd) {
        if (visible) {
            ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
            BringToTop();   // 从隐藏恢复后重新置顶
        } else {
            ShowWindow(m_hwnd, SW_HIDE);
        }
    }
}

void OverlayWindow::SetConfig(const OverlayConfig& config) {
    m_config = config;
    // 字号变化需重建字体
    if (m_textFormat) { m_textFormat->Release(); m_textFormat = nullptr; }
    if (m_monoFormat) { m_monoFormat->Release(); m_monoFormat = nullptr; }
    // 仅更新画刷颜色（不重建设备）
    if (m_brushText && m_d2dContext) {
        m_brushText->SetColor(UintToColorF(m_config.textColor));
    }
    if (m_brushAccent && m_d2dContext) {
        m_brushAccent->SetColor(UintToColorF(m_config.accentColor));
    }
    if (m_brushNetUp) {
        m_brushNetUp->SetColor(UintToColorF(m_config.netUpColor));
    }
    if (m_brushNetDown) {
        m_brushNetDown->SetColor(UintToColorF(m_config.netDownColor));
    }
    CreateFontsAndBrushes();
    UpdatePosition();
    Render();
}

void OverlayWindow::Render() {
    if (!m_swapChain || !m_d2dContext) return;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    UINT width = rc.right - rc.left;
    UINT height = rc.bottom - rc.top;
    if (width == 0 || height == 0) return;

    // 调整 swapchain 大小（仅当尺寸变化时）
    DXGI_SWAP_CHAIN_DESC1 desc;
    m_swapChain->GetDesc1(&desc);
    if (desc.Width != width || desc.Height != height) {
        m_d2dContext->SetTarget(nullptr);
        // 释放旧 bitmap
        if (m_d2dBitmap) { m_d2dBitmap->Release(); m_d2dBitmap = nullptr; }
        HRESULT hr = m_swapChain->ResizeBuffers(0, width, height, desc.Format, desc.Flags);
        if (FAILED(hr)) return;
    }

    // 复用 bitmap（仅在未创建或设备丢失时重建）
    if (!m_d2dBitmap) {
        IDXGISurface* surface = nullptr;
        if (m_swapChain->GetBuffer(0, __uuidof(IDXGISurface),
            reinterpret_cast<void**>(&surface)) != S_OK) return;

        D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                D2D1_ALPHA_MODE_PREMULTIPLIED));
        HRESULT hrBmp = m_d2dContext->CreateBitmapFromDxgiSurface(
            surface, bp, &m_d2dBitmap);
        surface->Release();
        if (FAILED(hrBmp) || !m_d2dBitmap) return;
    }

    m_d2dContext->SetTarget(m_d2dBitmap);

    m_d2dContext->BeginDraw();
    m_d2dContext->Clear(D2D1::ColorF(0, 0, 0, 0));

    SystemMetrics m = Monitor::Instance().GetSnapshot();
    DrawMetrics(m_d2dContext, m);

    HRESULT hrEnd = m_d2dContext->EndDraw();
    if (hrEnd == D2DERR_RECREATE_TARGET || hrEnd == D2DERR_WRONG_STATE) {
        // 设备丢失（TDR/驱动崩溃）：完整重建渲染管线
        ReleaseAll();
        if (InitD3D() && InitD2D() && InitComposition()) {
            UpdatePosition();
        }
        return;
    }

    m_swapChain->Present(1, 0);
    if (m_dcompDevice) m_dcompDevice->Commit();
}

void OverlayWindow::DrawMetrics(ID2D1DeviceContext* ctx, const SystemMetrics& metrics) {
    float fontSizePx = m_config.fontSize * m_dpi / 96.0f;
    float padX = 8.0f * m_dpi / 96.0f;

    RECT rc;
    GetClientRect(m_hwnd, &rc);
    float y = (rc.top + rc.bottom) / 2.0f - fontSizePx / 2.0f;
    float centerY = (rc.top + rc.bottom) / 2.0f;

    // 左对齐布局：从左往右画
    float x = padX;
    float itemGap = padX * 1.2f;
    float labelGap = padX * 0.15f;
    float dotR = fontSizePx * 0.18f;
    float dotGap = padX * 0.3f;
    float sepGap = padX * 0.8f;

    // 指示点颜色：蓝=CPU / 绿=GPU / 橙=RAM / 紫=NET
    D2D1_COLOR_F dotCpu = D2D1::ColorF(0.29f, 0.56f, 0.89f, 1.0f);
    D2D1_COLOR_F dotGpu = D2D1::ColorF(0.0f, 0.78f, 0.33f, 1.0f);
    D2D1_COLOR_F dotRam = D2D1::ColorF(1.0f, 0.55f, 0.0f, 1.0f);
    D2D1_COLOR_F dotNet = D2D1::ColorF(0.67f, 0.29f, 0.74f, 1.0f);

    bool prevGroupDrawn = false;

    // 分隔符 lambda：前一组有内容时才画
    auto drawSep = [&]() {
        if (m_config.showSeparator && prevGroupDrawn) {
            float sepX = x;
            DrawSeparator(ctx, sepX, centerY - fontSizePx * 0.35f,
                          centerY + fontSizePx * 0.35f);
            x += sepGap;
        }
    };

    // 温度画刷 lambda：色阶开启时返回临时变色的 m_brushAccent
    auto getTempBrush = [&](float temp) -> ID2D1Brush* {
        if (m_config.tempColorGradient && temp > 0 && m_brushAccent) {
            m_brushAccent->SetColor(TempToColor(temp));
            return m_brushAccent;
        }
        return m_brushText;
    };
    auto restoreAccent = [&]() {
        m_brushAccent->SetColor(UintToColorF(m_config.accentColor));
    };

    // === CPU ===
    bool hasCpu = (m_config.showCpuTemp && metrics.cpuTemp > 0) || m_config.showCpuUsage;
    if (hasCpu) {
        drawSep();
        if (m_config.showIndicatorDots) {
            DrawDot(ctx, x + dotR, centerY, dotR, dotCpu);
            x += dotR * 2 + dotGap;
        }
        if (m_config.showCpuTemp && metrics.cpuTemp > 0) {
            ID2D1Brush* b = getTempBrush(metrics.cpuTemp);
            std::wstring txt = L"CPU" + FormatTemp(metrics.cpuTemp);
            DrawTextLeft(ctx, txt, b, m_monoFormat, fontSizePx, labelGap, x, y);
            restoreAccent();
        }
        if (m_config.showCpuUsage) {
            std::wstring txt;
            if (m_config.showCpuTemp && metrics.cpuTemp > 0)
                txt = FormatPercent(metrics.cpuUsage);
            else
                txt = L"CPU" + FormatPercent(metrics.cpuUsage);
            DrawTextLeft(ctx, txt, m_brushText, m_monoFormat, fontSizePx, labelGap, x, y);
        }
        x += itemGap;
        prevGroupDrawn = true;
    }

    // === GPU ===
    bool hasGpu = m_config.showGpuTemp || m_config.showGpuUsage;
    if (hasGpu) {
        drawSep();
        if (m_config.showIndicatorDots) {
            DrawDot(ctx, x + dotR, centerY, dotR, dotGpu);
            x += dotR * 2 + dotGap;
        }
        if (m_config.showGpuTemp) {
            ID2D1Brush* b = getTempBrush(metrics.gpuTemp);
            std::wstring txt = L"GPU" + FormatTemp(metrics.gpuTemp);
            DrawTextLeft(ctx, txt, b, m_monoFormat, fontSizePx, labelGap, x, y);
            restoreAccent();
        }
        if (m_config.showGpuUsage) {
            std::wstring txt = FormatPercent(metrics.gpuUsage);
            DrawTextLeft(ctx, txt, m_brushText, m_monoFormat, fontSizePx, labelGap, x, y);
        }
        x += itemGap;
        prevGroupDrawn = true;
    }

    // === 内存 ===
    if (m_config.showMemUsage) {
        drawSep();
        if (m_config.showIndicatorDots) {
            DrawDot(ctx, x + dotR, centerY, dotR, dotRam);
            x += dotR * 2 + dotGap;
        }
        std::wstring txt = L"RAM" + FormatPercent(metrics.memUsage);
        DrawTextLeft(ctx, txt, m_brushText, m_monoFormat, fontSizePx, labelGap, x, y);
        x += itemGap;
        prevGroupDrawn = true;
    }

    // === 网络（↑↓ 紧贴数值，异色区分方向）===
    bool hasNet = m_config.showNetUp || m_config.showNetDown;
    if (hasNet) {
        drawSep();
        if (m_config.showIndicatorDots) {
            DrawDot(ctx, x + dotR, centerY, dotR, dotNet);
            x += dotR * 2 + dotGap;
        }
        ID2D1Brush* upBrush   = m_config.netColorSplit ? m_brushNetUp   : m_brushAccent;
        ID2D1Brush* downBrush = m_config.netColorSplit ? m_brushNetDown : m_brushAccent;
        if (m_config.showNetUp) {
            std::wstring txt = L"\u2191" + FormatRate(metrics.netUpload);
            DrawTextLeft(ctx, txt, upBrush, m_monoFormat, fontSizePx, 0, x, y);
        }
        if (m_config.showNetDown) {
            std::wstring txt = L"\u2193" + FormatRate(metrics.netDownload);
            DrawTextLeft(ctx, txt, downBrush, m_monoFormat, fontSizePx, 0, x, y);
        }
        x += itemGap;
        prevGroupDrawn = true;
    }
}

void OverlayWindow::DrawTextLeft(ID2D1DeviceContext* ctx, const std::wstring& text,
    ID2D1Brush* brush, IDWriteTextFormat* fmt,
    float fontSizePx, float padX, float& x, float y) {
    if (text.empty()) return;
    IDWriteTextLayout* layout = nullptr;
    m_dwriteFactory->CreateTextLayout(text.c_str(),
        static_cast<UINT32>(text.size()), fmt,
        1000.0f, fontSizePx * 2, &layout);
    if (!layout) return;
    DWRITE_TEXT_METRICS tm;
    layout->GetMetrics(&tm);
    ctx->DrawTextLayout(D2D1::Point2F(x, y), layout, brush,
        D2D1_DRAW_TEXT_OPTIONS_NONE);
    layout->Release();
    x += tm.width + padX * 0.3f;
}

// ============================================================
//  辅助方法
// ============================================================

D2D1_COLOR_F OverlayWindow::UintToColorF(uint32_t rgba) {
    return D2D1::ColorF(
        ((rgba >> 16) & 0xFF) / 255.0f,
        ((rgba >> 8) & 0xFF) / 255.0f,
        (rgba & 0xFF) / 255.0f,
        ((rgba >> 24) & 0xFF) / 255.0f);
}

D2D1_COLOR_F OverlayWindow::TempToColor(float temp) {
    // 温度色阶：< 50°C 绿 → 50-70°C 黄 → 70-85°C 橙 → > 85°C 红
    if (temp < 0) return D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f);
    if (temp < 50.0f) {
        return D2D1::ColorF(0.0f, 1.0f, 0.4f, 1.0f);       // 绿
    } else if (temp < 70.0f) {
        float t = (temp - 50.0f) / 20.0f;                    // 绿→黄
        return D2D1::ColorF(t, 1.0f, 0.4f * (1.0f - t), 1.0f);
    } else if (temp < 85.0f) {
        float t = (temp - 70.0f) / 15.0f;                    // 黄→橙
        return D2D1::ColorF(1.0f, 1.0f - t * 0.5f, 0.0f, 1.0f);
    } else {
        return D2D1::ColorF(1.0f, 0.2f, 0.0f, 1.0f);        // 红
    }
}

void OverlayWindow::DrawDot(ID2D1DeviceContext* ctx, float cx, float cy,
                            float radius, D2D1_COLOR_F color) {
    // 借用 m_brushAccent 临时换色画点，画完恢复
    if (!m_brushAccent) return;
    D2D1_COLOR_F orig = m_brushAccent->GetColor();
    m_brushAccent->SetColor(color);
    D2D1_ELLIPSE ellipse = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
    ctx->FillEllipse(ellipse, m_brushAccent);
    m_brushAccent->SetColor(orig);
}

void OverlayWindow::DrawSeparator(ID2D1DeviceContext* ctx, float x,
                                   float top, float bottom) {
    if (!m_brushSeparator) return;
    ctx->DrawLine(D2D1::Point2F(x, top), D2D1::Point2F(x, bottom),
        m_brushSeparator, 1.0f, nullptr);
}

// ============================================================
//  格式化工具
// ============================================================

std::wstring OverlayWindow::FormatRate(uint64_t bytesPerSec) {
    // 固定 6 字符总宽度（数字+单位），彻底防跳位
    // 格式：XX.XK / XXXK / XX.XM / XXXM / XX.XG
    // 无数据时显示 ----B
    wchar_t buf[16];
    if (bytesPerSec < 1024ULL) {
        // 0~1023 B/s：显示为 XXXB（不足前补空格）
        swprintf_s(buf, L"%4lluB", static_cast<unsigned long long>(bytesPerSec));
    } else if (bytesPerSec < 1024ULL * 1024) {
        double v = bytesPerSec / 1024.0;
        if (v < 100) swprintf_s(buf, L"%4.1fK", v);   // XX.XK
        else         swprintf_s(buf, L"%4.0fK", v);   //  XXXK
    } else if (bytesPerSec < 1024ULL * 1024 * 1024) {
        double v = bytesPerSec / (1024.0 * 1024);
        if (v < 100) swprintf_s(buf, L"%4.1fM", v);
        else         swprintf_s(buf, L"%4.0fM", v);
    } else {
        double v = bytesPerSec / (1024.0 * 1024 * 1024);
        if (v < 100) swprintf_s(buf, L"%4.1fG", v);
        else         swprintf_s(buf, L"%4.0fG", v);
    }
    return buf;
}

std::wstring OverlayWindow::FormatTemp(float temp) {
    // 固定 3 字符宽度：047° / --°
    if (temp < 0) return L"  --\u00B0";
    wchar_t buf[8];
    swprintf_s(buf, L"%3d\u00B0", static_cast<int>(temp));  // 047°
    return buf;
}

std::wstring OverlayWindow::FormatPercent(float pct) {
    // 固定 3 字符宽度：078% / --%
    if (pct < 0) return L"  --%";
    wchar_t buf[8];
    swprintf_s(buf, L"%3d%%", static_cast<int>(pct));  // 078%
    return buf;
}

// ============================================================
//  窗口过程
// ============================================================

LRESULT CALLBACK OverlayWindow::WndProcStatic(HWND hwnd, UINT msg,
    WPARAM wp, LPARAM lp) {
    OverlayWindow* self = reinterpret_cast<OverlayWindow*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (self) return self->WndProc(msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT OverlayWindow::WndProc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_DPICHANGED:
            OnDpiChanged();
            UpdatePosition();
            return 0;
        case WM_DISPLAYCHANGE:
            UpdatePosition();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return m_hwnd ? DefWindowProcW(m_hwnd, msg, wp, lp) : 0;
    }
}
