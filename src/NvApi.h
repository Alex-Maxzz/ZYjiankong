// NvApi.h - NVIDIA GPU 温度/占用率采集（动态加载 nvapi64.dll，零依赖）
#pragma once
#include "pch.h"

class NvApi {
public:
    static NvApi& Instance();

    // 初始化 NVAPI（动态加载 nvapi64.dll）
    bool Init();

    // 采集 GPU 温度和占用率（RTX 4060 Laptop）
    // temp: 摄氏度，-1 表示失败
    // usage: 0-100，-1 表示失败
    bool QueryGpu(float& temp, float& usage);

    void Shutdown();

private:
    NvApi();
    ~NvApi();
    NvApi(const NvApi&) = delete;
    NvApi& operator=(const NvApi&) = delete;

    // NVAPI 函数指针类型
    using NvAPI_QueryInterface_t        = void* (*)(unsigned int id);
    using NvAPI_Initialize_t            = int (*)();
    using NvAPI_EnumPhysicalGPUs_t      = int (*)(void** gpuHandles, int* count);
    using NvAPI_GPU_GetThermalSettings_t = int (*)(void* gpu, int sensorIndex, void* settings);
    using NvAPI_GPU_GetUsages_t         = int (*)(void* gpu, void* usages);

    HMODULE m_hNvApi{nullptr};
    bool    m_inited{false};

    // 缓存的 GPU 句柄（RTX 4060）
    void*   m_gpuHandle{nullptr};

    // 函数指针
    NvAPI_Initialize_t            m_fnInitialize{nullptr};
    NvAPI_EnumPhysicalGPUs_t      m_fnEnumPhysicalGPUs{nullptr};
    NvAPI_GPU_GetThermalSettings_t m_fnGetThermal{nullptr};
    NvAPI_GPU_GetUsages_t          m_fnGetUsages{nullptr};
};
