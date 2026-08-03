# TaskbarStudio · 任务栏资源监控悬浮条

> 一个常驻系统托盘的轻量监控工具：把 **CPU 温度/占用、GPU 温度/占用、内存、网络上下行** 实时显示成任务栏上的一条悬浮条，精确到秒、几乎零打扰。

针对笔记本场景优化（默认适配 **联想拯救者 R7000P 2023：Ryzen 7 7840H + RTX 4060**），只要平台是 **AMD CPU + NVIDIA 独显** 即可工作。

---

## ✨ 功能特性

- **实时监控**：CPU 温度 / 占用率、GPU 温度 / 占用率、内存占用、网络上行 / 下行速率
- **任务栏悬浮条**：贴近任务栏左边缘，透明叠加、硬件加速渲染，不抢焦点
- **托盘右键菜单**：即开即关各项指标、切换字号、切换文字颜色、开关外观增强
- **全屏自动隐藏**：看视频 / 打游戏时自动隐身，退出全屏再回来
- **开机自启**：通过「任务计划程序」以最高权限静默启动（`--silent`），绕过 UAC 弹窗
- **高度可定制**：彩色状态指示点、温度色阶（冷→热 绿→黄→橙→红）、网络上下行异色、项间分隔符、整体透明度、项间距、温度阈值
- **设置窗口**：模式对话框 + 实时预览
- **单实例**：重复启动自动退出，互不打架

---

## 🖥️ 显示项一览

| 指标 | 说明 | 数据来源 |
|------|------|----------|
| CPU 温度 | 摄氏度（Tctl/Tdie） | **PawnIO 驱动**读取 AMD Ryzen SMU PM Table |
| CPU 占用 | 0–100% | 轻量 API（Idle/Kernel/User 差值） |
| GPU 温度 | 摄氏度 | **NVAPI**（动态加载 `nvapi64.dll`） |
| GPU 占用 | 0–100% | NVAPI Dynamic P-States |
| 内存占用 | 0–100% | 性能计数器 |
| 网络 ↑ / ↓ | bytes/s，自动换算 K/M/G | 网络接口计数器差值 |

> 采集策略：高频指标（CPU 占用、内存、网络）走轻量 API **每秒** 刷新；慢速指标（温度等）走 **WMI 每 5 秒** 一次，避免拖慢主循环。温度连续失败会自动复位为「-1（无效）」，不会显示冻结的假数据。

---

## 📦 依赖与权限

| 依赖 | 用途 | 是否必需 |
|------|------|----------|
| **PawnIO 驱动** | 读取 AMD CPU 温度（SMU PM Table） | 想要 CPU 温度则需要；未安装 / 无权限时**自动降级**（温度显示 -1） |
| **NVAPI**（`nvapi64.dll`） | 读取 NVIDIA GPU 温度 / 占用 | 程序内动态加载，无需单独安装 |
| **管理员权限** | 访问 PawnIO 驱动 + 任务计划程序自启 | 建议以管理员运行；manifest 已声明 `requireAdministrator` |

> 未安装 PawnIO 或没给管理员权限时，程序仍可正常运行，只是 CPU 温度读不到（其余指标不受影响）。

---

## 🔧 构建

要求：**Windows + Visual Studio (MSVC) + CMake ≥ 3.20**，C++17。

```bash
# 克隆
git clone https://github.com/Alex-Maxzz/ZYjiankong.git
cd ZYjiankong

# 生成并编译（静态链接 CRT，产出单文件 exe，免安装）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

编译产物 `TaskbarStudio.exe` 位于 `build/`（`build/` 已被 `.gitignore` 忽略，不会进仓库）。

> 编译选项说明：静态链接 MSVC 运行库（`/MT`），无需目标机器安装 VC++ 运行库；嵌入自定义 manifest 实现 `requireAdministrator` + Per-Monitor DPI 感知 v2。

---

## 🚀 运行

1. **以管理员身份运行** `TaskbarStudio.exe`（推荐右键 → 以管理员身份运行）
2. 任务栏托盘区出现图标，**右键** 打开菜单：
   - 「显示项」勾选要监控的指标
   - 「字号 / 文字颜色」调整外观
   - 「外观增强」开关指示点、温度色阶、网络异色、分隔符
   - 「设置…」打开带实时预览的设置窗口
   - 「开机启动」加入 / 取消任务计划程序自启
   - 「全屏时隐藏」开关自动隐身
3. 「退出」关闭程序

配置文件保存在：**`%APPDATA%\TaskbarStudio\config.json`**（JSON，所有开关与外观都会持久化）。

---

## 🏗️ 技术架构

```
main.cpp            程序入口：单实例、托盘、菜单、计时器、全屏检测调度
├─ Monitor          后台采集线程：CPU/GPU/内存/网络，线程安全快照
│   ├─ PawnIo      AMD Ryzen SMU PM Table 读取（CPU 温度）
│   └─ NvApi       NVIDIA GPU 温度 / 占用
├─ OverlayWindow   DirectComposition + Direct2D + DirectWrite 透明悬浮窗
├─ FullscreenDetect 前台全屏窗口检测（自动隐藏）
├─ SettingsDialog   设置窗口（实时预览）
└─ AppConfig        配置加载 / 保存（JSON，%APPDATA%）
```

**渲染优化要点**：
- 使用 **DirectComposition** 实现真·透明窗口，悬浮于桌面之上、任务栏之中
- 复用 D3D 设备、D2D 画刷与 `IDCompositionVisual`，降低每帧开销
- 数字采用 **tnum 等宽数字**（Tabular Numbers），刷新时不跳位
- 定时 `BringToTop` 维持 Z-order 最顶层，避免被 TranslucentTB 等同类型置顶窗口遮挡

---

## 📝 说明

- 本项目专注**资源监控**，不含任务栏美化功能。
- 仓库目前为**公开（Public）**，欢迎 Issue / PR。
- License：暂未指定，如需开源协议请自行添加。

---

*TaskbarStudio — 让你的任务栏多一双「看硬件」的眼睛。*
