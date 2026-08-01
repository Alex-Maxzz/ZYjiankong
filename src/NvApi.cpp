// NvApi.cpp - NVIDIA GPU 温度/占用率采集实现
// 通过动态加载 nvapi64.dll，无需 NVIDIA SDK 头文件
#include "pch.h"
#include "NvApi.h"
#include <cstdio>

// NVAPI 函数 ID
constexpr unsigned int NVAPI_ID_INITIALIZE    = 0x0150E828;
constexpr unsigned int NVAPI_ID_ENUM_PHYSICAL = 0xE5AC921F;
constexpr unsigned int NVAPI_ID_THERMAL       = 0xE3640A56;
constexpr unsigned int NVAPI_ID_USAGES        = 0x189A1FDF;

constexpr int NVAPI_OK = 0;

// 热传感器结构体（V2, size=68, 无 flags 字段）
struct NV_GPU_THERMAL_SETTINGS {
    unsigned int version;       // 68 | 0x20000
    unsigned int count;
    struct {
        int controller;
        int defaultMinTemp;
        int defaultMaxTemp;
        int currentTemp;        // 当前温度（摄氏度）
        int target;
    } sensor[3];
};
static_assert(sizeof(NV_GPU_THERMAL_SETTINGS) == 68, "Thermal struct size mismatch");

// GPU 占用率结构体（V1, size=136）
struct NV_GPU_USAGES {
    unsigned int version;       // 136 | 0x10000
    unsigned int usages[33];
};
static_assert(sizeof(NV_GPU_USAGES) == 136, "Usages struct size mismatch");

// ============================================================

NvApi& NvApi::Instance() {
    static NvApi inst;
    return inst;
}

NvApi::NvApi() {}

NvApi::~NvApi() {
    Shutdown();
}

bool NvApi::Init() {
    if (m_inited) return true;

    m_hNvApi = LoadLibraryW(L"nvapi64.dll");
    if (!m_hNvApi) return false;

    auto queryInterface = reinterpret_cast<NvAPI_QueryInterface_t>(
        GetProcAddress(m_hNvApi, "nvapi_QueryInterface"));
    if (!queryInterface) {
        FreeLibrary(m_hNvApi);
        m_hNvApi = nullptr;
        return false;
    }

    m_fnInitialize      = reinterpret_cast<NvAPI_Initialize_t>(
        queryInterface(NVAPI_ID_INITIALIZE));
    m_fnEnumPhysicalGPUs = reinterpret_cast<NvAPI_EnumPhysicalGPUs_t>(
        queryInterface(NVAPI_ID_ENUM_PHYSICAL));
    m_fnGetThermal      = reinterpret_cast<NvAPI_GPU_GetThermalSettings_t>(
        queryInterface(NVAPI_ID_THERMAL));

    // 使用 GetUsages 代替 GetPstates（后者在新驱动上不可用）
    m_fnGetPstates = reinterpret_cast<NvAPI_GPU_GetDynamicPstatesInfoEx_t>(
        queryInterface(NVAPI_ID_USAGES));

    if (!m_fnInitialize || !m_fnEnumPhysicalGPUs || !m_fnGetThermal || !m_fnGetPstates) {
        FreeLibrary(m_hNvApi);
        m_hNvApi = nullptr;
        return false;
    }

    if (m_fnInitialize() != NVAPI_OK) {
        FreeLibrary(m_hNvApi);
        m_hNvApi = nullptr;
        return false;
    }

    void* gpuHandles[64] = {};
    int gpuCount = 0;
    if (m_fnEnumPhysicalGPUs(gpuHandles, &gpuCount) != NVAPI_OK || gpuCount == 0) {
        // 修复资源泄漏：失败时必须释放 nvapi64.dll
        FreeLibrary(m_hNvApi);
        m_hNvApi = nullptr;
        return false;
    }

    m_gpuHandle = gpuHandles[0];
    if (!m_gpuHandle) {
        FreeLibrary(m_hNvApi);
        m_hNvApi = nullptr;
        return false;
    }

    m_inited = true;
    return true;
}

bool NvApi::QueryGpu(float& temp, float& usage) {
    temp = -1.0f;
    usage = -1.0f;
    if (!m_inited || !m_gpuHandle) return false;

    // 查询温度（V2, size=68）
    NV_GPU_THERMAL_SETTINGS thermal{};
    thermal.version = sizeof(NV_GPU_THERMAL_SETTINGS) | 0x20000;
    if (m_fnGetThermal(m_gpuHandle, 0, &thermal) == NVAPI_OK) {
        if (thermal.count > 0) {
            temp = static_cast<float>(thermal.sensor[0].currentTemp);
        }
    }

    // 查询占用率（V1, size=136, usages[2] = GPU 占用率）
    NV_GPU_USAGES usages{};
    usages.version = sizeof(NV_GPU_USAGES) | 0x10000;
    // 复用 GetPstates 函数指针（实际指向 GetUsages）
    auto getUsages = reinterpret_cast<int (*)(void*, void*)>(m_fnGetPstates);
    if (getUsages(m_gpuHandle, &usages) == NVAPI_OK) {
        // usages[2] 是 GPU 引擎占用率
        usage = static_cast<float>(usages.usages[2]);
    }

    return temp >= 0 || usage >= 0;
}

void NvApi::Shutdown() {
    if (m_hNvApi) {
        FreeLibrary(m_hNvApi);
        m_hNvApi = nullptr;
    }
    m_inited = false;
    m_gpuHandle = nullptr;
}
