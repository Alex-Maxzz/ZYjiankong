// Monitor.cpp - 硬件数据采集实现
#include "pch.h"
#include "Monitor.h"
#include "NvApi.h"
#include "PawnIo.h"

Monitor& Monitor::Instance() {
    static Monitor inst;
    return inst;
}

Monitor::Monitor() {}

Monitor::~Monitor() {
    Stop();
}

bool Monitor::Start() {
    if (m_running.exchange(true)) return false;

    // 在主线程初始化 NVAPI（不需要 COM）
    NvApi::Instance().Init();

    // PawnIO 初始化（需管理员权限，失败静默降级到 HTTP/WMI）
    PawnIo::Instance().Init();

    // WMI 在采集线程内初始化（COM 单线程亲和）
    m_thread = std::thread([this]() {
        // COM 多线程模型，WMI 需要
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool comInited = SUCCEEDED(hr);

        if (comInited) {
            InitWmi();
        }

        // 初始化首次时间戳
        m_prevSlowTime = std::chrono::steady_clock::now() - std::chrono::seconds(10);
        m_prevNetTime  = std::chrono::steady_clock::now();

        // 采集循环：每秒一次快指标，每 5 秒一次慢指标
        while (m_running.load()) {
            try {
                auto now = std::chrono::steady_clock::now();

                // 快指标（轻量，1s）
                CollectCpuUsage();
                CollectCpuTemp();   // PawnIO 读 PM Table = 内存拷贝，微秒级
                CollectMemory();
                CollectNetwork();

                // 慢指标（WMI/NVAPI 较重，5s）
                if (now - m_prevSlowTime >= std::chrono::seconds(5)) {
                    m_prevSlowTime = now;
                    CollectGpu();
                }
            } catch (...) {
                // 防止异常穿透线程导致 terminate + COM 泄漏
            }

            // 休眠但可被 Stop 唤醒
            for (int i = 0; i < 10 && m_running.load(); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (comInited) {
            ReleaseWmi();
            CoUninitialize();
        }
    });

    return true;
}

void Monitor::Stop() {
    if (!m_running.exchange(false)) return;
    if (m_thread.joinable()) m_thread.join();
    PawnIo::Instance().Shutdown();
}

SystemMetrics Monitor::GetSnapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_metrics;
}

// ============== WMI 辅助 ==============

bool Monitor::InitWmi() {
    HRESULT hr = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWbemLocator, reinterpret_cast<void**>(&m_wmiLocator));
    if (FAILED(hr) || !m_wmiLocator) return false;

    hr = m_wmiLocator->ConnectServer(
        _bstr_t(L"ROOT\\WMI"), nullptr, nullptr, 0,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT, 0, 0, &m_wmiServices);
    if (FAILED(hr) || !m_wmiServices) {
        m_wmiLocator->Release();
        m_wmiLocator = nullptr;
        return false;
    }

    CoSetProxyBlanket(m_wmiServices, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
        nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE);

    // 第二连接：ROOT\CIMv2（ThermalZone 性能计数器，CPU 温度）
    IWbemServices* cimv2 = nullptr;
    hr = m_wmiLocator->ConnectServer(
        _bstr_t(L"ROOT\\CIMv2"), nullptr, nullptr, 0,
        WBEM_FLAG_CONNECT_USE_MAX_WAIT, 0, 0, &cimv2);
    if (SUCCEEDED(hr) && cimv2) {
        CoSetProxyBlanket(cimv2, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
            nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr, EOAC_NONE);
        m_wmiServicesCimv2 = cimv2;
    }

    return true;
}

void Monitor::ReleaseWmi() {
    if (m_wmiServicesCimv2) { m_wmiServicesCimv2->Release(); m_wmiServicesCimv2 = nullptr; }
    if (m_wmiServices) { m_wmiServices->Release(); m_wmiServices = nullptr; }
    if (m_wmiLocator)  { m_wmiLocator->Release();  m_wmiLocator  = nullptr; }
}

bool Monitor::WmiExecuteQuery(const wchar_t* wql, IEnumWbemClassObject** ppEnum) {
    if (!m_wmiServices) return false;
    HRESULT hr = m_wmiServices->ExecQuery(
        bstr_t("WQL"), bstr_t(wql),
        WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
        nullptr, ppEnum);
    return SUCCEEDED(hr) && *ppEnum;
}

// ============== CPU 占用率（GetSystemTimes，超轻量） ==============

void Monitor::CollectCpuUsage() {
    FILETIME ftIdle{}, ftKernel{}, ftUser{};
    if (!GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) return;

    ULARGE_INTEGER curIdle, curKernel, curUser;
    curIdle.LowPart   = ftIdle.dwLowDateTime;
    curIdle.HighPart  = ftIdle.dwHighDateTime;
    curKernel.LowPart = ftKernel.dwLowDateTime;
    curKernel.HighPart= ftKernel.dwHighDateTime;
    curUser.LowPart   = ftUser.dwLowDateTime;
    curUser.HighPart  = ftUser.dwHighDateTime;

    if (m_cpuFirstSample) {
        m_cpuFirstSample = false;
        m_prevIdle = curIdle;
        m_prevKernel = curKernel;
        m_prevUser = curUser;
        return;
    }

    uint64_t dIdle   = curIdle.QuadPart   - m_prevIdle.QuadPart;
    uint64_t dKernel = curKernel.QuadPart - m_prevKernel.QuadPart;
    uint64_t dUser   = curUser.QuadPart   - m_prevUser.QuadPart;
    uint64_t dTotal  = dKernel + dUser;

    m_prevIdle = curIdle;
    m_prevKernel = curKernel;
    m_prevUser = curUser;

    if (dTotal == 0) return;

    float usage = 100.0f * (1.0f - static_cast<float>(dIdle) / static_cast<float>(dTotal));

    std::lock_guard<std::mutex> lock(m_mutex);
    m_metrics.cpuUsage = usage;
}

// ============== CPU 温度 ==============
// 四层降级：
//   1) PawnIO 直读 AMD SMU PM Table（精确 Tctl/Tdie，需管理员权限）
//   2) LibreHardwareMonitor HTTP API（需 LHM 后台运行）
//   3) MSAcpi_ThermalZoneTemperature WMI（Intel 平台）
//   4) ThermalZoneInformation 性能计数器（ACPI 热区，精度较低）
//   均失败时保持 -1（界面不显示）

// 从 LHM JSON 中提取 CPU 温度（简单字符串搜索，无需 JSON 库）
static float ParseLhmCpuTemp(const std::string& json) {
    // 查找 /amdcpu/ 下的 temperature 传感器
    // JSON 节点格式: "Value":"79.5 °C","SensorId":"/amdcpu/0/temperature/2","Type":"Temperature"
    const char* marker = "/amdcpu/";
    size_t pos = 0;
    while ((pos = json.find(marker, pos)) != std::string::npos) {
        // 确认是 temperature 类型
        size_t tempPos = json.find("temperature/", pos);
        if (tempPos != std::string::npos && tempPos - pos < 30) {
            // 向前搜索 "Value":"
            size_t valStart = json.rfind("\"Value\":\"", tempPos);
            if (valStart != std::string::npos && tempPos - valStart < 200) {
                valStart += 9; // strlen("\"Value\":\"")
                float temp = 0;
                int consumed = 0;
                if (sscanf_s(json.c_str() + valStart, "%f%n", &temp, &consumed) == 1 && consumed > 0) {
                    if (temp > 0 && temp < 150) return temp;
                }
            }
        }
        pos += 8;
    }
    return -1.0f;
}

static float FetchLhmTemperature() {
    float result = -1.0f;

    HINTERNET hSession = WinHttpOpen(L"TaskbarStudio/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!hSession) return -1.0f;

    WinHttpSetTimeouts(hSession, 1000, 1000, 1000, 2000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", 8085, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return -1.0f; }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/data.json",
        nullptr, nullptr, nullptr, 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return -1.0f;
    }

    BOOL sent = WinHttpSendRequest(hRequest, nullptr, 0, nullptr, 0, 0, 0);
    if (sent && WinHttpReceiveResponse(hRequest, nullptr)) {
        std::string body;
        DWORD bytesRead = 0;
        char buf[8192];
        while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
            body.append(buf, bytesRead);
            bytesRead = 0;
            if (body.size() > 512 * 1024) break;
        }
        if (!body.empty()) {
            result = ParseLhmCpuTemp(body);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

void Monitor::CollectCpuTemp() {
    float temp = -1.0f;

    // 方案 1：PawnIO 直读 AMD SMU PM Table（精确 Tctl/Tdie，需管理员）
    if (PawnIo::Instance().IsAvailable()) {
        temp = PawnIo::Instance().ReadCpuTemperature();
    }

    // 方案 2：LibreHardwareMonitor HTTP API（备选，需 LHM 后台运行）
    // 失败后 30 秒内不重试，避免 HTTP 超时阻塞 1 秒快速循环
    if (temp < 0) {
        auto now = std::chrono::steady_clock::now();
        if (!m_lhmFailed || (now - m_lhmFailTime >= std::chrono::seconds(30))) {
            temp = FetchLhmTemperature();
            if (temp < 0) {
                m_lhmFailed = true;
                m_lhmFailTime = now;
            } else {
                m_lhmFailed = false;
            }
        }
    }

    // 方案 3：MSAcpi_ThermalZoneTemperature（Intel 常见）
    if (temp < 0) {
        IEnumWbemClassObject* pEnum = nullptr;
        if (WmiExecuteQuery(L"SELECT CurrentTemperature FROM MSAcpi_ThermalZoneTemperature", &pEnum)) {
            IWbemClassObject* pObj = nullptr;
            ULONG ret = 0;
            while (pEnum->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK) {
                VARIANT v;
                VariantInit(&v);
                if (SUCCEEDED(pObj->Get(L"CurrentTemperature", 0, &v, nullptr, nullptr)) &&
                    v.vt == VT_I4) {
                    temp = (v.lVal - 2732) / 10.0f;
                    if (temp < 0 || temp > 150) temp = -1.0f;
                }
                VariantClear(&v);
                pObj->Release();
                if (temp > 0) break;
            }
            pEnum->Release();
        }
    }

    // 方案 4：ThermalZoneInformation 性能计数器（ACPI 热区）
    if (temp < 0 && m_wmiServicesCimv2) {
        IEnumWbemClassObject* pEnum2 = nullptr;
        HRESULT hr = m_wmiServicesCimv2->ExecQuery(
            bstr_t("WQL"),
            bstr_t("SELECT Temperature FROM Win32_PerfFormattedData_Counters_ThermalZoneInformation"),
            WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &pEnum2);
        if (SUCCEEDED(hr) && pEnum2) {
            IWbemClassObject* pObj = nullptr;
            ULONG ret = 0;
            while (pEnum2->Next(WBEM_INFINITE, 1, &pObj, &ret) == S_OK) {
                VARIANT v;
                VariantInit(&v);
                if (SUCCEEDED(pObj->Get(L"Temperature", 0, &v, nullptr, nullptr))) {
                    long long raw = 0;
                    if (v.vt == VT_I4) raw = v.lVal;
                    else if (v.vt == VT_I8) raw = v.llVal;
                    else if (v.vt == VT_BSTR) raw = _wtoi64(v.bstrVal);
                    else if (v.vt == VT_UI4) raw = static_cast<long long>(v.ulVal);
                    if (raw > 0) {
                        float candidate = raw / 10.0f;
                        if (candidate > 0 && candidate < 150) temp = candidate;
                    }
                }
                VariantClear(&v);
                pObj->Release();
                if (temp > 0) break;
            }
            pEnum2->Release();
        }
    }

    // 更新温度 + 过期保护
    if (temp > 0) {
        m_tempFailCount = 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_metrics.cpuTemp = temp;
    } else {
        // 连续失败超过阈值：重置为 -1（界面隐藏），防止显示冻结的假温度
        if (++m_tempFailCount >= kTempFailMax) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_metrics.cpuTemp = -1.0f;
        }
    }
}

// ============== GPU 信息（NVAPI 获取 RTX 4060 温度/占用率） ==============

void Monitor::CollectGpu() {
    float temp = -1.0f, usage = -1.0f;
    if (NvApi::Instance().QueryGpu(temp, usage)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (temp >= 0)  m_metrics.gpuTemp  = temp;
        if (usage >= 0) m_metrics.gpuUsage = usage;
    }
}

// ============== 内存（GlobalMemoryStatusEx，微秒级） ==============

void Monitor::CollectMemory() {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (!GlobalMemoryStatusEx(&ms)) return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_metrics.memTotal  = ms.ullTotalPhys;
    m_metrics.memUsed   = ms.ullTotalPhys - ms.ullAvailPhys;
    m_metrics.memUsage  = static_cast<float>(ms.dwMemoryLoad);
}

// ============== 网络（GetIfTable2，比 PDH 轻量 10 倍） ==============

void Monitor::CollectNetwork() {
    PMIB_IF_TABLE2 ifTable = nullptr;
    if (GetIfTable2(&ifTable) != NO_ERROR || !ifTable) return;

    // 关键修复：必须只统计"真实物理网卡"，避免被 WFP/QoS/Filter 等虚拟接口重复计数
    // 诊断发现 Wi-Fi 网卡会有 6+ 个虚拟接口镜像同一份流量，导致 6 倍以上误差
    // 正确过滤：MediaType 必须是 NdisMedium80211 (Wi-Fi) 或 NdisMedium8023 (以太网)
    //          且 InterfaceAndOperStatusFlags.FilterInterface == 0（不是过滤器接口）
    uint64_t totalUp = 0, totalDown = 0;
    for (ULONG i = 0; i < ifTable->NumEntries; ++i) {
        MIB_IF_ROW2& row = ifTable->Table[i];

        // 排除 loopback
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        // 排除隧道接口
        if (row.Type == IF_TYPE_TUNNEL) continue;
        // 必须处于活动状态
        if (row.OperStatus != IfOperStatusUp) continue;

        // 关键过滤：跳过过滤器接口（WFP/QoS/VirtualWiFi/NativeWiFi 等）
        // 这些接口会镜像物理网卡的流量，导致重复计数
        if (row.InterfaceAndOperStatusFlags.FilterInterface) continue;

        // 只接受真实物理介质类型（Wi-Fi 802.11 或 以太网 802.3）
        // NdisMedium8023 = 0, NdisMedium80211 = 16 (from ntddndis.h)
        // 但 MIB_IF_ROW2.MediaType 使用 NET_LUID 对应的介质类型，802.3 通常为 0
        // 这里改用更稳定的判断：物理网卡一定有 InterfaceIndex 且不是 FilterInterface
        // 已通过 FilterInterface == 0 过滤，这里再加一道：必须是有接收流量的真实接口
        if (row.InOctets == 0 && row.OutOctets == 0) continue;

        totalUp   += row.OutOctets;
        totalDown += row.InOctets;
    }
    FreeMibTable(ifTable);

    auto now = std::chrono::steady_clock::now();

    // 首次采样：只记录基准值，不计算速率（避免启动瞬间数值爆炸）
    if (m_netFirstSample) {
        m_netFirstSample = false;
        m_prevNetUp   = totalUp;
        m_prevNetDown = totalDown;
        m_prevNetTime = now;
        return;
    }

    auto dt = std::chrono::duration<double>(now - m_prevNetTime).count();

    uint64_t upRate = 0, downRate = 0;
    if (dt > 0.1) {
        if (totalUp >= m_prevNetUp)   upRate   = static_cast<uint64_t>((totalUp   - m_prevNetUp)   / dt);
        if (totalDown >= m_prevNetDown) downRate = static_cast<uint64_t>((totalDown - m_prevNetDown) / dt);
        m_prevNetUp   = totalUp;
        m_prevNetDown = totalDown;
        m_prevNetTime = now;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_metrics.netUpload    = upRate;
    m_metrics.netDownload  = downRate;
    m_metrics.netTotalUp   = totalUp;
    m_metrics.netTotalDown = totalDown;
}
