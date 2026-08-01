// PawnIo.cpp - PawnIO 驱动接口实现
// 通过 PawnIO 签名驱动读取 AMD Ryzen PM Table（精确 Tctl/Tdie 温度）
#include "pch.h"
#include "PawnIo.h"

// 资源 ID（app.rc 中定义）
#define IDR_RYZENSMU_BLOB 200

PawnIo& PawnIo::Instance() {
    static PawnIo inst;
    return inst;
}

PawnIo::PawnIo() {}

PawnIo::~PawnIo() {
    Shutdown();
}

bool PawnIo::Init() {
    if (m_available) return true;

    // 动态加载 PawnIOLib.dll
    m_hLib = LoadLibraryW(L"C:\\Program Files\\PawnIO\\PawnIOLib.dll");
    if (!m_hLib) {
        // 尝试备用路径
        m_hLib = LoadLibraryW(L"PawnIOLib.dll");
    }
    if (!m_hLib) return false;

    m_fnOpen    = (pawnio_open_t)GetProcAddress(m_hLib, "pawnio_open");
    m_fnLoad    = (pawnio_load_t)GetProcAddress(m_hLib, "pawnio_load");
    m_fnExecute = (pawnio_execute_t)GetProcAddress(m_hLib, "pawnio_execute");
    m_fnClose   = (pawnio_close_t)GetProcAddress(m_hLib, "pawnio_close");

    if (!m_fnOpen || !m_fnLoad || !m_fnExecute || !m_fnClose) {
        FreeLibrary(m_hLib);
        m_hLib = nullptr;
        return false;
    }

    // 打开 PawnIO 设备（需要管理员权限）
    HRESULT hr = m_fnOpen(&m_handle);
    if (FAILED(hr) || !m_handle) {
        FreeLibrary(m_hLib);
        m_hLib = nullptr;
        return false;
    }

    // 从 EXE 资源加载 RyzenSMU blob
    if (!LoadBlobFromResource()) {
        m_fnClose(m_handle);
        m_handle = nullptr;
        FreeLibrary(m_hLib);
        m_hLib = nullptr;
        return false;
    }

    // 解析 PM Table 地址
    if (!ResolvePmTable()) {
        m_fnClose(m_handle);
        m_handle = nullptr;
        FreeLibrary(m_hLib);
        m_hLib = nullptr;
        return false;
    }

    m_available = true;
    return true;
}

void PawnIo::Shutdown() {
    if (m_handle && m_fnClose) {
        m_fnClose(m_handle);
        m_handle = nullptr;
    }
    if (m_hLib) {
        FreeLibrary(m_hLib);
        m_hLib = nullptr;
    }
    m_available = false;
    m_pmTableResolved = false;
}

bool PawnIo::LoadBlobFromResource() {
    // 从 EXE 嵌入资源读取 RyzenSMU.bin
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_RYZENSMU_BLOB), L"BLOB");
    if (!hRes) return false;

    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return false;

    DWORD size = SizeofResource(nullptr, hRes);
    const UCHAR* data = static_cast<const UCHAR*>(LockResource(hData));
    if (!data || size == 0) return false;

    HRESULT hr = m_fnLoad(m_handle, data, size);
    return SUCCEEDED(hr);
}

bool PawnIo::ResolvePmTable() {
    // ioctl_resolve_pm_table: inSize=0, outSize=2 → [version, dramBase]
    ULONG64 out[2] = {};
    SIZE_T ret = 0;
    HRESULT hr = m_fnExecute(m_handle, "ioctl_resolve_pm_table", nullptr, 0, out, 2, &ret);
    if (FAILED(hr) || ret < 2) return false;

    m_pmTableVersion = static_cast<uint32_t>(out[0]);
    m_pmTableBase = out[1];

    // 验证：版本和地址不能为 0
    if (m_pmTableVersion == 0 || m_pmTableBase == 0) return false;

    m_pmTableResolved = true;
    return true;
}

bool PawnIo::UpdateAndReadPmTable() {
    if (!m_pmTableResolved) return false;

    // ioctl_update_pm_table: inSize=0, outSize=1
    ULONG64 out1[1] = {};
    SIZE_T ret = 0;
    HRESULT hr = m_fnExecute(m_handle, "ioctl_update_pm_table", nullptr, 0, out1, 1, &ret);
    if (FAILED(hr)) return false;

    // ioctl_read_pm_table: inSize=1 [size], outSize=size
    ULONG64 sizeArg[1] = { kPmTableSize };
    memset(m_pmTable, 0, sizeof(m_pmTable));
    ret = 0;
    hr = m_fnExecute(m_handle, "ioctl_read_pm_table", sizeArg, 1, m_pmTable, kPmTableSize, &ret);
    if (FAILED(hr) || ret == 0) return false;

    return true;
}

float PawnIo::ReadCpuTemperature() {
    if (!m_available) return -1.0f;

    if (!UpdateAndReadPmTable()) return -1.0f;

    // PM Table 格式：每个 ULONG64 包含两个 float32（低 32 位 + 高 32 位）
    // Phoenix (7840H) PM Table v0x4C0009:
    //   [8].hi = Tctl/Tdie 实时温度
    //   （15 样本 Pearson 相关系数 0.958，平均偏差 -0.88°C vs LHM）
    static constexpr int kTctlIndex = 8;

    float temp;
    uint32_t highBits = static_cast<uint32_t>(m_pmTable[kTctlIndex] >> 32);
    memcpy(&temp, &highBits, sizeof(float));

    // 合理性校验
    if (temp > 0.0f && temp < 150.0f) return temp;

    return -1.0f;
}
