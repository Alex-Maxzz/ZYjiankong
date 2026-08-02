// main.cpp - 程序入口（集成开机启动、全屏隐藏、显示设置、配置持久化）
#include "pch.h"
#include "Monitor.h"
#include "OverlayWindow.h"
#include "AppConfig.h"
#include "FullscreenDetect.h"
#include "SettingsDialog.h"

#include <shellapi.h>
#include <commctrl.h>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

static const wchar_t* kAppTitle       = L"TaskbarStudio";
static const UINT     kTaskbarIconMsg = WM_APP + 1;
static const UINT     kTimerRefresh   = 1001;  // 1s 刷新数据
static const UINT     kTimerFsCheck   = 1002;  // 500ms 检查全屏
static const UINT     kTrayId         = 1000;

// 菜单命令 ID
enum : UINT {
    IDM_EXIT              = 1001,
    IDM_SETTINGS          = 1002,
    IDM_STARTUP           = 2001,
    IDM_HIDE_FULLSCREEN   = 2002,
    IDM_TOGGLE_CPU_TEMP   = 2010,
    IDM_TOGGLE_CPU        = 2011,
    IDM_TOGGLE_GPU_TEMP   = 2012,
    IDM_TOGGLE_GPU_USAGE  = 2013,
    IDM_TOGGLE_MEM        = 2014,
    IDM_TOGGLE_NET_UP     = 2015,
    IDM_TOGGLE_NET_DOWN   = 2016,
    IDM_FONT_SMALL        = 2021,
    IDM_FONT_MEDIUM       = 2022,
    IDM_FONT_LARGE        = 2023,
    IDM_COLOR_WHITE       = 2031,
    IDM_COLOR_YELLOW      = 2032,
    IDM_COLOR_CYAN        = 2033,
    IDM_COLOR_GREEN       = 2034,
    IDM_TOGGLE_DOTS       = 2041,  // 彩色指示点
    IDM_TOGGLE_TEMP_COLOR = 2042,  // 温度色阶
    IDM_TOGGLE_NET_SPLIT  = 2043,  // 网络异色
    IDM_TOGGLE_SEPARATOR  = 2044,  // 项间分隔符
};

static HMENU g_hMenu = nullptr;
static HICON g_hAppIcon = nullptr;  // 应用图标（从资源加载）

// ===================== 托盘图标 =====================

static bool AddTrayIcon(HWND hwnd) {
    // 加载自定义图标（多尺寸，托盘自动选 16x16）
    if (!g_hAppIcon) {
        g_hAppIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        if (!g_hAppIcon) {
            g_hAppIcon = LoadIcon(nullptr, IDI_APPLICATION);
        }
    }

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd;
    nid.uID    = kTrayId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTaskbarIconMsg;
    nid.hIcon  = g_hAppIcon;
    wcscpy_s(nid.szTip, kAppTitle);
    return Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
}

static void RemoveTrayIcon(HWND hwnd) {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = hwnd;
    nid.uID    = kTrayId;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

// ===================== 应用配置到悬浮窗 =====================

static void ApplyConfigToOverlay() {
    const DisplayConfig& dc = AppConfig::Instance().Get();
    OverlayConfig oc{};
    oc.showCpuTemp  = dc.showCpuTemp;
    oc.showCpuUsage = dc.showCpuUsage;
    oc.showGpuTemp  = dc.showGpuTemp;
    oc.showGpuUsage = dc.showGpuUsage;
    oc.showMemUsage = dc.showMemUsage;
    oc.showNetUp    = dc.showNetUp;
    oc.showNetDown  = dc.showNetDown;
    oc.fontSize     = dc.fontSize;
    oc.textColor    = dc.textColor;
    oc.accentColor  = dc.accentColor;
    oc.showIndicatorDots = dc.showIndicatorDots;
    oc.tempColorGradient = dc.tempColorGradient;
    oc.netColorSplit     = dc.netColorSplit;
    oc.showSeparator     = dc.showSeparator;
    oc.netUpColor        = dc.netUpColor;
    oc.netDownColor      = dc.netDownColor;
    oc.fontFamily        = dc.fontFamily;
    oc.tempLowThreshold  = dc.tempLowThreshold;
    oc.tempHighThreshold = dc.tempHighThreshold;
    oc.spacingScale      = dc.spacingScale;
    oc.overlayOpacity    = dc.overlayOpacity;
    oc.alignRight   = false;
    OverlayWindow::Instance().SetConfig(oc);
}

// ===================== 托盘菜单 =====================

static void ShowContextMenu(HWND hwnd) {
    if (!g_hMenu) g_hMenu = CreatePopupMenu();
    while (GetMenuItemCount(g_hMenu) > 0)
        DeleteMenu(g_hMenu, 0, MF_BYPOSITION);  // DeleteMenu 会销毁子菜单，RemoveMenu 不会

    const DisplayConfig& dc = AppConfig::Instance().Get();

    // 开机启动
    AppendMenuW(g_hMenu, MF_STRING | (dc.runOnStartup ? MF_CHECKED : 0),
        IDM_STARTUP, L"开机启动");

    // 全屏隐藏
    AppendMenuW(g_hMenu, MF_STRING | (dc.hideOnFullscreen ? MF_CHECKED : 0),
        IDM_HIDE_FULLSCREEN, L"全屏时隐藏");

    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);

    // 显示项
    HMENU hShow = CreatePopupMenu();
    AppendMenuW(hShow, MF_STRING | (dc.showCpuTemp ? MF_CHECKED : 0),
        IDM_TOGGLE_CPU_TEMP, L"CPU 温度");
    AppendMenuW(hShow, MF_STRING | (dc.showCpuUsage ? MF_CHECKED : 0),
        IDM_TOGGLE_CPU, L"CPU 占用率");
    AppendMenuW(hShow, MF_STRING | (dc.showGpuTemp ? MF_CHECKED : 0),
        IDM_TOGGLE_GPU_TEMP, L"GPU 温度");
    AppendMenuW(hShow, MF_STRING | (dc.showGpuUsage ? MF_CHECKED : 0),
        IDM_TOGGLE_GPU_USAGE, L"GPU 占用率");
    AppendMenuW(hShow, MF_STRING | (dc.showMemUsage ? MF_CHECKED : 0),
        IDM_TOGGLE_MEM, L"内存占用");
    AppendMenuW(hShow, MF_STRING | (dc.showNetUp ? MF_CHECKED : 0),
        IDM_TOGGLE_NET_UP, L"网络上行");
    AppendMenuW(hShow, MF_STRING | (dc.showNetDown ? MF_CHECKED : 0),
        IDM_TOGGLE_NET_DOWN, L"网络下行");
    AppendMenuW(g_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hShow), L"显示项");

    // 字号
    HMENU hFont = CreatePopupMenu();
    AppendMenuW(hFont, MF_STRING | (dc.fontSize == 10.0f ? MF_CHECKED : 0),
        IDM_FONT_SMALL, L"小 (10)");
    AppendMenuW(hFont, MF_STRING | (dc.fontSize == 12.0f ? MF_CHECKED : 0),
        IDM_FONT_MEDIUM, L"中 (12)");
    AppendMenuW(hFont, MF_STRING | (dc.fontSize == 14.0f ? MF_CHECKED : 0),
        IDM_FONT_LARGE, L"大 (14)");
    AppendMenuW(g_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hFont), L"字号");

    // 颜色
    HMENU hColor = CreatePopupMenu();
    AppendMenuW(hColor, MF_STRING | (dc.textColor == 0xFFFFFFFF ? MF_CHECKED : 0),
        IDM_COLOR_WHITE, L"白色");
    AppendMenuW(hColor, MF_STRING | (dc.textColor == 0xFFFFFF00 ? MF_CHECKED : 0),
        IDM_COLOR_YELLOW, L"黄色");
    AppendMenuW(hColor, MF_STRING | (dc.textColor == 0xFF00FFFF ? MF_CHECKED : 0),
        IDM_COLOR_CYAN, L"青色");
    AppendMenuW(hColor, MF_STRING | (dc.textColor == 0xFF00FF66 ? MF_CHECKED : 0),
        IDM_COLOR_GREEN, L"绿色");
    AppendMenuW(g_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hColor), L"文字颜色");

    // 外观增强
    HMENU hVisual = CreatePopupMenu();
    AppendMenuW(hVisual, MF_STRING | (dc.showIndicatorDots ? MF_CHECKED : 0),
        IDM_TOGGLE_DOTS, L"彩色指示点");
    AppendMenuW(hVisual, MF_STRING | (dc.tempColorGradient ? MF_CHECKED : 0),
        IDM_TOGGLE_TEMP_COLOR, L"温度色阶");
    AppendMenuW(hVisual, MF_STRING | (dc.netColorSplit ? MF_CHECKED : 0),
        IDM_TOGGLE_NET_SPLIT, L"网络上下行异色");
    AppendMenuW(hVisual, MF_STRING | (dc.showSeparator ? MF_CHECKED : 0),
        IDM_TOGGLE_SEPARATOR, L"项间分隔符");
    AppendMenuW(g_hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hVisual), L"外观增强");

    AppendMenuW(g_hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(g_hMenu, MF_STRING, IDM_SETTINGS, L"设置...");
    AppendMenuW(g_hMenu, MF_STRING, IDM_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(g_hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
        pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);  // KB135788: 确保菜单正确关闭
}

// ===================== 切换配置项 =====================

static void ToggleShowFlag(UINT id) {
    DisplayConfig dc = AppConfig::Instance().Get();
    switch (id) {
        case IDM_TOGGLE_CPU_TEMP:  dc.showCpuTemp     = !dc.showCpuTemp;     break;
        case IDM_TOGGLE_CPU:       dc.showCpuUsage    = !dc.showCpuUsage;    break;
        case IDM_TOGGLE_GPU_TEMP:  dc.showGpuTemp     = !dc.showGpuTemp;     break;
        case IDM_TOGGLE_GPU_USAGE: dc.showGpuUsage    = !dc.showGpuUsage;    break;
        case IDM_TOGGLE_MEM:       dc.showMemUsage    = !dc.showMemUsage;    break;
        case IDM_TOGGLE_NET_UP:    dc.showNetUp       = !dc.showNetUp;       break;
        case IDM_TOGGLE_NET_DOWN:  dc.showNetDown     = !dc.showNetDown;     break;
    }
    AppConfig::Instance().Set(dc);
    AppConfig::Instance().Save();
    ApplyConfigToOverlay();
}

static void SetFontSize(float size) {
    DisplayConfig dc = AppConfig::Instance().Get();
    dc.fontSize = size;
    AppConfig::Instance().Set(dc);
    AppConfig::Instance().Save();
    ApplyConfigToOverlay();
}

static void SetTextColor(uint32_t color) {
    DisplayConfig dc = AppConfig::Instance().Get();
    dc.textColor = color;
    AppConfig::Instance().Set(dc);
    AppConfig::Instance().Save();
    ApplyConfigToOverlay();
}

// ===================== 窗口过程 =====================

static LRESULT CALLBACK HiddenWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case kTaskbarIconMsg:
            if (lp == WM_RBUTTONUP) ShowContextMenu(hwnd);
            return 0;
        case WM_COMMAND: {
            UINT id = LOWORD(wp);
            switch (id) {
                case IDM_SETTINGS:
                    SettingsDialog::Show(hwnd);
                    return 0;
                case IDM_EXIT:
                    PostQuitMessage(0);
                    return 0;
                case IDM_STARTUP: {
                    DisplayConfig dc = AppConfig::Instance().Get();
                    dc.runOnStartup = !dc.runOnStartup;
                    AppConfig::Instance().Set(dc);
                    AppConfig::Instance().Save();
                    return 0;
                }
                case IDM_HIDE_FULLSCREEN: {
                    DisplayConfig dc = AppConfig::Instance().Get();
                    dc.hideOnFullscreen = !dc.hideOnFullscreen;
                    AppConfig::Instance().Set(dc);
                    AppConfig::Instance().Save();
                    return 0;
                }
                case IDM_FONT_SMALL:  SetFontSize(10.0f); return 0;
                case IDM_FONT_MEDIUM: SetFontSize(12.0f); return 0;
                case IDM_FONT_LARGE:  SetFontSize(14.0f); return 0;
                case IDM_COLOR_WHITE:  SetTextColor(0xFFFFFFFF); return 0;
                case IDM_COLOR_YELLOW: SetTextColor(0xFFFFFF00); return 0;
                case IDM_COLOR_CYAN:   SetTextColor(0xFF00FFFF); return 0;
                case IDM_COLOR_GREEN:  SetTextColor(0xFF00FF66); return 0;
                case IDM_TOGGLE_DOTS: {
                    DisplayConfig dc = AppConfig::Instance().Get();
                    dc.showIndicatorDots = !dc.showIndicatorDots;
                    AppConfig::Instance().Set(dc);
                    AppConfig::Instance().Save();
                    ApplyConfigToOverlay();
                    return 0;
                }
                case IDM_TOGGLE_TEMP_COLOR: {
                    DisplayConfig dc = AppConfig::Instance().Get();
                    dc.tempColorGradient = !dc.tempColorGradient;
                    AppConfig::Instance().Set(dc);
                    AppConfig::Instance().Save();
                    ApplyConfigToOverlay();
                    return 0;
                }
                case IDM_TOGGLE_NET_SPLIT: {
                    DisplayConfig dc = AppConfig::Instance().Get();
                    dc.netColorSplit = !dc.netColorSplit;
                    AppConfig::Instance().Set(dc);
                    AppConfig::Instance().Save();
                    ApplyConfigToOverlay();
                    return 0;
                }
                case IDM_TOGGLE_SEPARATOR: {
                    DisplayConfig dc = AppConfig::Instance().Get();
                    dc.showSeparator = !dc.showSeparator;
                    AppConfig::Instance().Set(dc);
                    AppConfig::Instance().Save();
                    ApplyConfigToOverlay();
                    return 0;
                }
                default:
                    if (id >= IDM_TOGGLE_CPU_TEMP && id <= IDM_TOGGLE_NET_DOWN) {
                        ToggleShowFlag(id);
                        return 0;
                    }
                    break;  // 未知命令交给 DefWindowProcW
            }
        }
        case WM_TIMER:
            if (wp == kTimerRefresh) {
                // 周期性重新置顶，防止被 TranslucentTB 等同类 TOPMOST 窗口压在下面
                // SetWindowPos HWND_TOPMOST 在已是 TOPMOST 时是近乎零开销的 no-op
                OverlayWindow::Instance().BringToTop();
                OverlayWindow::Instance().Update();
            } else if (wp == kTimerFsCheck) {
                // 全屏检测
                const DisplayConfig& dc = AppConfig::Instance().Get();
                if (dc.hideOnFullscreen) {
                    bool fs = FullscreenDetect::Instance().IsFullscreenWindowActive();
                    OverlayWindow::Instance().Show(!fs);
                } else {
                    OverlayWindow::Instance().Show(true);
                }
            }
            return 0;
        case WM_DISPLAYCHANGE:
            OverlayWindow::Instance().Update();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ===================== 主入口 =====================

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR cmdLine, int) {
    // 单实例保护：已运行则直接退出
    HANDLE hSingle = CreateMutexW(nullptr, TRUE, L"TaskbarStudio_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (hSingle) CloseHandle(hSingle);
        return 0;
    }

    // --silent 参数：开机静默启动
    bool silent = (cmdLine && wcsstr(cmdLine, L"--silent") != nullptr);

    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (FAILED(hr)) return 1;

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    // 加载配置
    AppConfig::Instance().Load();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = HiddenWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"TaskbarStudioMain";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(0, L"TaskbarStudioMain", kAppTitle,
        WS_OVERLAPPED, 0, 0, 1, 1, nullptr, nullptr, hInst, nullptr);
    if (!hwnd) { CoUninitialize(); return 1; }

    // 启动硬件采集
    if (!Monitor::Instance().Start()) {
        if (!silent) MessageBoxW(nullptr, L"硬件采集启动失败", kAppTitle, MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // 创建悬浮数据条（使用持久化配置）
    const DisplayConfig& dc = AppConfig::Instance().Get();
    OverlayConfig oc{};
    oc.showCpuTemp  = dc.showCpuTemp;
    oc.showCpuUsage = dc.showCpuUsage;
    oc.showGpuTemp  = dc.showGpuTemp;
    oc.showGpuUsage = dc.showGpuUsage;
    oc.showMemUsage = dc.showMemUsage;
    oc.showNetUp    = dc.showNetUp;
    oc.showNetDown  = dc.showNetDown;
    oc.fontSize     = dc.fontSize;
    oc.textColor    = dc.textColor;
    oc.accentColor  = dc.accentColor;
    oc.showIndicatorDots = dc.showIndicatorDots;
    oc.tempColorGradient = dc.tempColorGradient;
    oc.netColorSplit     = dc.netColorSplit;
    oc.showSeparator     = dc.showSeparator;
    oc.netUpColor        = dc.netUpColor;
    oc.netDownColor      = dc.netDownColor;
    oc.fontFamily        = dc.fontFamily;
    oc.tempLowThreshold  = dc.tempLowThreshold;
    oc.tempHighThreshold = dc.tempHighThreshold;
    oc.spacingScale      = dc.spacingScale;
    oc.overlayOpacity    = dc.overlayOpacity;
    oc.alignRight   = false;
    if (!OverlayWindow::Instance().Create(oc)) {
        if (!silent) MessageBoxW(nullptr, L"悬浮窗创建失败", kAppTitle, MB_ICONWARNING);
    }

    AddTrayIcon(hwnd);
    SetTimer(hwnd, kTimerRefresh, 1000, nullptr);
    SetTimer(hwnd, kTimerFsCheck, 500, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // 清理
    KillTimer(hwnd, kTimerRefresh);
    KillTimer(hwnd, kTimerFsCheck);
    RemoveTrayIcon(hwnd);
    OverlayWindow::Instance().Destroy();
    Monitor::Instance().Stop();
    if (g_hMenu) { DestroyMenu(g_hMenu); g_hMenu = nullptr; }
    DestroyWindow(hwnd);
    UnregisterClassW(L"TaskbarStudioMain", hInst);
    CoUninitialize();
    if (hSingle) CloseHandle(hSingle);
    return static_cast<int>(msg.wParam);
}
