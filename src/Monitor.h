// Monitor.h - 硬件数据采集模块（针对 R7000P 2023: Ryzen 7 7840H + RTX 4060）
// 设计原则：轻量 API 高频采集(1s)，WMI 慢速指标低频采集(5s)
#pragma once
#include "pch.h"

struct SystemMetrics {
    // CPU
    float       cpuUsage   = 0.0f;   // 0-100
    float       cpuTemp    = -1.0f;  // 摄氏度，-1 表示无效
    std::string cpuName;

    // GPU
    float       gpuUsage   = -1.0f;  // 0-100，-1 表示无效
    float       gpuTemp    = -1.0f;  // 摄氏度，-1 表示无效
    std::string gpuName;
    float       gpuMemUsage= -1.0f;  // 显存占用百分比

    // 内存
    uint64_t    memTotal   = 0;      // 字节
    uint64_t    memUsed    = 0;      // 字节
    float       memUsage   = 0.0f;   // 0-100

    // 网络（实时速率）
    uint64_t    netUpload   = 0;     // bytes/s
    uint64_t    netDownload = 0;     // bytes/s
    uint64_t    netTotalUp  = 0;     // 累计上传字节
    uint64_t    netTotalDown= 0;     // 累计下载字节
};

class Monitor {
public:
    static Monitor& Instance();

    // 启动后台采集线程
    bool Start();
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    // 获取当前快照（线程安全）
    SystemMetrics GetSnapshot() const;

private:
    Monitor();
    ~Monitor();
    Monitor(const Monitor&) = delete;
    Monitor& operator=(const Monitor&) = delete;

    // 各采集器
    void CollectCpuUsage();
    void CollectCpuTemp();          // WMI 慢速
    void CollectGpu();              // WMI 慢速
    void CollectMemory();
    void CollectNetwork();

    // WMI 辅助
    bool InitWmi();
    void ReleaseWmi();
    bool WmiExecuteQuery(const wchar_t* wql, IEnumWbemClassObject** ppEnum);

    std::atomic<bool> m_running{false};
    std::thread       m_thread;

    mutable std::mutex m_mutex;
    SystemMetrics      m_metrics{};

    // CPU 占用差值计算
    ULARGE_INTEGER m_prevIdle{};
    ULARGE_INTEGER m_prevKernel{};
    ULARGE_INTEGER m_prevUser{};
    bool           m_cpuFirstSample{true};

    // 网络速率差值计算
    uint64_t m_prevNetUp{0};
    uint64_t m_prevNetDown{0};
    bool     m_netFirstSample{true};  // 首次采样只记录基准，不计算速率
    std::chrono::steady_clock::time_point m_prevNetTime;

    // 慢速指标时间戳
    std::chrono::steady_clock::time_point m_prevSlowTime;

    // HTTP 温度源失败缓存（避免每秒重试阻塞循环）
    std::chrono::steady_clock::time_point m_lhmFailTime{};
    bool     m_lhmFailed{false};

    // 温度连续失败计数（超过阈值重置为 -1，防止显示冻结值）
    int      m_tempFailCount{0};
    static constexpr int kTempFailMax = 10;  // 连续 10 次失败后重置

    // WMI 句柄
    IWbemLocator*  m_wmiLocator{nullptr};
    IWbemServices* m_wmiServices{nullptr};      // ROOT\WMI（GPU 等）
    IWbemServices* m_wmiServicesCimv2{nullptr}; // ROOT\CIMv2（ThermalZone 性能计数器）
};
