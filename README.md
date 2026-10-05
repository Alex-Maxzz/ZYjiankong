# TaskbarStudio · 任务栏资源监控悬浮条

> A lightweight Windows taskbar overlay for real-time hardware monitoring — CPU temperature/usage, GPU temperature/usage, RAM, and network upload/download speeds, displayed as a sleek floating bar on your taskbar.

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
| GPU 占用 | 0–100% | NVAPI（GPU 引擎占用率） |
| 内存占用 | 0–100% | 性能计数器 |
| 网络 ↑ / ↓ | bytes/s，自动换算 K/M/G | 网络接口计数器差值 |

> 采集策略：所有指标**每秒**刷新一次。CPU 占用/内存/网络走轻量系统 API；CPU 温度走 PawnIO 驱动（内存拷贝级开销）；GPU 温度/占用走 NVAPI 动态查询，开销同样很小。各数据源失败时有独立退避与过期保护（连续失败自动复位为「-1（无效）」），不会显示冻结的假数据。

---

## 📦 依赖与权限

| 依赖 | 用途 | 是否必需 |
|------|------|----------|
| **PawnIO 驱动** | 读取 AMD CPU 温度（SMU PM Table） | 想要 CPU 温度则需要；未安装 / 无权限时**自动降级**（温度显示 -1） |
| **NVAPI**（`nvapi64.dll`） | 读取 NVIDIA GPU 温度 / 占用 | 程序内动态加载，无需单独安装 |
| **管理员权限** | 访问 PawnIO 驱动 + 任务计划程序自启 | 建议以管理员运行；manifest 已声明 `requireAdministrator` |

> 未安装 PawnIO 或没给管理员权限时，程序仍可正常运行，只是 CPU 温度读不到（其余指标不受影响）。

### ⚠️ 关于驱动自动安装与安全

- 本程序检测到 PawnIO 驱动缺失时，会从程序内置资源**静默安装** PawnIO（无需你手动操作）；若内置安装失败，也会尝试从 GitHub 官方 Release（[namazso/PawnIO.Setup](https://github.com/namazso/PawnIO.Setup)）下载后安装——**下载的安装器必须通过 Authenticode 数字签名验证才会执行**。
- PawnIO 是有签名的内核驱动，被 FanControl、LibreHardwareMonitor 等主流开源硬件监控工具采用，用于替代已被 Microsoft Defender 标记为漏洞驱动的 WinRing0。
- **已知限制**：PawnIO 当前签名证书曾被部分反作弊系统（如 FACEIT AC）列入阻止名单。如果你玩依赖 FACEIT 反作弊的游戏，PawnIO 可能无法加载（表现为本工具没有 CPU 温度），详见 [PawnIO issue #1](https://github.com/namazso/PawnIO.Setup/issues/1)。

### 🔧 驱动「半残」状态与自动深度修复

某些安全更新或系统清理工具会**单独删掉 `PawnIO.sys` 驱动文件**，却留下 INF、设备节点、Class 绑定等残留。设备管理器此时显示 **Code 19（配置信息不完整）**。

这类状态有个陷阱：安装器看到卸载注册表项还在，会误判「已安装」而走更新分支，不做全新安装；更新分支又依赖已被删除的服务键，于是**反复重装永远失败**。

本程序的处理方式：

| 能力 | 说明 |
|------|------|
| **三态检测** | 不只看服务键，而是用 SetupDi + `CM_Get_DevNode_Status` 查设备节点故障码，能识别 Code 19 半残态 |
| **探测式深度清理** | 仅在检测到半残时自动清理：`pnputil /delete-driver` 删驱动包 → 删服务键 → 删卸载注册表项，然后才调用安装器 |
| **安装后轮询** | 驱动落地后 PnP 需时间生成服务键，最多轮询 5 秒再判定成功，避免误报失败 |

设置面板「驱动」页会显示三态状态（已安装 / 未安装 / **已损坏（需深度修复）**），点「重新安装（本地资源）」即可完成清理+重装。

若程序外需要手动修复，右键以管理员身份运行 `tools/repair_pawnio.bat`，会输出完整报告到 `%TEMP%\pawnio_repair_log.txt`。

> ⚠️ **为什么建议用本程序而不是手工补服务键**：手工 `reg add` 造出的服务键与现有 Class 绑定对不上，`pawnio_open` 未必成功；驱动也不会正常出现在设备管理器里，下次排查更难。

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

## 🧪 自动化验收

程序崩溃后没日志可查是最难查的问题之一。为此本仓库提供两套可复跑的验收工具，
以及程序内置的崩溃取证，避免「靠猜」。

```bash
# 1) 构建验收工具（默认不构建）
cmake -S . -B build -DBUILD_VERIFY_TOOL=ON -DBUILD_UI_TEST=ON
cmake --build build --config Release --target verify_pawnio test_settings_crash TaskbarStudio_uitest

# 2) 驱动状态检测（只读，在「半残」机器上应报 Corrupted）
build/Release/verify_pawnio.exe

# 3) 设置面板端到端验收：自动拉起程序 → 逐个切 Tab → 反复切换 → 点两个重装按钮
#    任何一步进程/窗口死亡即 FAIL，并打印被测程序写的崩溃日志
build/Release/test_settings_crash.exe
```

**为什么需要 `TaskbarStudio_uitest.exe`**：正式版清单是 `requireAdministrator`，
非提权进程无法 `CreateProcess` 拉起它（错误 740），自动化脚本就永远拉不起被测程序。
该验收构建与正式版**同一份源码**，唯一差别是清单为 `asInvoker`。

**崩溃取证**：程序启动即安装未处理异常捕获（`src/CrashLog.cpp`），崩溃后写入

| 文件 | 内容 |
| --- | --- |
| `%TEMP%\ts_crash.log` | 异常码释义、故障模块 + 模块内偏移、调用栈、访问违规详情 |
| `%TEMP%\ts_crash_*.dmp` | minidump，可用 WinDbg / VS 打开 |

> 本机 `HKLM\...\Windows Error Reporting\Disabled=1`（WER 关闭）时，
> 系统不会留下任何崩溃事件或报告，此时只能依赖上述自建日志。
> 注意：堆损坏（`0xC0000374`）由 `__fastfail` 上抛，会绕过 SEH 过滤器，**不会**留下日志。

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
├─ CrashLog         未处理异常捕获（日志 + minidump）
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
- License：**MIT**，可自由使用、修改、分发。

---

*TaskbarStudio — 让你的任务栏多一双「看硬件」的眼睛。*
