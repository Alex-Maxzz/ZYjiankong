// PawnIo.h - PawnIO 驱动接口（读取 AMD Ryzen SMU PM Table）
// 依赖：PawnIO 驱动已安装（随 LibreHardwareMonitor/PawnIO.Setup 安装）
// 需要管理员权限运行
#pragma once
#include "pch.h"

class PawnIo {
public:
    static PawnIo& Instance();

    // 初始化：打开设备 + 加载 RyzenSMU blob（从 EXE 资源）
    // 需要管理员权限，失败返回 false（静默降级）
    // 如果驱动缺失，会自动尝试从内嵌资源恢复
    bool Init();
    void Shutdown();
    bool IsAvailable() const { return m_available; }

    // 驱动安装状态（三态）
    enum class DriverState {
        Missing,        // 完全没装
        Corrupted,      // 半残：设备节点在但服务/sys 缺失（Code 19）
        Installed       // 服务在且设备无故障
    };

    // 检测驱动状态。比 IsDriverInstalled 更严格：会检查设备节点 problem code，
    // 能识别「设备在、驱动文件或服务键被删」的半残状态。
    static DriverState QueryDriverState();

    // 便捷判断：服务键与设备节点均正常才算已安装
    static bool IsDriverInstalled();

    // 清理 PawnIO 驱动残留（删除 DriverStore 包 + 服务键 + 卸载注册表项）。
    // 用于「深度修复」：安装器遇到半残状态会走进更新分支而失败，必须先清干净。
    // 需要管理员权限。成功返回 true。
    // 注意：会先请求关闭设备句柄，调用方需确保不在数据采集路径上调用。
    static bool PurgeDriver();

    // 从内嵌资源恢复驱动（静默安装），成功返回 true
    // repairBroken=true 时会先检测半残状态并自动深度清理再安装
    static bool RecoverDriverEmbedded(bool repairBroken = true);

    // 从网络下载并安装驱动，成功返回 true
    static bool RecoverDriverNetwork(bool repairBroken = true);

    // 读取 CPU 温度（Tctl/Tdie，摄氏度）
    // 返回 -1 表示失败
    float ReadCpuTemperature();

    // 重新初始化驱动（关闭后重开，用于手动恢复后重连）
    bool Reinit();

private:
    PawnIo();
    ~PawnIo();
    PawnIo(const PawnIo&) = delete;
    PawnIo& operator=(const PawnIo&) = delete;

    bool LoadBlobFromResource();
    bool ResolvePmTable();
    bool UpdateAndReadPmTable();
    void DetectTempOffset();

    // PawnIO API 函数指针
    using pawnio_open_t    = HRESULT (STDAPICALLTYPE*)(PHANDLE);
    using pawnio_load_t    = HRESULT (STDAPICALLTYPE*)(HANDLE, const UCHAR*, SIZE_T);
    using pawnio_execute_t = HRESULT (STDAPICALLTYPE*)(HANDLE, PCSTR, const ULONG64*, SIZE_T, PULONG64, SIZE_T, PSIZE_T);
    using pawnio_close_t   = HRESULT (STDAPICALLTYPE*)(HANDLE);

    HMODULE          m_hLib{nullptr};
    HANDLE           m_handle{nullptr};
    bool             m_available{false};
    bool             m_pmTableResolved{false};

    pawnio_open_t    m_fnOpen{nullptr};
    pawnio_load_t    m_fnLoad{nullptr};
    pawnio_execute_t m_fnExecute{nullptr};
    pawnio_close_t   m_fnClose{nullptr};

    // CPU 代号 + 温度偏移（多平台支持）
    int              m_cpuCodeName{-1};
    int              m_tempIndex{8};     // PM Table ULONG64 索引
    bool             m_tempHigh{true};   // true=高32位, false=低32位

    // PM Table 缓存
    uint32_t         m_pmTableVersion{0};
    uint64_t         m_pmTableBase{0};
    static constexpr int kPmTableSize = 1024;
    ULONG64          m_pmTable[kPmTableSize]{};

    // 陈旧数据检测：SMU 通信中断时 IOCTL 仍返回成功但数据不变
    uint64_t         m_lastChecksum{0};
    int              m_staleCount{0};
    static constexpr int kStaleMax = 10;  // 连续 10 次读数完全相同 → 重连
    int              m_reinitCooldown{0}; // 重连失败后的冷却计数（避免每秒重试）

    // 保护设备句柄与库句柄的生命周期。
    // 采集线程（ReadCpuTemperature → Reinit）与设置面板线程
    // （RecoverDriver* → PurgeDriver）会并发触碰这些状态，必须串行化。
    // 用 recursive：Init() 内部会调RecoverDriverEmbedded()，
    // 后者在半残时又会走 PurgeDriver()，存在同线程重入路径。
    std::recursive_mutex m_handleMutex;
};
