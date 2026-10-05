// test_settings_crash.cpp - 端到端验收：自动启动设置面板 → 切换/压力点 Tab → 验证不崩溃
//
// 为什么这样写（不是简单 FindWindow）：
//   1) TaskbarStudio 有单实例互斥，已运行时会直接退出，导致旧版工具误报"未找到窗口"。
//      故本工具先结束所有旧实例，再自己拉起一个。
//   2) 只看 IsWindow 不够：进程可能整体崩掉但窗口句柄尚未回收。
//      故同时用 WaitForSingleObject 监控进程句柄，任何一项死亡即判 FAIL。
//   3) 崩溃点在「切到驱动页」，故把驱动页放最后点；并通过反复切换 + 点击重装按钮
//      覆盖渲染路径、后台恢复线程、PurgeDriver 与结果回传路径。
//   4) 本机 WER 已关闭，崩溃无系统日志。被测程序自带 %TEMP%\ts_crash.log，
//      本工具运行前清空、运行后回读，命中即判 FAIL 并打印栈。
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <string>

// 与 SettingsDialog.cpp 保持一致
static const int BASE_W = 440, TITLE_H = 38, TAB_H = 38, TAB_N = 6;
// PageDriver 按钮位置（DIP 设计坐标，由 PageDriver 布局推出）
static const int BTN_LOCAL_X = 220, BTN_LOCAL_Y = 234;
static const int BTN_NET_X   = 220, BTN_NET_Y   = 278;

static FILE* g_out = nullptr;
#define P(...) do { fwprintf(g_out, __VA_ARGS__); fflush(g_out); } while (0)

static void TempFile(wchar_t* out, const wchar_t* leaf) {
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    wcscpy_s(out, MAX_PATH, tmp);
    wcscat_s(out, MAX_PATH, leaf);
}

// 结束所有同名进程（避免单实例互斥把新进程顶掉）
static void KillExisting(const wchar_t* exeName) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    const DWORD self = GetCurrentProcessId();
    int killed = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == self) continue;
            if (_wcsicmp(pe.szExeFile, exeName) == 0) {
                HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                if (h) { TerminateProcess(h, 0); CloseHandle(h); ++killed; }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    if (killed) { P(L"已结束 %d 个旧 %s 实例\n", killed, exeName); Sleep(600); }
}

struct Target {
    HWND  hwnd = nullptr;
    HANDLE proc = nullptr;
    double scale = 1.0;

    bool alive() const {
        if (!hwnd || !IsWindow(hwnd)) return false;
        if (WaitForSingleObject(proc, 0) == WAIT_OBJECT_0) return false;
        return true;
    }
    DWORD exitCode() const { DWORD ec = 0; GetExitCodeProcess(proc, &ec); return ec; }

    // 发一次鼠标点击（坐标 DIP → 客户区物理像素）
    void clickDips(int dx, int dy) const {
        const long cx = (long)(dx * scale), cy = (long)(dy * scale);
        const LPARAM lp = MAKELPARAM(cx, cy);
        SendMessageW(hwnd, WM_MOUSEMOVE, 0, lp);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, lp);
        SendMessageW(hwnd, WM_LBUTTONUP, 0, lp);
    }
    // 等待若干毫秒，中途一旦进程/窗口死亡立刻返回 false
    bool wait(int ms) const {
        for (int t = 0; t < ms; t += 20) {
            if (!alive()) return false;
            Sleep(20);
        }
        return true;
    }
};

static int g_alive = 0, g_total = 0;
static const char* g_failStep = nullptr;

// 单步验收：点一下 → 等一会 → 检查存活
static bool Step(const Target& t, const char* name, int dx, int dy, int waitMs) {
    ++g_total;
    t.clickDips(dx, dy);
    if (!t.wait(waitMs)) {
        g_failStep = name;
        P(L"[FAIL] 步骤「%hs」后崩溃/退出  退出码=0x%08lX\n", name, t.exitCode());
        return false;
    }
    ++g_alive;
    P(L"[OK ] %hs\n", name);
    return true;
}

int wmain() {
    wchar_t report[MAX_PATH] = {};
    TempFile(report, L"settings_crash_test.txt");
    _wfopen_s(&g_out, report, L"w, ccs=UTF-8");
    if (!g_out) return 3;

    wchar_t tracePath[MAX_PATH] = {};
    TempFile(tracePath, L"ts_settings_trace.log");
    DeleteFileW(tracePath);

    wchar_t crashLog[MAX_PATH] = {};
    TempFile(crashLog, L"ts_crash.log");
    DeleteFileW(crashLog);   // 只保留本次运行的崩溃记录

    // 定位被测程序（与本工具同目录）
    wchar_t self[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring dir(self);
    const size_t slash = dir.find_last_of(L"\\/");
    if (slash != std::wstring::npos) dir = dir.substr(0, slash + 1);
    // 优先用非提权的验收构建：正式版清单是 requireAdministrator，
    // 非提权进程 CreateProcess 会直接失败（740）。
    std::wstring exe = dir + L"TaskbarStudio_uitest.exe";
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        exe = dir + L"TaskbarStudio.exe";
    }
    P(L"被测程序: %s\n", exe.c_str());
    if (GetFileAttributesW(exe.c_str()) == INVALID_FILE_ATTRIBUTES) {
        P(L"[X] 找不到 TaskbarStudio_uitest.exe / TaskbarStudio.exe，请先构建。\n");
        fclose(g_out);
        return 3;
    }

    // 单实例互斥名与 exe 名无关，两种进程都要清掉
    KillExisting(L"TaskbarStudio.exe");
    KillExisting(L"TaskbarStudio_uitest.exe");

    std::wstring cmd = L"\"" + exe + L"\" --settings";
    STARTUPINFOW si{}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), &cmd[0], nullptr, nullptr, FALSE, 0,
                        nullptr, dir.c_str(), &si, &pi)) {
        P(L"[X] CreateProcess 失败(错误码 %lu)。若为 740，说明该 EXE 要求提权，"
          L"验收版本需用 asInvoker manifest 构建。\n", GetLastError());
        fclose(g_out);
        return 3;
    }
    P(L"已启动 pid=%lu，等待设置窗口...\n", pi.dwProcessId);

    Target t;
    t.proc = pi.hProcess;
    for (int i = 0; i < 150 && !t.hwnd; ++i) {
        if (WaitForSingleObject(t.proc, 0) == WAIT_OBJECT_0) {
            P(L"[FAIL] 进程在窗口出现前就已退出（--settings 路径本身崩溃）\n");
            CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
            fclose(g_out);
            return 1;
        }
        t.hwnd = FindWindowW(L"TSSettingsV2", nullptr);
        Sleep(100);
    }
    if (!t.hwnd) {
        P(L"[FAIL] 15s 内未出现设置窗口 TSSettingsV2\n");
        TerminateProcess(t.proc, 0);
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        fclose(g_out);
        return 1;
    }

    RECT rc{};
    GetClientRect(t.hwnd, &rc);
    t.scale = (double)(rc.right - rc.left) / BASE_W;
    P(L"设置窗口: hwnd=%p  客户区=%ldx%ld  scale=%.3f\n\n",
      t.hwnd, rc.right - rc.left, rc.bottom - rc.top, t.scale);

    const float tw = (float)(BASE_W - 24.f) / TAB_N;
    auto tabX = [&](int i) { return (int)(12 + i * tw + tw / 2); };
    const int tabY = TITLE_H + TAB_H / 2;

    bool pass = true;

    // 1) 逐个切换 6 个 Tab（驱动页最后）
    for (int i = 0; i < TAB_N && pass; ++i) {
        char name[64];
        sprintf_s(name, "切换到 Tab %d%s", i, i == 5 ? " (驱动)" : "");
        pass = Step(t, name, tabX(i), tabY, 400);
    }

    // 2) 压力：反复在显示页与驱动页之间来回切（驱动页重绘负载最高）
    for (int i = 0; i < 10 && pass; ++i) {
        pass = Step(t, "循环切换 显示->驱动", tabX(0), tabY, 60);
        if (pass) pass = Step(t, "循环切换 驱动", tabX(5), tabY, 60);
    }

    // 3) 停在驱动页，点两个重装按钮（覆盖后台恢复线程 + 结果回传路径）
    if (pass) pass = Step(t, "回到驱动页", tabX(5), tabY, 300);
    if (pass) pass = Step(t, "点击「重新安装（本地资源 · 自动清理残留）」", BTN_LOCAL_X, BTN_LOCAL_Y, 6000);
    if (pass) pass = Step(t, "点击「从网络下载并安装」", BTN_NET_X, BTN_NET_Y, 6000);
    if (pass) pass = Step(t, "压力后切回显示页", tabX(0), tabY, 400);

    // 4) 先把结论落盘（不依赖后续崩溃日志回读是否成功）
    P(L"\n结果: %d/%d 步通过\n", g_alive, g_total);
    if (pass) P(L"步骤结论: 无崩溃\n");
    else      P(L"步骤结论: 存在崩溃，首个失败步骤: %hs\n", g_failStep ? g_failStep : "?");

    // 5) 回读被测程序自建的崩溃日志（本机 WER 已关闭，这是唯一的崩溃证据来源）
    bool crashLogged = false;
    FILE* cf = nullptr;
    if (_wfopen_s(&cf, crashLog, L"r") == 0 && cf) {
        char line[1024];
        while (fgets(line, sizeof(line), cf)) {
            if (strstr(line, "崩溃") || strstr(line, "terminate")) {
                crashLogged = true;
                P(L"\n[被测程序崩溃日志]\n");
                P(L"  %hs", line);
                for (int k = 0; k < 12 && fgets(line, sizeof(line), cf); ++k) P(L"  %hs", line);
                break;
            }
        }
        fclose(cf);
    }

    const bool ok = pass && !crashLogged;
    P(L"\n%s\n", ok ? L"PASS: 设置面板切换与驱动页操作均无崩溃"
                    : L"FAIL: 存在崩溃或崩溃日志");
    P(L"埋点日志: %s\n", tracePath);
    P(L"崩溃日志: %s\n", crashLog);
    P(L"报告: %s\n", report);
    fprintf(stdout, "test_settings_crash: %s (%d/%d steps)\n", ok ? "PASS" : "FAIL", g_alive, g_total);

    if (ok) TerminateProcess(t.proc, 0);
    CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    fclose(g_out);
    return ok ? 0 : 1;
}
