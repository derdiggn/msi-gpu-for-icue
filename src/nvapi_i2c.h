// SPDX-License-Identifier: GPL-2.0-or-later
//
// Minimal NvAPI loader with the undocumented I2C entry points that the RGB
// controller on NVIDIA partner boards is attached to.
//
// NvAPI exports a single function, nvapi_QueryInterface(id), that returns the
// address of every other entry point. The ids and the NV_I2C_INFO_V3 layout
// are the ones OpenRGB uses (i2c_smbus_nvapi.cpp / dependencies/NVFC), see
// THIRD_PARTY.md. No admin rights are needed.
//
// All functions are thread safe. Bus traffic is serialised internally.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nvi2c {

using GpuHandle = void*;

struct GpuInfo {
    GpuHandle handle = nullptr;
    uint16_t vendor = 0;     // PCI vendor id, 0x10DE for NVIDIA
    uint16_t device = 0;     // PCI device id, e.g. 0x2C02 for an RTX 5080
    uint16_t subVendor = 0;  // board partner, 0x1462 for MSI
    uint16_t subDevice = 0;  // board model
};

// Loads the NvAPI library and initialises it. Returns true immediately when
// already initialised. On failure `error` (optional) receives the reason.
bool Initialize(std::string* error = nullptr);

// Lists all physical NVIDIA GPUs with their PCI ids. Empty when not
// initialised or when NvAPI reports an error.
std::vector<GpuInfo> EnumerateGpus();

// SMBus "write byte data" on the RGB port: [addr] <reg> <value>.
// `addr` is the 7-bit I2C address.
bool WriteByte(GpuHandle gpu, uint8_t addr, uint8_t reg, uint8_t value);

// SMBus "read byte data" on the RGB port. Returns -1 on failure.
int ReadByte(GpuHandle gpu, uint8_t addr, uint8_t reg);

// --- test hooks -------------------------------------------------------------

// Library to load instead of nvapi64.dll (the unit tests use a fake NvAPI).
// Takes effect with the next Initialize() after Reset().
void SetLibraryName(const std::wstring& name);

// Forgets all entry points and unloads the library, so that the next
// Initialize() starts from scratch. Never called by the plugin itself.
void Reset();

} // namespace nvi2c
