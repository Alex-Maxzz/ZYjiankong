// FullscreenDetect.h - 全屏窗口检测（游戏/视频全屏时自动隐藏悬浮窗）
#pragma once
#include "pch.h"

class FullscreenDetect {
public:
    static FullscreenDetect& Instance();

    // 检查当前前台窗口是否全屏（覆盖整个屏幕）
    bool IsFullscreenWindowActive() const;

    // 获取前台窗口所在的显示器矩形
    void GetForegroundMonitorRect(RECT* rc) const;

private:
    FullscreenDetect() = default;
    FullscreenDetect(const FullscreenDetect&) = delete;
    FullscreenDetect& operator=(const FullscreenDetect&) = delete;
};
