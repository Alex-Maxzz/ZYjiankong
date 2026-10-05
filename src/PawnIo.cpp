// PawnIo.cpp - PawnIO 驱动接口实现
// 通过 PawnIO 签名驱动读取 AMD Ryzen PM Table（精确 Tctl/Tdie 温度）
#include "pch.h"
#include "PawnIo.h"
#include <softpub.h>
#include <wintrust.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

// 资源 ID（app.rc 中定义）
#define IDR_RYZENSMU_BLOB 200
#define IDR_PAWNIO_SETUP  201

#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

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
    std::lock_guard<std::recursive_mutex> lk(m_handleMutex);

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
        // 驱动缺失或半残（设备在、服务/sys 被删）。
        // 此处只做「全新安装」，不做深度清理：清理会 FreeLibrary(m_hLib)，
        // 而下面还要继续用已取出的函数指针，释放后调用即为崩溃。
        // 半残状态的深度清理由设置面板的修复按钮走PurgeDriver() + Reinit() 完成。
        if (QueryDriverState() == DriverState::Missing) {
            RecoverDriverEmbedded(/*repairBroken=*/false);
        }
        // 重试打开
        hr = m_fnOpen(&m_handle);
        if (FAILED(hr) || !m_handle) {
            FreeLibrary(m_hLib);
            m_hLib = nullptr;
            return false;
        }
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
    std::lock_guard<std::recursive_mutex> lk(m_handleMutex);
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
    // 整个采集流程串行化：设置面板可能在另一线程触发 PurgeDriver/Reinit，
    // 那里会关闭句柄并释放 PawnIOLib.dll
    std::lock_guard<std::recursive_mutex> lk(m_handleMutex);

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
    {
        std::lock_guard<std::recursive_mutex> lk(m_handleMutex);
        Shutdown();
    }
    Sleep(200);  // 给驱动一点时间释放资源
    bool ok = Init();  // Init 内部自行加锁（recursive，可重入）
    std::lock_guard<std::recursive_mutex> lk(m_handleMutex);
    if (ok) {
        m_staleCount = 0;
        m_lastChecksum = 0;
        m_reinitCooldown = 0;
    }
    return ok;
}

// ===================== 驱动健康检测与自动恢复 =====================

// 验证文件 Authenticode 签名（WinVerifyTrust，完整证书链 + 未过期/未吊销校验）。
// 从网络下载的驱动安装器必须验签通过才允许静默安装——本程序以管理员运行，
// 安装未经签名验证的可执行文件等于给供应链攻击开后门。
static bool VerifyFileSignature(const wchar_t* filePath) {
    WINTRUST_FILE_INFO fileInfo = {};
    fileInfo.cbStruct       = sizeof(fileInfo);
    fileInfo.pcwszFilePath  = filePath;

    GUID actionGenericVerifyV2 = WINTRUST_ACTION_GENERIC_VERIFY_V2;

    WINTRUST_DATA wd = {};
    wd.cbStruct            = sizeof(wd);
    wd.dwStateAction       = WTD_STATEACTION_VERIFY;   // 完整验证（含 CRL 检查）
    wd.dwUIChoice          = WTD_UI_NONE;              // 静默，不弹任何 UI
    wd.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;    // 校验整条证书链吊销状态
    wd.dwUnionChoice       = WTD_CHOICE_FILE;
    wd.pFile               = &fileInfo;

    LONG st = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE),
                             &actionGenericVerifyV2, &wd);
    // 释放验证状态（与 WTD_STATEACTION_VERIFY 配对）
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &actionGenericVerifyV2, &wd);
    return st == ERROR_SUCCESS;
}

// ===================== 驱动状态检测 =====================

// PawnIO 设备所属的 class GUID（来自 oem163.inf 的 ClassGuid）
static const GUID kPawnIoClassGuid = {
    0x62f9c741, 0xb25a, 0x46ce, {0xb5, 0x4c, 0x9b, 0xcc, 0xce, 0x08, 0xb6, 0xf2}
};

// 打开服务键判断内核服务是否存在
static bool QueryPawnIoService() {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!scm) return false;
    SC_HANDLE svc = OpenServiceW(scm, L"PawnIO", SERVICE_QUERY_STATUS);
    const bool exists = (svc != nullptr);
    if (svc) CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return exists;
}

// 在给定设备列表中查找硬件 ID 认领 Root\PawnIO 的设备。
// 命中时通过 devInstIdOut 回传设备实例 ID（供 CM_Get_DevNode_Status 查询故障码）。
static bool FindPawnIoInDevList(HDEVINFO hDevInfo, std::wstring* devInstIdOut) {
    SP_DEVINFO_DATA devInfo = {};
    devInfo.cbSize = sizeof(devInfo);
    for (DWORD i = 0;; ++i) {
        if (!SetupDiEnumDeviceInfo(hDevInfo, i, &devInfo)) break;

        DWORD reqSize = 0;
        // SPDRP_HARDWAREID 是多字符串属性，先问大小再分配
        SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_HARDWAREID, nullptr,
                                          nullptr, 0, &reqSize);
        if (reqSize == 0) continue;

        std::vector<BYTE> buf(reqSize, 0);
        if (!SetupDiGetDeviceRegistryPropertyW(hDevInfo, &devInfo, SPDRP_HARDWAREID, nullptr,
                                               buf.data(), reqSize, nullptr))
            continue;
        // 多字符串以连续 '\0' 结尾，按单个宽字符串读取即可命中
        const std::wstring id((LPCWSTR)buf.data());
        if (id.find(L"Root\\PawnIO") == std::wstring::npos) continue;

        if (devInstIdOut) {
            DWORD idSize = 0;
            SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfo, nullptr, 0, &idSize);
            if (idSize > 0) {
                std::vector<BYTE> idBuf(idSize, 0);
                if (SetupDiGetDeviceInstanceIdW(hDevInfo, &devInfo, (PWSTR)idBuf.data(),
                                               idSize, nullptr))
                    *devInstIdOut = (LPCWSTR)idBuf.data();
            }
        }
        return true;
    }
    return false;
}

// 用 CM_Get_DevNode_Status 读取设备的故障码（CM_PROB_*）。
// 半残状态（设备节点在、服务/sys 被删）时这里返回 19。
static bool QueryPawnIoProblemCode() {
    HDEVINFO hDevInfo = SetupDiGetClassDevsW(&kPawnIoClassGuid, nullptr, nullptr,
                                              DIGCF_PRESENT);
    if (hDevInfo == INVALID_HANDLE_VALUE) return false;

    std::wstring devInstId;
    const bool found = FindPawnIoInDevList(hDevInfo, &devInstId);
    SetupDiDestroyDeviceInfoList(hDevInfo);
    if (!found || devInstId.empty()) return false;

    DEVINST devInst = 0;
    // CM_Locate_DevNodeW 的deviceID 参数为非常量 LPCWSTR
    DEVINSTID_W devId = const_cast<DEVINSTID_W>(devInstId.c_str());
    if (CM_Locate_DevNodeW(&devInst, devId, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS)
        return false;

    ULONG status = 0, problem = 0;
    if (CM_Get_DevNode_Status(&status, &problem, devInst, 0) != CR_SUCCESS) return false;
    return problem != 0;  // 19 = CM_PROB_REGISTRY（半残特征）
}

// 查 ROOT\PawnIO 设备节点是否存在以及是否处于故障态
static bool QueryPawnIoDeviceNode(bool& hasProblem) {
    hasProblem = false;

    HDEVINFO hPresent = SetupDiGetClassDevsW(&kPawnIoClassGuid, nullptr, nullptr,
                                              DIGCF_PRESENT);
    if (hPresent == INVALID_HANDLE_VALUE) return false;
    const bool present = FindPawnIoInDevList(hPresent, nullptr);
    SetupDiDestroyDeviceInfoList(hPresent);
    if (!present) return false;

    hasProblem = QueryPawnIoProblemCode();
    return true;
}

PawnIo::DriverState PawnIo::QueryDriverState() {
    const bool svc = QueryPawnIoService();
    bool devProblem = false;
    const bool dev = QueryPawnIoDeviceNode(devProblem);

    if (svc && dev && !devProblem) return DriverState::Installed;
    if (dev || svc) return DriverState::Corrupted;
    return DriverState::Missing;
}

bool PawnIo::IsDriverInstalled() {
    return QueryDriverState() == DriverState::Installed;
}

// ===================== 驱动残留清理 =====================

// 安装完成后收尾：等待服务键与设备就绪（声明，实现见文件后部）
static bool FinalizeInstall();

// 枚举 C:\Windows\INF\oem*.inf，找出认领 Root\PawnIO 的那些（正常只有一个）
static std::vector<std::wstring> FindPawnIoOemInfs() {
    std::vector<std::wstring> found;
    WIN32_FIND_DATAW fd = {};
    HANDLE h = FindFirstFileW(L"C:\\Windows\\INF\\oem*.inf", &fd);
    if (h == INVALID_HANDLE_VALUE) return found;

    do {
        const std::wstring full = std::wstring(L"C:\\Windows\\INF\\") + fd.cFileName;
        HANDLE f = CreateFileW(full.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (f == INVALID_HANDLE_VALUE) continue;
        LARGE_INTEGER sz = {};
        GetFileSizeEx(f, &sz);
        if (sz.QuadPart > 0 && sz.QuadPart < 1024 * 1024) {
            std::string text((size_t)sz.QuadPart, '\0');
            DWORD read = 0;
            if (ReadFile(f, text.data(), (DWORD)sz.QuadPart, &read, nullptr) && read > 0) {
                text.resize(read);
                if (text.find("Root\\PawnIO") != std::string::npos)
                    found.push_back(fd.cFileName);
            }
        }
        CloseHandle(f);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return found;
}

bool PawnIo::PurgeDriver() {
    // 释放设备占用，否则文件删不掉、服务停不了。
    // 必须在 Init() 之外调用：Init() 期间 m_hLib 正被使用，
    // 此时 FreeLibrary 会让 Init 后续调用已卸载 DLL 的函数指针。
    {
        std::lock_guard<std::recursive_mutex> lk(Instance().m_handleMutex);
        Instance().Shutdown();
    }
    Sleep(300);

    // ① 删除 DriverStore 里的驱动包（连带移除设备节点与 Class 绑定）
    for (const auto& inf : FindPawnIoOemInfs()) {
        wchar_t sysDir[MAX_PATH] = {};
        if (!GetSystemDirectoryW(sysDir, MAX_PATH)) continue;
        const std::wstring tool = std::wstring(sysDir) + L"\\pnputil.exe";
        const std::wstring args = L"/delete-driver " + inf + L" /uninstall /force";

        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
        sei.lpVerb = L"open";
        sei.lpFile = tool.c_str();
        sei.lpParameters = args.c_str();
        sei.nShow = SW_HIDE;
        if (ShellExecuteExW(&sei) && sei.hProcess) {
            WaitForSingleObject(sei.hProcess, 15000);
            CloseHandle(sei.hProcess);
        }
    }

    // ② 删除残留服务键（pnputil 正常会清，但半残状态下可能留下）
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
                    L"SYSTEM\\CurrentControlSet\\Services\\PawnIO", KEY_WOW64_64KEY, 0);

    // ③ 删除卸载注册表项 —— 关键：没有它，安装器会误判「已安装」而走更新分支
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO",
                    KEY_WOW64_64KEY, 0);
    RegDeleteKeyExW(HKEY_LOCAL_MACHINE,
                    L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\PawnIO",
                    KEY_WOW64_64KEY, 0);

    Sleep(500);
    // 清理后应回到完全未安装状态
    return QueryDriverState() == DriverState::Missing;
}

bool PawnIo::RecoverDriverEmbedded(bool repairBroken) {
    // 探测式深度清理：只在命中半残状态时才清，避免每次重装都无谓地删驱动包
    if (repairBroken && QueryDriverState() == DriverState::Corrupted) {
        PurgeDriver();
    }

    // 从 EXE 资源提取安装器
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_PAWNIO_SETUP), RT_RCDATA);
    if (!hRes) return false;
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return false;
    DWORD size = SizeofResource(nullptr, hRes);
    const void* data = LockResource(hData);
    if (!data || size == 0) return false;

    // 写入临时文件
    wchar_t tempPath[MAX_PATH], tempFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    GetTempFileNameW(tempPath, L"pio", 0, tempFile);
    std::wstring exePath = std::wstring(tempFile) + L".exe";

    HANDLE hFile = CreateFileW(exePath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        DeleteFileW(tempFile);
        return false;
    }
    DWORD written = 0;
    WriteFile(hFile, data, size, &written, nullptr);
    CloseHandle(hFile);

    if (written != size) {
        DeleteFileW(exePath.c_str());
        DeleteFileW(tempFile);
        return false;
    }

    // 静默安装
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = exePath.c_str();
    sei.lpParameters = L"-install -silent";
    sei.nShow = SW_HIDE;
    ShellExecuteExW(&sei);

    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        CloseHandle(sei.hProcess);
    }

    // 清理临时文件
    DeleteFileW(exePath.c_str());
    DeleteFileW(tempFile);

    return FinalizeInstall();
}

// 安装完成后收尾：驱动刚落地时 PnP 需要一点时间创建服务键，
// 立即检查会误判为失败，故轮询等待。
static bool FinalizeInstall() {
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (PawnIo::QueryDriverState() == PawnIo::DriverState::Installed) return true;
        Sleep(500);
    }
    // 至少不再是 Missing 就算部分成功（驱动在但设备待重启刷新）
    return PawnIo::QueryDriverState() != PawnIo::DriverState::Missing;
}

bool PawnIo::RecoverDriverNetwork(bool repairBroken) {
    if (repairBroken && QueryDriverState() == DriverState::Corrupted) {
        PurgeDriver();
    }

    // 使用 WinHTTP 从 GitHub 下载最新安装器
    HINTERNET hSession = WinHttpOpen(L"TaskbarStudio/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return false;

    HINTERNET hConnect = WinHttpConnect(hSession, L"github.com",
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return false; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET",
        L"/namazso/PawnIO.Setup/releases/latest/download/PawnIO_setup.exe",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (bResults) {
        bResults = WinHttpReceiveResponse(hRequest, nullptr);
    }

    if (!bResults) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return false;
    }

    // 下载到临时文件
    wchar_t tempPath[MAX_PATH], tempFile[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    GetTempFileNameW(tempPath, L"pio", 0, tempFile);
    std::wstring exePath = std::wstring(tempFile) + L".exe";

    HANDLE hFile = CreateFileW(exePath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        DeleteFileW(tempFile);
        return false;
    }

    DWORD dwSize = 0;
    DWORD written = 0;
    do {
        dwSize = 0;
        WinHttpQueryDataAvailable(hRequest, &dwSize);
        if (dwSize == 0) break;
        std::vector<char> buffer(dwSize);
        DWORD dwDownloaded = 0;
        if (WinHttpReadData(hRequest, buffer.data(), dwSize, &dwDownloaded)) {
            WriteFile(hFile, buffer.data(), dwDownloaded, &written, nullptr);
        }
    } while (dwSize > 0);

    CloseHandle(hFile);
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    // 检查文件大小是否合理（安装器应 > 1MB）
    LARGE_INTEGER fileSize;
    HANDLE hCheck = CreateFileW(exePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                OPEN_EXISTING, 0, nullptr);
    if (hCheck == INVALID_HANDLE_VALUE) {
        DeleteFileW(exePath.c_str());
        DeleteFileW(tempFile);
        return false;
    }
    GetFileSizeEx(hCheck, &fileSize);
    CloseHandle(hCheck);

    if (fileSize.QuadPart < 1024 * 1024) {  // < 1MB，下载失败
        DeleteFileW(exePath.c_str());
        DeleteFileW(tempFile);
        return false;
    }

    // 安全检查：下载的安装器必须通过 Authenticode 验签才允许静默安装
    // （本程序以管理员运行，未验签的内核驱动安装器 = 供应链攻击入口）
    if (!VerifyFileSignature(exePath.c_str())) {
        DeleteFileW(exePath.c_str());
        DeleteFileW(tempFile);
        return false;
    }

    // 静默安装
    SHELLEXECUTEINFOW sei = {};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    sei.lpVerb = L"open";
    sei.lpFile = exePath.c_str();
    sei.lpParameters = L"-install -silent";
    sei.nShow = SW_HIDE;
    ShellExecuteExW(&sei);

    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, 30000);
        CloseHandle(sei.hProcess);
    }

    DeleteFileW(exePath.c_str());
    DeleteFileW(tempFile);

    return FinalizeInstall();
}
