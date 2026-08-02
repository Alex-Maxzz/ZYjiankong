// AppConfig.cpp - 配置持久化实现
#include "pch.h"
#include "AppConfig.h"
#include <shlobj.h>

#pragma comment(lib, "shell32.lib")

// 简易 JSON 读写（无需第三方库）

static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kAppTitle = L"TaskbarStudio";

AppConfig& AppConfig::Instance() {
    static AppConfig inst;
    return inst;
}

AppConfig::AppConfig() {}

std::wstring AppConfig::GetConfigPath() const {
    PWSTR appData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData))) {
        std::wstring dir = appData;
        CoTaskMemFree(appData);
        dir += L"\\TaskbarStudio";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\config.json";
    }
    return L"config.json";
}

bool AppConfig::Load() {
    std::wstring path = GetConfigPath();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { m_loaded = true; return false; }

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0 || sz.QuadPart > 1024 * 1024) {
        CloseHandle(h);
        m_loaded = true;
        return false;
    }
    std::string json(static_cast<size_t>(sz.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(h, json.data(), static_cast<DWORD>(sz.QuadPart), &read, nullptr);
    CloseHandle(h);
    json.resize(read);  // 截断到实际读取长度

    // 极简解析：查找 key=value 模式
    auto getBool = [&](const char* key, bool def) -> bool {
        std::string k = "\"" + std::string(key) + "\":";
        auto pos = json.find(k);
        if (pos == std::string::npos) return def;
        pos += k.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        return json.substr(pos, 4) == "true";
    };
    auto getFloat = [&](const char* key, float def) -> float {
        std::string k = "\"" + std::string(key) + "\":";
        auto pos = json.find(k);
        if (pos == std::string::npos) return def;
        pos += k.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        try { return std::stof(json.substr(pos)); } catch (...) { return def; }
    };
    auto getUint = [&](const char* key, uint32_t def) -> uint32_t {
        std::string k = "\"" + std::string(key) + "\":";
        auto pos = json.find(k);
        if (pos == std::string::npos) return def;
        pos += k.size();
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) pos++;
        try { return static_cast<uint32_t>(std::stoul(json.substr(pos), nullptr, 0)); } catch (...) { return def; }
    };
    auto getStringW = [&](const char* key, const wchar_t* def) -> std::wstring {
        std::string k = "\"" + std::string(key) + "\":\"";
        auto pos = json.find(k);
        if (pos == std::string::npos) return def;
        pos += k.size();
        auto end = json.find('"', pos);
        if (end == std::string::npos) return def;
        std::string val = json.substr(pos, end - pos);
        int wlen = MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), nullptr, 0);
        std::wstring wval(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), &wval[0], wlen);
        return wval;
    };

    m_cfg.showCpuTemp      = getBool("showCpuTemp", true);
    m_cfg.showCpuUsage     = getBool("showCpuUsage", true);
    m_cfg.showGpuTemp      = getBool("showGpuTemp", true);
    m_cfg.showGpuUsage     = getBool("showGpuUsage", true);
    m_cfg.showMemUsage     = getBool("showMemUsage", true);
    m_cfg.showNetUp        = getBool("showNetUp", true);
    m_cfg.showNetDown      = getBool("showNetDown", true);
    m_cfg.fontSize         = getFloat("fontSize", 12.0f);
    m_cfg.textColor        = getUint("textColor", 0xFFFFFFFF);
    m_cfg.accentColor      = getUint("accentColor", 0xFF4A90E2);

    m_cfg.showIndicatorDots = getBool("showIndicatorDots", true);
    m_cfg.tempColorGradient = getBool("tempColorGradient", true);
    m_cfg.netColorSplit     = getBool("netColorSplit", true);
    m_cfg.showSeparator     = getBool("showSeparator", true);
    m_cfg.netUpColor        = getUint("netUpColor", 0xFFFF8C00);
    m_cfg.netDownColor      = getUint("netDownColor", 0xFF00CED1);

    // 字体 + 温度阈值 + 间距
    m_cfg.fontFamily        = getStringW("fontFamily", L"Segoe UI");
    m_cfg.tempLowThreshold  = getFloat("tempLowThreshold", 45.0f);
    m_cfg.tempHighThreshold = getFloat("tempHighThreshold", 90.0f);
    m_cfg.spacingScale      = getFloat("spacingScale", 1.0f);
    m_cfg.overlayOpacity    = getFloat("overlayOpacity", 1.0f);

    m_cfg.hideOnFullscreen = getBool("hideOnFullscreen", true);
    m_cfg.runOnStartup     = IsStartupEnabled();

    // ---- 配置校验：异常值回退默认 ----
    if (m_cfg.fontSize < 8.0f || m_cfg.fontSize > 24.0f) m_cfg.fontSize = 12.0f;
    if (m_cfg.textColor == 0)   m_cfg.textColor = 0xFFFFFFFF;
    if (m_cfg.accentColor == 0) m_cfg.accentColor = 0xFF4A90E2;
    if (m_cfg.netUpColor == 0)  m_cfg.netUpColor = 0xFFFF8C00;
    if (m_cfg.netDownColor == 0) m_cfg.netDownColor = 0xFF00CED1;

    m_loaded = true;
    return true;
}

bool AppConfig::Save() {
    std::wstring path = GetConfigPath();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    // fontFamily wstring → UTF-8
    char fontUtf8[256] = "Segoe UI";
    WideCharToMultiByte(CP_UTF8, 0, m_cfg.fontFamily.c_str(), -1,
        fontUtf8, sizeof(fontUtf8), nullptr, nullptr);

    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
        "{\n"
        "  \"showCpuTemp\": %s,\n"
        "  \"showCpuUsage\": %s,\n"
        "  \"showGpuTemp\": %s,\n"
        "  \"showGpuUsage\": %s,\n"
        "  \"showMemUsage\": %s,\n"
        "  \"showNetUp\": %s,\n"
        "  \"showNetDown\": %s,\n"
        "  \"fontSize\": %.1f,\n"
        "  \"textColor\": 0x%08X,\n"
        "  \"accentColor\": 0x%08X,\n"
        "  \"showIndicatorDots\": %s,\n"
        "  \"tempColorGradient\": %s,\n"
        "  \"netColorSplit\": %s,\n"
        "  \"showSeparator\": %s,\n"
        "  \"netUpColor\": 0x%08X,\n"
        "  \"netDownColor\": 0x%08X,\n"
        "  \"fontFamily\": \"%s\",\n"
        "  \"tempLowThreshold\": %.1f,\n"
        "  \"tempHighThreshold\": %.1f,\n"
        "  \"spacingScale\": %.2f,\n"
        "  \"overlayOpacity\": %.2f,\n"
        "  \"hideOnFullscreen\": %s,\n"
        "  \"runOnStartup\": %s\n"
        "}\n",
        m_cfg.showCpuTemp      ? "true" : "false",
        m_cfg.showCpuUsage     ? "true" : "false",
        m_cfg.showGpuTemp      ? "true" : "false",
        m_cfg.showGpuUsage     ? "true" : "false",
        m_cfg.showMemUsage     ? "true" : "false",
        m_cfg.showNetUp        ? "true" : "false",
        m_cfg.showNetDown      ? "true" : "false",
        m_cfg.fontSize,
        m_cfg.textColor,
        m_cfg.accentColor,
        m_cfg.showIndicatorDots ? "true" : "false",
        m_cfg.tempColorGradient ? "true" : "false",
        m_cfg.netColorSplit     ? "true" : "false",
        m_cfg.showSeparator     ? "true" : "false",
        m_cfg.netUpColor,
        m_cfg.netDownColor,
        fontUtf8,
        m_cfg.tempLowThreshold,
        m_cfg.tempHighThreshold,
        m_cfg.spacingScale,
        m_cfg.overlayOpacity,
        m_cfg.hideOnFullscreen ? "true" : "false",
        m_cfg.runOnStartup     ? "true" : "false");

    DWORD written = 0;
    WriteFile(h, buf, static_cast<DWORD>(n), &written, nullptr);
    CloseHandle(h);

    // 仅在开机启动状态变化时写注册表（避免每次 Save 都触发）
    if (IsStartupEnabled() != m_cfg.runOnStartup) {
        EnableStartup(m_cfg.runOnStartup);
    }
    return true;
}

// ===================== 开机启动（任务计划程序） =====================
// 程序 manifest 要求 requireAdministrator，注册表 Run 键启动需要 UAC 弹窗，
// Windows 会静默跳过或被 UAC 拦截。改用任务计划程序 /rl highest 自动提权。
// 本进程已是管理员权限，schtasks 继承令牌直接执行。

static const wchar_t* kTaskName = L"TaskbarStudio_AutoStart";

// 辅助：执行 schtasks 命令并等待返回
static int RunSchtasks(const wchar_t* params) {
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};

    std::wstring cmd = std::wstring(L"schtasks.exe ") + params;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        return -1;
    }
    WaitForSingleObject(pi.hProcess, 10000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return static_cast<int>(exitCode);
}

bool AppConfig::IsStartupEnabled() {
    // 优先检查任务计划程序
    if (RunSchtasks(L"/query /tn TaskbarStudio_AutoStart") == 0)
        return true;

    // 回退检查注册表（兼容旧版本写入的项）
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        wchar_t value[MAX_PATH] = {};
        DWORD sz = sizeof(value);
        LSTATUS r = RegQueryValueExW(hKey, kAppTitle, nullptr, nullptr,
            reinterpret_cast<LPBYTE>(value), &sz);
        RegCloseKey(hKey);
        if (r == ERROR_SUCCESS) {
            // 旧注册表项存在 → 迁移到任务计划程序，然后删除注册表项
            EnableStartup(true);
            if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
                RegDeleteValueW(hKey, kAppTitle);
                RegCloseKey(hKey);
            }
            return true;
        }
    }
    return false;
}

bool AppConfig::EnableStartup(bool enable) {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    if (enable) {
        // 创建任务计划：用户登录时以最高权限静默运行
        std::wstring params = L"/create /tn TaskbarStudio_AutoStart /tr \"\\\"" +
            std::wstring(exePath) + L"\\\" --silent\" /sc onlogon /rl highest /f";
        return RunSchtasks(params.c_str()) == 0;
    } else {
        // 删除任务计划
        RunSchtasks(L"/delete /tn TaskbarStudio_AutoStart /f");
        // 同时清理旧注册表项（兼容迁移）
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, kAppTitle);
            RegCloseKey(hKey);
        }
        return true;
    }
}
