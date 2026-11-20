// SPDX-License-Identifier: GPL-2.0-or-later
//
// Fake nvapi64.dll for the unit tests, see fake_nvapi.h.
#include "fake_nvapi.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstring>

namespace {

constexpr int kMaxTransfers = 4096;
constexpr int kMaxHandles = 64;

SRWLOCK g_lock = SRWLOCK_INIT;   // guards everything below
FakeNvConfig g_config;
FakeNvTransfer g_transfers[kMaxTransfers];
int g_transferCount = 0;
int g_writeCount = 0;
int g_initCount = 0;
int g_enumCount = 0;

template <typename T>
T At(const unsigned char* base, size_t offset)
{
    T v;
    memcpy(&v, base + offset, sizeof(T));
    return v;
}

int32_t Record(void* gpu, const void* rawInfo, bool write)
{
    const auto* raw = static_cast<const unsigned char*>(rawInfo);
    FakeNvTransfer t{};
    t.write = write ? 1 : 0;
    t.gpu = gpu;
    t.version = At<uint32_t>(raw, 0x00);
    t.displayMask = At<uint32_t>(raw, 0x04);
    t.isDdcPort = At<uint8_t>(raw, 0x08);
    t.devAddress = At<uint8_t>(raw, 0x09);
    t.reg = *At<uint8_t*>(raw, 0x10);
    t.regAddressSize = At<uint32_t>(raw, 0x18);
    uint8_t* data = At<uint8_t*>(raw, 0x20);
    t.size = At<uint32_t>(raw, 0x28);
    t.speed = At<uint32_t>(raw, 0x2C);
    t.speedKhz = At<uint32_t>(raw, 0x30);
    t.portId = At<uint8_t>(raw, 0x34);
    t.isPortIdSet = At<uint32_t>(raw, 0x38);

    AcquireSRWLockExclusive(&g_lock);
    int32_t status;
    if (write) {
        t.value = *data;
        status = g_config.writeStatus;
        if (g_writeCount == g_config.failWriteNumber)
            status = -1;
        ++g_writeCount;
    } else {
        status = g_config.readStatus;
        if (status == 0)
            *data = g_config.readValue;
    }
    if (g_transferCount < kMaxTransfers)
        g_transfers[g_transferCount++] = t;
    ReleaseSRWLockExclusive(&g_lock);
    return status;
}

int32_t __cdecl Initialize()
{
    AcquireSRWLockExclusive(&g_lock);
    ++g_initCount;
    uint32_t sleepMs = g_config.initSleepMs;
    int32_t status = g_config.initStatus;
    ReleaseSRWLockExclusive(&g_lock);
    Sleep(sleepMs);
    return status;
}

int32_t __cdecl EnumPhysicalGpus(void** handles, int32_t* count)
{
    AcquireSRWLockExclusive(&g_lock);
    ++g_enumCount;
    int32_t status = g_config.enumStatus;
    int32_t n = g_config.gpuCount;
    ReleaseSRWLockExclusive(&g_lock);
    for (int32_t i = 0; i < n && i < kMaxHandles; ++i)
        handles[i] = reinterpret_cast<void*>(kFakeGpuHandleBase + i);
    *count = n;
    return status;
}

int32_t __cdecl GetPciIdentifiers(void* gpu, uint32_t* deviceId, uint32_t* subSystemId, uint32_t* revisionId,
                                  uint32_t* extDeviceId)
{
    int index = static_cast<int>(reinterpret_cast<uintptr_t>(gpu) - kFakeGpuHandleBase);
    AcquireSRWLockShared(&g_lock);
    bool fail = index < 32 && (g_config.pciFailMask & (1u << index)) != 0;
    bool configured = index < kFakeMaxConfiguredGpus;
    *deviceId = configured ? g_config.deviceId[index] : 0;
    *subSystemId = configured ? g_config.subSystemId[index] : 0;
    ReleaseSRWLockShared(&g_lock);
    *revisionId = 0xA1;
    *extDeviceId = 0;
    return fail ? -1 : 0;
}

int32_t __cdecl I2CWriteEx(void* gpu, void* info, uint32_t* /*unknown*/)
{
    return Record(gpu, info, true);
}

int32_t __cdecl I2CReadEx(void* gpu, void* info, uint32_t* /*unknown*/)
{
    return Record(gpu, info, false);
}

} // namespace

extern "C" {

__declspec(dllexport) void* nvapi_QueryInterface(uint32_t id)
{
    AcquireSRWLockShared(&g_lock);
    uint32_t hidden = g_config.hiddenFunction;
    ReleaseSRWLockShared(&g_lock);
    if (id == hidden)
        return nullptr;
    switch (id) {
    case 0x0150E828: return reinterpret_cast<void*>(&Initialize);
    case 0xE5AC921F: return reinterpret_cast<void*>(&EnumPhysicalGpus);
    case 0x2DDFB66E: return reinterpret_cast<void*>(&GetPciIdentifiers);
    case 0x283AC65A: return reinterpret_cast<void*>(&I2CWriteEx);
    case 0x4D7B0709: return reinterpret_cast<void*>(&I2CReadEx);
    default: return nullptr;
    }
}

__declspec(dllexport) void FakeNv_Configure(const FakeNvConfig* config)
{
    AcquireSRWLockExclusive(&g_lock);
    g_config = *config;
    g_transferCount = 0;
    g_writeCount = 0;
    ReleaseSRWLockExclusive(&g_lock);
}

__declspec(dllexport) int FakeNv_Transfers(FakeNvTransfer* out, int max)
{
    AcquireSRWLockShared(&g_lock);
    int n = g_transferCount;
    for (int i = 0; i < n && i < max; ++i)
        out[i] = g_transfers[i];
    ReleaseSRWLockShared(&g_lock);
    return n;
}

__declspec(dllexport) int FakeNv_InitCount()
{
    AcquireSRWLockShared(&g_lock);
    int n = g_initCount;
    ReleaseSRWLockShared(&g_lock);
    return n;
}

__declspec(dllexport) int FakeNv_EnumCount()
{
    AcquireSRWLockShared(&g_lock);
    int n = g_enumCount;
    ReleaseSRWLockShared(&g_lock);
    return n;
}

} // extern "C"
