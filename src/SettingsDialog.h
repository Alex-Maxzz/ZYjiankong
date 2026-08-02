// SettingsDialog.h - 设置窗口（模式对话框，实时预览）
#pragma once
#include "pch.h"

namespace SettingsDialog {
    // 打开/关闭设置窗口（单例，已打开则前置）
    void Show(HWND owner);
    void Close();
    bool IsOpen();
}
