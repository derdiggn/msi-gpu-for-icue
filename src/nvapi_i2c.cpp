// SPDX-License-Identifier: GPL-2.0-or-later
//
#include "nvapi_i2c.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <mutex>

namespace nvi2c {
namespace {

using NvStatus = int32_t;

// NV_I2C_INFO_V3 (x64). sizeof == 0x40.
struct I2CInfoV3 {
    uint32_t version;
    uint32_t displayMask;
    uint8_t isDdcPort;
    uint8_t devAddress;      // 8-bit address (7-bit << 1)
    uint8_t* regAddress;
    uint32_t regAddressSize;
    uint8_t* data;
    uint32_t size;
    uint32_t speed;          // deprecated field, 0xFFFF = use speedKhz
    uint32_t speedKhz;       // 0 = driver default
    uint8_t portId;
    uint32_t isPortIdSet;
};
static_assert(sizeof(I2CInfoV3) == 0x40, "NV_I2C_INFO_V3 layout");

constexpr uint32_t kI2CInfoVersion = sizeof(I2CInfoV3) | (3u << 16);

// The RGB microcontroller sits on I2C port 1 of the GPU.
constexpr uint8_t kRgbPort = 1;

// nvapi_QueryInterface ids.
constexpr uint32_t kIdInitialize = 0x0150E828;
constexpr uint32_t kIdEnumPhysicalGpus = 0xE5AC921F;
constexpr uint32_t kIdGetPciIdentifiers = 0x2DDFB66E;
constexpr uint32_t kIdI2CWriteEx = 0x283AC65A;
constexpr uint32_t kIdI2CReadEx = 0x4D7B0709;

constexpr int kMaxPhysicalGpus = 64;

using QueryInterfaceFn = void*(__cdecl*)(uint32_t);
using InitializeFn = NvStatus(__cdecl*)();
using EnumPhysicalGpusFn = NvStatus(__cdecl*)(GpuHandle* handles, int32_t* count);
using GetPciIdentifiersFn = NvStatus(__cdecl*)(GpuHandle, uint32_t* deviceId, uint32_t* subSystemId,
                                               uint32_t* revisionId, uint32_t* extDeviceId);
using I2CExFn = NvStatus(__cdecl*)(GpuHandle, I2CInfoV3*, uint32_t* unknown);

struct EntryPoints {
    InitializeFn initialize = nullptr;
    EnumPhysicalGpusFn enumPhysicalGpus = nullptr;
    GetPciIdentifiersFn getPciIdentifiers = nullptr;
    I2CExFn i2cWriteEx = nullptr;
    I2CExFn i2cReadEx = nullptr;
};

// g_mutex guards everything below. NvAPI is not documented as thread safe
// for I2C, so it also serialises all bus traffic.
std::mutex g_mutex;
std::wstring g_libraryName = L"nvapi64.dll";
HMODULE g_module = nullptr;
EntryPoints g_api;
bool g_initialized = false;

I2CInfoV3 MakeRequest(uint8_t addr, uint8_t* reg, uint8_t* data)
{
    I2CInfoV3 info{};
    info.version = kI2CInfoVersion;
    info.isDdcPort = 0;
    info.devAddress = static_cast<uint8_t>(addr << 1);
    info.regAddress = reg;
    info.regAddressSize = 1;
    info.data = data;
    info.size = 1;
    info.speed = 0xFFFF;
    info.speedKhz = 0;
    info.portId = kRgbPort;
    info.isPortIdSet = 1;
    return info;
}

bool Fail(HMODULE module, std::string* error, std::string message)
{
    FreeLibrary(module);
    if (error)
        *error = std::move(message);
    return false;
}

} // namespace

bool Initialize(std::string* error)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_initialized)
        return true;

    // Only System32 is searched for the default name, so a planted nvapi64.dll
    // next to the host executable or in the current directory is never loaded.
    // A full path (unit tests) is loaded as given.
    HMODULE module = LoadLibraryExW(g_libraryName.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) {
        if (error)
            *error = "nvapi64.dll not found (NVIDIA driver missing?)";
        return false;
    }

    auto query = reinterpret_cast<QueryInterfaceFn>(GetProcAddress(module, "nvapi_QueryInterface"));
    if (!query)
        return Fail(module, error, "nvapi_QueryInterface not exported");

    EntryPoints api;
    api.initialize = reinterpret_cast<InitializeFn>(query(kIdInitialize));
    api.enumPhysicalGpus = reinterpret_cast<EnumPhysicalGpusFn>(query(kIdEnumPhysicalGpus));
    api.getPciIdentifiers = reinterpret_cast<GetPciIdentifiersFn>(query(kIdGetPciIdentifiers));
    api.i2cWriteEx = reinterpret_cast<I2CExFn>(query(kIdI2CWriteEx));
    api.i2cReadEx = reinterpret_cast<I2CExFn>(query(kIdI2CReadEx));
    if (!api.initialize || !api.enumPhysicalGpus || !api.getPciIdentifiers || !api.i2cWriteEx || !api.i2cReadEx)
        return Fail(module, error, "required NvAPI functions are unavailable");

    NvStatus status = api.initialize();
    if (status != 0)
        return Fail(module, error, "NvAPI_Initialize failed with status " + std::to_string(status));

    g_module = module;
    g_api = api;
    g_initialized = true;
    return true;
}

std::vector<GpuInfo> EnumerateGpus()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    std::vector<GpuInfo> result;
    if (!g_initialized)
        return result;

    GpuHandle handles[kMaxPhysicalGpus] = {};
    int32_t count = 0;
    if (g_api.enumPhysicalGpus(handles, &count) != 0)
        return result;

    for (int32_t i = 0; i < count && i < kMaxPhysicalGpus; ++i) {
        uint32_t deviceId = 0;
        uint32_t subSystemId = 0;
        uint32_t revisionId = 0;
        uint32_t extDeviceId = 0;
        if (g_api.getPciIdentifiers(handles[i], &deviceId, &subSystemId, &revisionId, &extDeviceId) != 0)
            continue;

        GpuInfo info;
        info.handle = handles[i];
        info.vendor = static_cast<uint16_t>(deviceId & 0xFFFF);
        info.device = static_cast<uint16_t>(deviceId >> 16);
        info.subVendor = static_cast<uint16_t>(subSystemId & 0xFFFF);
        info.subDevice = static_cast<uint16_t>(subSystemId >> 16);
        result.push_back(info);
    }
    return result;
}

bool WriteByte(GpuHandle gpu, uint8_t addr, uint8_t reg, uint8_t value)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized)
        return false;
    uint8_t regBuf = reg;
    uint8_t dataBuf = value;
    I2CInfoV3 info = MakeRequest(addr, &regBuf, &dataBuf);
    uint32_t unknown = 0;
    return g_api.i2cWriteEx(gpu, &info, &unknown) == 0;
}

int ReadByte(GpuHandle gpu, uint8_t addr, uint8_t reg)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_initialized)
        return -1;
    uint8_t regBuf = reg;
    uint8_t dataBuf = 0;
    I2CInfoV3 info = MakeRequest(addr, &regBuf, &dataBuf);
    uint32_t unknown = 0;
    if (g_api.i2cReadEx(gpu, &info, &unknown) != 0)
        return -1;
    return dataBuf;
}

void SetLibraryName(const std::wstring& name)
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_libraryName = name;
}

void Reset()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_module)
        FreeLibrary(g_module);
    g_module = nullptr;
    g_api = EntryPoints{};
    g_initialized = false;
}

} // namespace nvi2c
