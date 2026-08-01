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
    GetFileSizeEx(h, &sz);
    std::string json(sz.QuadPart, '\0');
    DWORD read = 0;
    ReadFile(h, json.data(), static_cast<DWORD>(sz.QuadPart), &read, nullptr);
    CloseHandle(h);

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
    m_cfg.hideOnFullscreen = getBool("hideOnFullscreen", true);
    m_cfg.runOnStartup     = IsStartupEnabled();

    m_loaded = true;
    return true;
}

bool AppConfig::Save() {
    std::wstring path = GetConfigPath();
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    char buf[1024];
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
        m_cfg.hideOnFullscreen ? "true" : "false",
        m_cfg.runOnStartup     ? "true" : "false");

    DWORD written = 0;
    WriteFile(h, buf, static_cast<DWORD>(n), &written, nullptr);
    CloseHandle(h);

    // 同步注册表开机启动
    EnableStartup(m_cfg.runOnStartup);
    return true;
}

bool AppConfig::IsStartupEnabled() {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &hKey) != ERROR_SUCCESS)
        return false;

    wchar_t value[MAX_PATH] = {};
    DWORD sz = sizeof(value);
    LSTATUS r = RegQueryValueExW(hKey, kAppTitle, nullptr, nullptr,
        reinterpret_cast<LPBYTE>(value), &sz);
    RegCloseKey(hKey);
    return r == ERROR_SUCCESS;
}

bool AppConfig::EnableStartup(bool enable) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
        return false;

    bool ok = false;
    if (enable) {
        wchar_t exePath[MAX_PATH];
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);
        std::wstring cmd = std::wstring(L"\"") + exePath + L"\" --silent";
        ok = RegSetValueExW(hKey, kAppTitle, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(cmd.c_str()),
            static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    } else {
        ok = RegDeleteValueW(hKey, kAppTitle) == ERROR_SUCCESS ||
             GetLastError() == ERROR_FILE_NOT_FOUND;
    }
    RegCloseKey(hKey);
    return ok;
}
