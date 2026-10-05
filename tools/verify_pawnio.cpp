// verify_pawnio.cpp - 独立验收工具：验证 PawnIo 的驱动状态检测与清理逻辑
// 只做只读检测；加 --purge 参数才执行清理（需要管理员）。
// 用法：
//   verify_pawnio.exe           仅检测，输出状态
//   verify_pawnio.exe --purge   检测后执行深度清理（需管理员）
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <shellapi.h>
#include <cstdio>
#include <string>
#include <vector>

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

static const GUID kPawnIoClassGuid = {
    0x62f9c741, 0xb25a, 0x46ce, {0xb5, 0x4c, 0x9b, 0xcc, 0xce, 0x08, 0xb6, 0xf2}
};

// ---- 与 PawnIo.cpp 中 QueryDriverState() 保持一致的检测实现 ----

static bool QueryService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"PawnIO", SERVICE_QUERY_STATUS);
    const bool exists = (svc != nullptr);
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return exists;
}

static bool FindInDevList(HDEVINFO h, std::wstring* devId) {
    SP_DEVINFO_DATA di = {};
    di.cbSize = sizeof(di);
    for (DWORD i = 0;; ++i) {
        if (!SetupDiEnumDeviceInfo(h, i, &di)) break;
        DWORD need = 0;
        SetupDiGetDeviceRegistryPropertyW(h, &di, SPDRP_HARDWAREID, nullptr, nullptr, 0, &need);
        if (!need) continue;
        std::vector<BYTE> buf(need, 0);
        if (!SetupDiGetDeviceRegistryPropertyW(h, &di, SPDRP_HARDWAREID, nullptr,
                                               buf.data(), need, nullptr)) continue;
        const std::wstring id((LPCWSTR)buf.data());
        if (id.find(L"Root\\PawnIO") == std::wstring::npos) continue;
        if (devId) {
            DWORD sz = 0;
            SetupDiGetDeviceInstanceIdW(h, &di, nullptr, 0, &sz);
            if (sz) {
                std::vector<BYTE> b(sz, 0);
                if (SetupDiGetDeviceInstanceIdW(h, &di, (PWSTR)b.data(), sz, nullptr))
                    *devId = (LPCWSTR)b.data();
            }
        }
        return true;
    }
    return false;
}

static bool QueryDeviceNode(bool& hasProblem, ULONG& problemCode) {
    hasProblem = false;
    problemCode = 0;
    HDEVINFO h = SetupDiGetClassDevsW(&kPawnIoClassGuid, nullptr, nullptr, DIGCF_PRESENT);
    if (h == INVALID_HANDLE_VALUE) return false;
    std::wstring devId;
    const bool found = FindInDevList(h, &devId);
    SetupDiDestroyDeviceInfoList(h);
    if (!found) return false;

    DEVINST dn = 0;
    DEVINSTID_W idc = const_cast<DEVINSTID_W>(devId.c_str());
    if (CM_Locate_DevNodeW(&dn, idc, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) return true;
    ULONG st = 0, pr = 0;
    if (CM_Get_DevNode_Status(&st, &pr, dn, 0) == CR_SUCCESS) {
        problemCode = pr;
        hasProblem = (pr != 0);
    }
    return true;
}

static const wchar_t* StateName(int s) {
    switch (s) {
        case 0:  return L"Missing（完全未安装）";
        case 1:  return L"Corrupted（半残：设备在但服务/驱动文件缺失）";
        default: return L"Installed（正常）";
    }
}

static int QueryState() {
    const bool svc = QueryService();
    bool prob = false;
    ULONG code = 0;
    const bool dev = QueryDeviceNode(prob, code);
    if (svc && dev && !prob) return 2;
    if (dev || svc) return 1;
    return 0;
}

static std::vector<std::wstring> FindOemInfs() {
    std::vector<std::wstring> out;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(L"C:\\Windows\\INF\\oem*.inf", &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        const std::wstring full = std::wstring(L"C:\\Windows\\INF\\") + fd.cFileName;
        HANDLE f = CreateFileW(full.c_str(), GENERIC_READ,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER sz = {};
        GetFileSizeEx(f, &sz);
        if (sz.QuadPart > 0 && sz.QuadPart < 1024 * 1024) {
            std::string t((size_t)sz.QuadPart, '\0');
            DWORD rd = 0;
            if (ReadFile(f, t.data(), (DWORD)sz.QuadPart, &rd, nullptr) && rd) {
                t.resize(rd);
                if (t.find("Root\\PawnIO") != std::string::npos)
                    out.push_back(fd.cFileName);
            }
        }
        CloseHandle(f);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

static bool CheckIsAdmin() {
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID admins = nullptr;
    if (!AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID,
                                  DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admins))
        return false;
    BOOL ok = FALSE;
    CheckTokenMembership(nullptr, admins, &ok);
    FreeSid(admins);
    return ok == TRUE;
}

int wmain(int argc, wchar_t** argv) {
    const bool doPurge = (argc > 1 && wcscmp(argv[1], L"--purge") == 0);
    const bool isAdmin = CheckIsAdmin();

    // 输出走文件：控制台代码页在不同 locale / Git Bash 下会乱码
    FILE* out = nullptr;
    wchar_t outPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, outPath);
    wcscat_s(outPath, L"pawnio_verify.txt");
    _wfopen_s(&out, outPath, L"w, ccs=UTF-8");
    if (!out) return 3;
    std::FILE* fout = out;
    // 每行立即落盘：若中途崩溃，报告里能看出停在何处
    #define WPRINTF(...) do { fwprintf(fout, __VA_ARGS__); fflush(fout); } while (0)

    WPRINTF(L"管理员权限: %s\n", isAdmin ? L"是" : L"否");
    WPRINTF(L"PawnIO.sys 存在: %s\n",
            GetFileAttributesW(L"C:\\Windows\\System32\\drivers\\PawnIO.sys") != INVALID_FILE_ATTRIBUTES
                ? L"是" : L"否");

    HKEY k1 = nullptr;
    const LSTATUS st1 = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Services\\PawnIO", 0, KEY_READ, &k1);
    WPRINTF(L"服务键: %s (LSTATUS=%ld)\n", st1 == ERROR_SUCCESS ? L"存在" : L"无", st1);
    if (k1) RegCloseKey(k1);

    HKEY k2 = nullptr;
    const LSTATUS st2 = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO",
        0, KEY_READ, &k2);
    WPRINTF(L"卸载项: %s (LSTATUS=%ld)\n", st2 == ERROR_SUCCESS ? L"存在" : L"无", st2);
    if (k2) RegCloseKey(k2);

    bool prob = false;
    ULONG code = 0;
    const bool dev = QueryDeviceNode(prob, code);
    WPRINTF(L"设备节点: %s", dev ? L"存在" : L"不存在");
    if (dev) WPRINTF(L"，problem code = %lu%s", code,
                     code == 19 ? L" (CM_PROB_REGISTRY，半残特征)" : L"");
    WPRINTF(L"\n");

    const auto infs = FindOemInfs();
    WPRINTF(L"认领 Root\\PawnIO 的 oem inf: ");
    if (infs.empty()) WPRINTF(L"（无）");
    else for (auto& s : infs) WPRINTF(L"%s ", s.c_str());
    WPRINTF(L"\n\n");

    const int st = QueryState();
    WPRINTF(L"==> QueryDriverState() = %s\n", StateName(st));

    if (doPurge) {
        if (!isAdmin) {
            WPRINTF(L"\n[跳过] --purge 需要管理员权限。\n");
            fclose(fout);
            return 2;
        }
        if (st == 2) {
            WPRINTF(L"\n[跳过] 状态正常，无需清理。\n");
            fclose(fout);
            return 0;
        }
        WPRINTF(L"\n[清理] 删除驱动包...\n");
        for (auto& inf : infs) {
            wchar_t sysDir[MAX_PATH] = {};
            GetSystemDirectoryW(sysDir, MAX_PATH);
            const std::wstring tool = std::wstring(sysDir) + L"\\pnputil.exe";
            const std::wstring args = L"/delete-driver " + inf + L" /uninstall /force";
            WPRINTF(L"  执行: pnputil.exe %s\n", args.c_str());
            SHELLEXECUTEINFOW sei = {};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
            sei.lpVerb = L"open";
            sei.lpFile = tool.c_str();
            sei.lpParameters = args.c_str();
            sei.nShow = SW_HIDE;
            if (ShellExecuteExW(&sei) && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 15000);
                CloseHandle(sei.hProcess);
            }
        }
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
                        L"SYSTEM\\CurrentControlSet\\Services\\PawnIO", KEY_WOW64_64KEY, 0);
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO", KEY_WOW64_64KEY, 0);
        WPRINTF(L"  已清理服务键与卸载项\n");
        Sleep(500);
        const int st2 = QueryState();
        WPRINTF(L"\n==> 清理后状态 = %s\n", StateName(st2));
        const bool pass2 = (st2 == 0);
        WPRINTF(L"%s\n", pass2 ? L"PASS: 已回到完全未安装态，可执行安装器重装"
                                : L"WARN: 未完全清理");
        WPRINTF(L"\n报告已写入: %s\n", outPath);
        fclose(fout);
        return pass2 ? 0 : 1;
    }

    // 断言：当前机器真实处于半残态
    WPRINTF(L"\n[断言] 期望: Corrupted（服务键缺失 + 设备 problem=19）\n");
    WPRINTF(L"[断言] 实际: %s\n", StateName(st));
    const bool pass = (st == 1) && (st1 != ERROR_SUCCESS) && dev && code == 19;
    WPRINTF(L"%s\n", pass ? L"PASS: 检测逻辑正确识别出半残状态"
                          : L"FAIL: 与预期不符");
    WPRINTF(L"\n报告已写入: %s\n", outPath);
    fclose(fout);
    return pass ? 0 : 1;
}
