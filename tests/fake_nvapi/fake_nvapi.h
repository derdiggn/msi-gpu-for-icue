// SPDX-License-Identifier: GPL-2.0-or-later
//
// Interface of fake_nvapi64.dll, a stand-in for NVIDIA's nvapi64.dll that the
// unit tests load through nvi2c::SetLibraryName(). It implements the five
// NvAPI entry points the plugin uses and records every I2C transfer.
//
// Only plain C types cross the DLL boundary (test exe and fake DLL each have
// their own static CRT).
#pragma once

#include <cstdint>

constexpr int kFakeMaxConfiguredGpus = 8;

struct FakeNvConfig {
    int32_t initStatus = 0;        // NvAPI_Initialize result
    uint32_t initSleepMs = 0;      // delay inside NvAPI_Initialize
    uint32_t hiddenFunction = 0;   // nvapi_QueryInterface id that returns null
    int32_t enumStatus = 0;        // NvAPI_EnumPhysicalGPUs result
    int32_t gpuCount = 0;          // reported GPU count, may exceed 64
    uint32_t deviceId[kFakeMaxConfiguredGpus] = {};      // (device << 16) | vendor
    uint32_t subSystemId[kFakeMaxConfiguredGpus] = {};   // (subDevice << 16) | subVendor
    uint32_t pciFailMask = 0;      // bit i: GetPCIIdentifiers fails for GPU i
    int32_t writeStatus = 0;       // I2CWriteEx result
    int32_t failWriteNumber = -1;  // 0-based number of the one write that fails
    int32_t readStatus = 0;        // I2CReadEx result
    uint8_t readValue = 0;         // byte returned by I2CReadEx
};

// One I2C transfer as seen by the driver, decoded from the raw
// NV_I2C_INFO_V3 bytes at their documented offsets.
struct FakeNvTransfer {
    int32_t write;          // 1 = I2CWriteEx, 0 = I2CReadEx
    void* gpu;
    uint32_t version;       // +0x00
    uint32_t displayMask;   // +0x04
    uint8_t isDdcPort;      // +0x08
    uint8_t devAddress;     // +0x09
    uint32_t regAddressSize;// +0x18
    uint8_t reg;            // *(+0x10)
    uint32_t size;          // +0x28
    uint8_t value;          // *(+0x20), for writes
    uint32_t speed;         // +0x2C
    uint32_t speedKhz;      // +0x30
    uint8_t portId;         // +0x34
    uint32_t isPortIdSet;   // +0x38
};

// GPU handles handed out by the fake: kFakeGpuHandleBase + index.
constexpr uintptr_t kFakeGpuHandleBase = 0x1000;

// Exports (extern "C"):
//   void FakeNv_Configure(const FakeNvConfig*)   replaces the config, clears transfers
//   int  FakeNv_Transfers(FakeNvTransfer*, int)  copies up to `max`, returns the total
//   int  FakeNv_InitCount()                      number of NvAPI_Initialize calls
//   int  FakeNv_EnumCount()                      number of NvAPI_EnumPhysicalGPUs calls
using FakeNvConfigureFn = void (*)(const FakeNvConfig*);
using FakeNvTransfersFn = int (*)(FakeNvTransfer* out, int max);
using FakeNvCountFn = int (*)();
