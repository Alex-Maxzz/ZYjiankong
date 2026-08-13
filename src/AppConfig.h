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

    // 字体
    std::wstring fontFamily = L"Segoe UI";  // 标签字体（数字自动 tnum/fixed-width）

    // 美学增强
    bool     showIndicatorDots  = true;  // 彩色状态指示点
    bool     tempColorGradient  = true;  // 温度数值随温度变色
    bool     netColorSplit      = true;  // 网络上下行异色
    bool     showSeparator      = true;  // 项间分隔符
    uint32_t netUpColor         = 0xFFFF8C00;   // 上行橙色
    uint32_t netDownColor       = 0xFF00CED1;   // 下行青色

    // 温度色阶阈值
    float    tempLowThreshold  = 45.0f;   // 开始变色的温度
    float    tempHighThreshold = 90.0f;   // 全红的温度

    // 布局
    float    spacingScale      = 1.0f;    // 项间距倍率 (0.8~1.5)
    float    overlayOpacity    = 1.0f;    // 整体透明度 (0.3~1.0)

    bool hideOnFullscreen = true;        // 全屏时自动隐藏
    bool runOnStartup     = false;       // 开机启动
    bool showCleanBtn     = false;       // 内存清理按钮（点击清理可用内存）
};

class AppConfig {
public:
    static AppConfig& Instance();

    // 加载/保存配置（%APPDATA%\TaskbarStudio\config.json）
    bool Load();
    bool Save();

    const DisplayConfig& Get() const { return m_cfg; }
    void Set(const DisplayConfig& cfg) { m_cfg = cfg; }

    // 开机启动（任务计划程序，以最高权限运行，绕过 UAC）
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
