<div align="center">

# 🖥️ TaskbarStudio · 任务栏资源监控悬浮条

**实时监控 CPU / GPU / 内存 / 网络，常驻任务栏的轻量工具**

[![Windows](https://img.shields.io/badge/Windows-10%2B-0078D4?logo=windows)](https://microsoft.com)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B)](https://isocpp.org)
[![CMake](https://img.shields.io/badge/CMake-≥3.20-064F8C?logo=cmake)](https://cmake.org)
[![DirectComposition](https://img.shields.io/badge/DirectComposition-D2D%20%2B%20DWrite-blue)]()
[![License](https://img.shields.io/badge/license-MIT-informational)]()

[功能特性](#-功能特性) · [技术架构](#️-技术架构) · [快速开始](#-快速开始) · [配置说明](#-配置说明)

</div>

---

## ✨ 功能特性

### 📊 实时监控
| 指标 | 说明 | 数据来源 | 刷新频率 |
|------|------|----------|:--------:|
| CPU 温度 | 摄氏度（Tctl/Tdie） | PawnIO 读取 AMD SMU PM Table | 1s |
| CPU 占用 | 0–100% | Idle/Kernel/User 差值 | 1s |
| GPU 温度 | 摄氏度 | NVAPI（NVIDIA） | 5s |
| GPU 占用 | 0–100% | NVAPI Dynamic P-States | 5s |
| 内存占用 | 0–100% | 性能计数器 | 1s |
| 网络 ↑/↓ | bytes/s，自动换算 K/M/G | 网络接口计数器差值 | 1s |

### 🖥️ 任务栏悬浮条
- **透明叠加** — DirectComposition + Direct2D + DirectWrite，硬件加速渲染
- **不抢焦点** — 悬浮于桌面之上、任务栏之中，不影响正常操作
- **Z-order 最顶层** — 自动 BringToTop，避免被同类窗口遮挡
- **tnum 等宽数字** — 刷新时不跳位

### 🎨 高度可定制
- **开关各项指标** — 右键托盘菜单随时开关
- **三档字号** — 小(10) / 中(12) / 大(14)
- **四种文字颜色** — 白 / 黄 / 青 / 绿
- **外观增强** — 彩色指示点、温度色阶（绿→黄→橙→红）、网络异色、项间分隔符
- **整体透明度 + 项间距** — 自由调整

### 🎮 全屏自动隐藏
检测前台全屏窗口（视频 / 游戏），自动隐身，退出全屏再回来

### ⚙️ 开机自启
通过「任务计划程序」以最高权限静默启动（`--silent`），绕过 UAC 弹窗

---

## 🖥️ 技术架构

```
main.cpp · 程序入口
├─ Monitor          后台采集线程：CPU/GPU/内存/网络，线程安全快照
│   ├─ PawnIo      AMD Ryzen SMU PM Table 读取（CPU 温度）
│   └─ NvApi       NVIDIA GPU 温度 / 占用
├─ OverlayWindow   DirectComposition + Direct2D + DirectWrite 透明悬浮窗
├─ FullscreenDetect 前台全屏窗口检测（自动隐藏）
├─ SettingsDialog   设置窗口（实时预览）
└─ AppConfig        配置加载 / 保存（JSON，%APPDATA%）
```

### 渲染优化要点
- **D3D 设备复用** — 降低每帧开销
- **D2D 画刷缓存** — 避免重复创建
- **IDCompositionVisual** — 独立更新每个显示项
- **Per-Monitor DPI v2** — 多显示器适配

---

## 🛠️ 构建

### 前置条件
- Windows 10+
- Visual Studio（MSVC）
- CMake ≥ 3.20
- C++17

### 编译步骤

```bash
git clone https://github.com/Alex-Maxzz/ZYjiankong.git
cd ZYjiankong
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

编译产物 `TaskbarStudio.exe` 位于 `build/`（已被 `.gitignore` 忽略）。

### ⚠️ 编译选项说明
- **静态链接 MSVC 运行库**（`/MT`），无需目标机器安装 VC++ 运行库
- **嵌入自定义 manifest** — `requireAdministrator` + Per-Monitor DPI 感知 v2

---

## 🚀 运行

**右键 → 以管理员身份运行** `TaskbarStudio.exe`

> 管理员权限用于访问 PawnIO 驱动（读 CPU 温度）和任务计划程序（开机自启）。
> 未安装 PawnIO 或无管理员权限时，程序仍可正常运行，只是 CPU 温度读不到（显示 -1）。

### 托盘右键菜单
- **显示项** — 勾选要监控的指标
- **字号 / 文字颜色** — 调整外观
- **外观增强** — 开关指示点、温度色阶、网络异色、分隔符
- **设置…** — 打开带实时预览的设置窗口
- **开机启动** — 加入 / 取消任务计划程序自启
- **全屏时隐藏** — 开关自动隐身
- **退出** — 关闭程序

### 配置文件
保存在 `%APPDATA%\TaskbarStudio\config.json`（JSON，所有开关与外观都会持久化）。

---

## 📦 依赖与权限

| 依赖 | 用途 | 是否必需 |
|------|------|:--------:|
| PawnIO 驱动 | 读取 AMD CPU 温度（SMU PM Table） | 仅 CPU 温度需要；无则自动降级 |
| NVAPI（`nvapi64.dll`） | 读取 NVIDIA GPU 温度 / 占用 | 程序内动态加载，无需单独安装 |
| 管理员权限 | 访问 PawnIO 驱动 + 任务计划程序自启 | 建议以管理员运行 |

> LibreHardwareMonitor 可选，作为 CPU 温度的备选数据源（HTTP API，端口 8085）。

---

## 🔧 技术细节

### 四层 CPU 温度降级策略
1. **PawnIO 直读** — AMD SMU PM Table（精确 Tctl/Tdie，需管理员）
2. **LibreHardwareMonitor** — HTTP API（需 LHM 后台运行）
3. **MSAcpi_ThermalZoneTemperature** — WMI（Intel 平台）
4. **ThermalZoneInformation** — ACPI 热区性能计数器（精度较低）

> 均失败时保持 -1（界面不显示），连续失败自动复位，不会显示冻结的假数据。

### 网络流量统计
- 只统计**真实物理网卡**，跳过 WFP / QoS / Filter 等虚拟接口
- 避免 6 倍以上流量误差

---

## 📄 许可证

本项目采用 [MIT](LICENSE) 许可证。

---

<div align="center">

**如果这个项目对你有帮助，请给一个 ⭐ Star**

Made with ❤️ by [Alex-Maxzz](https://github.com/Alex-Maxzz)

</div>
