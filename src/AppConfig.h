// AppConfig.h - 应用配置与持久化
#pragma once
#include "pch.h"

// 显示项配置
struct DisplayConfig {
    bool showCpuTemp   = true;
    bool showCpuUsage  = true;
    bool showGpuTemp   = true;
    bool showGpuUsage  = true;
    bool showMemUsage  = true;
    bool showNetUp     = true;
    bool showNetDown   = true;

    float    fontSize    = 12.0f;
    uint32_t textColor   = 0xFFFFFFFF;   // RGBA
    uint32_t accentColor = 0xFF4A90E2;   // 蓝色强调

    bool hideOnFullscreen = true;        // 全屏时自动隐藏
    bool runOnStartup     = false;       // 开机启动
};

class AppConfig {
public:
    static AppConfig& Instance();

    // 加载/保存配置（%APPDATA%\TaskbarStudio\config.json）
    bool Load();
    bool Save();

    const DisplayConfig& Get() const { return m_cfg; }
    void Set(const DisplayConfig& cfg) { m_cfg = cfg; }

    // 开机启动（注册表 HKCU\Software\Microsoft\Windows\CurrentVersion\Run）
    bool IsStartupEnabled();
    bool EnableStartup(bool enable);

private:
    AppConfig();
    AppConfig(const AppConfig&) = delete;
    AppConfig& operator=(const AppConfig&) = delete;

    std::wstring GetConfigPath() const;

    DisplayConfig m_cfg{};
    bool m_loaded{false};
};
