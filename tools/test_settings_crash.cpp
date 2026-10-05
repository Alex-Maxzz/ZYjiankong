// test_settings_crash.cpp - 验收工具：模拟点击设置窗口各 Tab，验证不再崩溃
// 做法：枚举 TSSettingsV2 窗口 → 找到驱动页 Tab 区域 → 发WM_LBUTTONUP
// 用法：test_settings_crash.exe  （需先手动打开设置窗口）
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

// 与 SettingsDialog.cpp 中 BASE_W/TITLE_H/TAB_H 保持一致
static const int BASE_W = 440, TITLE_H = 38, TAB_H = 38, TAB_N = 6;

// 设置窗口可能被DPI 缩放，需按窗口实际客户区宽度换算
struct TabHit { int index; long x; long y; };

int wmain() {
    FILE* out = nullptr;
    wchar_t path[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, path);
    wcscat_s(path, L"settings_crash_test.txt");
    _wfopen_s(&out, path, L"w, ccs=UTF-8");
    if (!out) return 3;
    #define P(...) do { fwprintf(out, __VA_ARGS__); fflush(out); } while (0)

    // 找设置窗口
    HWND hwnd = FindWindowW(L"TSSettingsV2", nullptr);
    if (!hwnd) {
        P(L"[X] 未找到设置窗口 TSSettingsV2\n");
        P(L"    请先运行 TaskbarStudio.exe 并从托盘菜单打开设置，然后重跑本工具。\n");
        fclose(out);
        return 2;
    }

    RECT rc;
    GetClientRect(hwnd, &rc);
    const long cw = rc.right - rc.left;
    const long ch = rc.bottom - rc.top;
    const double scale = (double)cw / BASE_W;   // DPI 缩放比
    P(L"设置窗口: hwnd=%p  客户区=%ldx%ld  scale=%.3f\n", hwnd, cw, ch, scale);
    P(L"线程: %u\n", GetCurrentThreadId());

    // 依次点击 6 个 Tab，每个之后检查窗口是否还活着
    const float tw = (float)(BASE_W - 24.f) / TAB_N;
    int alive = 0;
    for (int i = 0; i < TAB_N; ++i) {
        const long cx = (long)((12 + i * tw + tw / 2) * scale);
        const long cy = (long)((TITLE_H + TAB_H / 2) * scale);
        const LPARAM lp = MAKELPARAM(cx, cy);

        SendMessageW(hwnd, WM_MOUSEMOVE, 0, lp);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, lp);

        // 让消息循环处理完
        for (int spin = 0; spin < 40; ++spin) {
            MSG msg;
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
            Sleep(15);
        }

        if (!IsWindow(hwnd)) {
            P(L"[FAIL] 点击第 %d 个 Tab 后窗口已销毁（崩溃）\n", i);
            break;
        }
        P(L"[OK ] Tab %d (y=%ld) 窗口存活\n", i, cy);
        alive++;
    }

    P(L"\n结果: %d/%d 个 Tab 切换后窗口存活\n", alive, TAB_N);
    P(L"%s\n", alive == TAB_N ? L"PASS: 全部 Tab 切换无崩溃"
                               : L"FAIL: 存在崩溃");
    P(L"\n报告: %s\n", path);
    fclose(out);
    return alive == TAB_N ? 0 : 1;
}
