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
    bool Init();
    void Shutdown();
    bool IsAvailable() const { return m_available; }

    // 读取 CPU 温度（Tctl/Tdie，摄氏度）
    // 返回 -1 表示失败
    float ReadCpuTemperature();

private:
    PawnIo();
    ~PawnIo();
    PawnIo(const PawnIo&) = delete;
    PawnIo& operator=(const PawnIo&) = delete;

    bool LoadBlobFromResource();
    bool ResolvePmTable();
    bool UpdateAndReadPmTable();

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

    // PM Table 缓存
    uint32_t         m_pmTableVersion{0};
    uint64_t         m_pmTableBase{0};
    static constexpr int kPmTableSize = 1024;  // Phoenix PM table size (ULONG64 count)
    ULONG64          m_pmTable[kPmTableSize]{};
};
