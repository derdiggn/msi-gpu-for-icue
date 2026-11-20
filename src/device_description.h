// SPDX-License-Identifier: GPL-2.0-or-later
//
// Builds the structures iCUE asks for (DeviceInfo, DeviceView) and frees them
// again. Kept free of global state so they can be tested in isolation.
#pragma once

#include "card_style.h"
#include "cue_plugin_abi.h"
#include "light_device.h"
#include "nvapi_i2c.h"

#include <cstdint>
#include <string>

namespace device {

// Pictures of one card style, shipped in the plugin folder below
// images/<folder>/, and the light bar in its device_view.png in pixels.
// The values come from tools/make-images.ps1, which warns if they differ.
struct CardArt {
    const char* folder;
    int barLeft;
    int barRight;
    int barTop;
    int barBottom;
};

const CardArt& ArtFor(CardStyle style);

// Path of one picture relative to the plugin folder, e.g.
// "images/suprim_liquid/promo.png".
std::string ImagePath(CardStyle style, const char* file);

// Stable iCUE device id, e.g. "MSIGPU_10DE2C02_14625312". iCUE keys its
// lighting configuration on it, so it must never change for a given card.
std::string DeviceIdFor(const nvi2c::GpuInfo& gpu);

// Lowercase hex SHA-256 of a file, "" if it cannot be read.
std::string Sha256File(const std::wstring& path);

// Caller frees the result with FreeDeviceInfo().
cue_abi::DeviceInfo* BuildDeviceInfo(const std::string& name, const std::string& deviceId, int32_t deviceType,
                                     int32_t ledCount, CardStyle style, const std::wstring& pluginDir);
void FreeDeviceInfo(cue_abi::DeviceInfo* info);

// Splits the style's light bar into `ledCount` zones. Caller frees the result
// with FreeDeviceView().
cue_abi::DeviceView* BuildDeviceView(int32_t ledCount, CardStyle style, const std::wstring& pluginDir);
void FreeDeviceView(cue_abi::DeviceView* view);

// Averages the colours of this device's LEDs (kLedGpuZone1 .. +ledCount-1)
// and clamps each channel to 0..255. Returns false if none of them is in
// `colors`.
bool AverageColor(const cue_abi::LedColor* colors, int32_t size, int32_t ledCount, Rgb& out);

} // namespace device
