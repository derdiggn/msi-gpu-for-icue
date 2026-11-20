// SPDX-License-Identifier: GPL-2.0-or-later
//
// iCUE 5 device-plugin ABI (x64), as far as it is needed for a lighting-only
// device.
//
// Corsair has not published this interface. The layouts below come from
// expired6978/CUEORGBPlugin (MIT, see THIRD_PARTY.md) and were checked
// against the plugins Corsair ships with iCUE 5.52 (plugins\ASUS, plugins\MSI,
// plugins\Nvidia): all of them export the same four functions, return a
// 0x98-byte function table and a 0x38-byte DeviceInfo.
#pragma once

#include <cstddef>
#include <cstdint>

namespace cue_abi {

// CorsairPluginGetFeatures() bit flags (names from iCUE's Qt meta data).
enum Feature : uint32_t {
    FeatureKeyEvents = 1u << 0,
    FeatureKeyEventConfiguration = 1u << 1,
    FeatureDetachedMode = 1u << 2,
    FeaturePluginProperties = 1u << 3,
    FeatureDeviceProperties = 1u << 4,
    FeatureDeviceChannels = 1u << 5,
    FeatureDeviceQuickLightingZones = 1u << 6,
};

enum class Mode : int32_t { Invalid = 0, Detached = 1, Attached = 2 };

// LED ids. For plugin API versions 0x66..0x6A, iCUEDevicePluginHost maps the
// ids a plugin reports through a QHash to UniversalKey ids. Ids are
// (group << 16) | index. Group 3, index 1..50 maps to
// UniversalKey "Led_PluginGpuZoneZone1".."Zone50". The older CUE SDK ids
// (CLGPU_Zone1 = 1462) are rejected with "Invalid key provided".
constexpr int32_t kLedGpuZone1 = 0x30001;

struct Image {
    char* path;   // relative to the plugin's folder
    char* hash;   // lowercase hex SHA-256 of the file contents
};

struct LedColor {
    int32_t ledId;
    int32_t r;
    int32_t g;
    int32_t b;
};

struct LedPosition {          // 0x20
    int32_t ledId;
    int32_t unk04;
    double x;
    double y;
    int32_t unk18;
    int32_t unk1C;
};
static_assert(sizeof(LedPosition) == 0x20, "LedPosition layout");

struct LedPositions {
    int32_t numberOfLed;
    LedPosition* ledPosition;
};

enum class ZoneAppearance : uint32_t { SvgPath = 0 };

struct LedView {              // 0x20
    int32_t ledId;
    char* path;               // SVG path in device-view image pixels
    ZoneAppearance appearance;
    char* text;
};
static_assert(sizeof(LedView) == 0x20, "LedView layout");

struct LedViews {
    int32_t numberOfLed;
    LedView* view;
};

struct DeviceInfo {           // 0x38
    char* deviceName;
    int32_t deviceType;       // Corsair's own NVIDIA GPU plugin reports 4
    LedPositions* ledPositions;
    Image* thumbnail;
    char* deviceId;
    int32_t numberOfDeviceView;
    Image* promoImage;
};
static_assert(sizeof(DeviceInfo) == 0x38, "DeviceInfo layout");

struct DeviceView {
    Image* view;
    Image* mask;
    LedViews* ledView;
    LedViews* actionZones;
};

using ConnectionCallback = int32_t (*)(void* context, const char* deviceId, int32_t connected);

using GetDeviceInfoFn = DeviceInfo* (*)(const char* deviceId);
using SetLedsColorsFn = bool (*)(const char* deviceId, int32_t size, LedColor* colors);
using SubscribeFn = void (*)(void* context, ConnectionCallback callback);
using UnsubscribeFn = void (*)();
using GetDeviceViewFn = DeviceView* (*)(const char* deviceId, int32_t index);
using FreeDeviceInfoFn = void (*)(DeviceInfo*);
using FreeDeviceViewFn = void (*)(DeviceView*);
using SetModeFn = void (*)(int32_t mode);

// Function table returned by CorsairPluginGetInstance(). Entries this plugin
// does not implement stay null, exactly like Corsair's ASUS plugin does.
struct Instance {             // 0x98
    GetDeviceInfoFn getDeviceInfo;                 // 0x00
    SetLedsColorsFn setLedsColors;                 // 0x08
    SubscribeFn subscribe;                         // 0x10
    UnsubscribeFn unsubscribe;                     // 0x18
    GetDeviceViewFn getDeviceView;                 // 0x20
    FreeDeviceInfoFn freeDeviceInfo;               // 0x28
    FreeDeviceViewFn freeDeviceView;               // 0x30
    void* configureKeyEvent;                       // 0x38
    void* subscribeForEvents;                      // 0x40
    void* unsubscribeFromEvents;                   // 0x48
    SetModeFn setMode;                             // 0x50
    void* getPluginPropertyInfo;                   // 0x58
    void* readPluginPropertyData;                  // 0x60
    void* writePluginPropertyData;                 // 0x68
    void* getDevicePropertyInfo;                   // 0x70
    void* readDevicePropertyData;                  // 0x78
    void* writeDevicePropertyData;                 // 0x80
    void* freePropertyData;                        // 0x88
    void* setLedsColorsAtChannel;                  // 0x90
};
static_assert(sizeof(Instance) == 0x98, "Instance layout");

// Field offsets the host relies on (x64).
static_assert(sizeof(Image) == 0x10 && offsetof(Image, hash) == 0x08, "Image layout");
static_assert(sizeof(LedColor) == 0x10 && offsetof(LedColor, b) == 0x0C, "LedColor layout");
static_assert(offsetof(LedPosition, x) == 0x08 && offsetof(LedPosition, y) == 0x10, "LedPosition layout");
static_assert(sizeof(LedPositions) == 0x10 && offsetof(LedPositions, ledPosition) == 0x08, "LedPositions layout");
static_assert(offsetof(LedView, path) == 0x08 && offsetof(LedView, appearance) == 0x10 &&
                  offsetof(LedView, text) == 0x18,
              "LedView layout");
static_assert(sizeof(LedViews) == 0x10 && offsetof(LedViews, view) == 0x08, "LedViews layout");
static_assert(offsetof(DeviceInfo, deviceType) == 0x08 && offsetof(DeviceInfo, ledPositions) == 0x10 &&
                  offsetof(DeviceInfo, thumbnail) == 0x18 && offsetof(DeviceInfo, deviceId) == 0x20 &&
                  offsetof(DeviceInfo, numberOfDeviceView) == 0x28 && offsetof(DeviceInfo, promoImage) == 0x30,
              "DeviceInfo layout");
static_assert(sizeof(DeviceView) == 0x20 && offsetof(DeviceView, mask) == 0x08 &&
                  offsetof(DeviceView, ledView) == 0x10 && offsetof(DeviceView, actionZones) == 0x18,
              "DeviceView layout");
static_assert(offsetof(Instance, freeDeviceView) == 0x30 && offsetof(Instance, setMode) == 0x50 &&
                  offsetof(Instance, setLedsColorsAtChannel) == 0x90,
              "Instance layout");

} // namespace cue_abi
