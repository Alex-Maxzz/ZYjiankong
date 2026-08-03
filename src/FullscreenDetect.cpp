// FullscreenDetect.cpp - 全屏检测实现
#include "pch.h"
#include "FullscreenDetect.h"

FullscreenDetect& FullscreenDetect::Instance() {
    static FullscreenDetect inst;
    return inst;
}

bool FullscreenDetect::IsFullscreenWindowActive() const {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;

    // 排除桌面窗口和 Shell
    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0) return false;
    if (wcscmp(cls, L"Shell_TrayWnd") == 0) return false;

    // 检查窗口是否最大化
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    if ((style & WS_MAXIMIZE) == 0) {
        // 即使不是 WS_MAXIMIZE，游戏全屏也可能无此样式
        // 进一步检查窗口矩形是否覆盖显示器
    }

    RECT wndRect;
    if (!GetWindowRect(hwnd, &wndRect)) return false;

    // 获取窗口所在显示器
    HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(hMon, &mi)) return false;

    // 窗口矩形完全覆盖显示器工作区或全屏区
    bool coversMonitor =
        wndRect.left   <= mi.rcMonitor.left  &&
        wndRect.right  >= mi.rcMonitor.right &&
        wndRect.top    <= mi.rcMonitor.top   &&
        wndRect.bottom >= mi.rcMonitor.bottom;

    // 进一步排除：窗口必须是顶级窗口、可见、非工具窗口
    bool isVisible = IsWindowVisible(hwnd);

    // 游戏全屏窗口特征：
    // 1. 无边框（无 caption 和 thickframe）
    // 2. 顶级窗口（父窗口为桌面）
    // 3. 完全覆盖显示器
    bool noBorder = (style & (WS_CAPTION | WS_THICKFRAME)) == 0;
    HWND parent = GetParent(hwnd);
    bool isTopLevel = (parent == nullptr || parent == GetDesktopWindow());

    // 满足全屏条件
    return isVisible && isTopLevel && coversMonitor && noBorder;
}

void FullscreenDetect::GetForegroundMonitorRect(RECT* rc) const {
    HWND hwnd = GetForegroundWindow();
    if (hwnd) {
        HMONITOR hMon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hMon, &mi)) {
            *rc = mi.rcMonitor;
            return;
        }
    }
    // 回退：主显示器
    rc->left = 0; rc->top = 0;
    rc->right = GetSystemMetrics(SM_CXSCREEN);
    rc->bottom = GetSystemMetrics(SM_CYSCREEN);
}
