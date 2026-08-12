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
    m_staleCount = 0;
    m_lastChecksum = 0;
    m_reinitCooldown = 0;
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
    // ioctl_get_code_name: outSize=1 → CPU 代号
    ULONG64 codeOut[1] = {};
    SIZE_T ret = 0;
    HRESULT hr = m_fnExecute(m_handle, "ioctl_get_code_name", nullptr, 0, codeOut, 1, &ret);
    if (SUCCEEDED(hr) && ret >= 1) {
        m_cpuCodeName = static_cast<int>(codeOut[0]);
    }

    // ioctl_resolve_pm_table: inSize=0, outSize=2 → [version, dramBase]
    ULONG64 out[2] = {};
    ret = 0;
    hr = m_fnExecute(m_handle, "ioctl_resolve_pm_table", nullptr, 0, out, 2, &ret);
    if (FAILED(hr) || ret < 2) return false;

    m_pmTableVersion = static_cast<uint32_t>(out[0]);
    m_pmTableBase = out[1];

    if (m_pmTableVersion == 0 || m_pmTableBase == 0) return false;

    m_pmTableResolved = true;

    // 根据 CPU 代号确定温度偏移
    DetectTempOffset();

    return true;
}

// PM Table 温度偏移表：{cpuCodeName, ulong64Index, useHighBits}
// 通过 Pearson 相关系数验证（15 样本 vs LHM）
struct TempOffsetEntry {
    int codeName;
    int index;
    bool high;
};

static const TempOffsetEntry kOffsetTable[] = {
    // Phoenix / Lucienne (7040/7030 系列, Zen4/Zen3 笔记本)
    // 验证: r=0.958, 偏差 -0.88°C
    {23, 8, true},   // Lucienne (blob 对 7840H 返回 23)
    {24, 8, true},   // Phoenix
    {25, 8, true},   // Phoenix2
    // Rembrandt (6000 系列, Zen3+ 笔记本)
    {11, 8, true},   // Rembrandt (推测同结构，待验证)
    // Raphael / GraniteRidge (7000/9000 桌面, Zen4/Zen5)
    {17, 8, true},   // Raphael (推测，待验证)
    {18, 8, true},   // GraniteRidge (推测，待验证)
    // Vermeer / Cezanne (5000 桌面/笔记本, Zen3)
    {12, 8, true},   // Vermeer (推测，待验证)
    {14, 8, true},   // Cezanne (推测，待验证)
};

void PawnIo::DetectTempOffset() {
    // 查表
    for (const auto& entry : kOffsetTable) {
        if (entry.codeName == m_cpuCodeName) {
            m_tempIndex = entry.index;
            m_tempHigh = entry.high;
            return;
        }
    }

    // 未知代号：启发式扫描（读两次 PM Table，找 30-110°C 范围内变化的值）
    if (!UpdateAndReadPmTable()) {
        m_tempIndex = 8;  // 默认猜测
        m_tempHigh = true;
        return;
    }

    // 保存第一次读数
    ULONG64 first[kPmTableSize];
    memcpy(first, m_pmTable, sizeof(first));

    // 等 500ms 再读第二次
    Sleep(500);
    if (!UpdateAndReadPmTable()) {
        m_tempIndex = 8;
        m_tempHigh = true;
        return;
    }

    // 找最佳候选：在温度范围内且两次读数不同
    int bestIdx = 8;
    bool bestHigh = true;
    float bestDist = 999;

    for (int i = 0; i < 50; i++) {  // 只扫前 50 个（温度通常在前面）
        for (int half = 0; half < 2; half++) {
            uint32_t raw1 = (half == 0) ? (uint32_t)(first[i] & 0xFFFFFFFF) : (uint32_t)(first[i] >> 32);
            uint32_t raw2 = (half == 0) ? (uint32_t)(m_pmTable[i] & 0xFFFFFFFF) : (uint32_t)(m_pmTable[i] >> 32);
            float v1, v2;
            memcpy(&v1, &raw1, 4);
            memcpy(&v2, &raw2, 4);

            // 必须在合理温度范围，且两次有变化（排除静态值）
            if (v1 > 30 && v1 < 110 && v1 != v2) {
                // 优先选最接近 60°C 的（典型 CPU 工作温度）
                float dist = fabsf(v1 - 60.0f);
                if (dist < bestDist) {
                    bestDist = dist;
                    bestIdx = i;
                    bestHigh = (half == 1);
                }
            }
        }
    }

    m_tempIndex = bestIdx;
    m_tempHigh = bestHigh;
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
    if (!m_available) {
        // 驱动不可用时，定期尝试重连（每 30 秒一次）
        if (m_reinitCooldown > 0) {
            m_reinitCooldown--;
            return -1.0f;
        }
        if (Reinit()) {
            // 重连成功，继续往下读
        } else {
            m_reinitCooldown = 30;  // 失败后 30 秒内不再重试
            return -1.0f;
        }
    }

    if (!UpdateAndReadPmTable()) {
        // IOCTL 调用失败：可能是驱动句柄失效
        if (++m_staleCount >= kStaleMax) {
            if (!Reinit()) {
                m_reinitCooldown = 30;  // 重连失败，30 秒后再试
            }
        }
        return -1.0f;
    }

    // 陈旧数据检测：对整个 PM Table 计算校验和
    // SMU 正常工作时，8KB 数据中至少有功耗/频率/电压在波动
    // 如果校验和连续 N 次完全相同，说明 SMU 通信已中断（IOCTL 假成功）
    uint64_t checksum = 0;
    for (int i = 0; i < kPmTableSize; i++) {
        checksum ^= m_pmTable[i];
    }

    if (checksum == m_lastChecksum) {
        m_staleCount++;
        if (m_staleCount >= kStaleMax) {
            // PM Table 已冻结 10 秒，尝试重连
            if (!Reinit()) {
                m_reinitCooldown = 30;
                return -1.0f;
            }
            // 重连后重试一次读取
            if (!UpdateAndReadPmTable()) return -1.0f;
            // 重新计算校验和
            checksum = 0;
            for (int i = 0; i < kPmTableSize; i++) {
                checksum ^= m_pmTable[i];
            }
        }
    } else {
        m_staleCount = 0;
        m_lastChecksum = checksum;
    }

    // 从检测到的偏移读取温度
    float temp;
    uint32_t bits;
    if (m_tempHigh)
        bits = static_cast<uint32_t>(m_pmTable[m_tempIndex] >> 32);
    else
        bits = static_cast<uint32_t>(m_pmTable[m_tempIndex] & 0xFFFFFFFF);
    memcpy(&temp, &bits, sizeof(float));

    // 合理性校验
    if (temp > 0.0f && temp < 150.0f) return temp;

    return -1.0f;
}

bool PawnIo::Reinit() {
    Shutdown();
    Sleep(200);  // 给驱动一点时间释放资源
    bool ok = Init();
    if (ok) {
        m_staleCount = 0;
        m_lastChecksum = 0;
        m_reinitCooldown = 0;
    }
    return ok;
}
