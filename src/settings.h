// SPDX-License-Identifier: GPL-2.0-or-later
//
// Runtime settings and logging.
//
//   %ProgramData%\MsiGpuForIcue\settings.ini   optional overrides (admin-editable)
//   %LOCALAPPDATA%\MsiGpuForIcue\plugin.log    written while iCUE runs
//
// iCUE starts third-party plugins in their own iCUEDevicePluginHost.exe
// process under the logged-in user (not elevated), so the log lives in the
// user profile. See settings.example.ini for the meaning of every key.
#pragma once

#include <cstdint>
#include <string>

struct Settings {
    int32_t apiVersion = 0x69;      // what CorsairPluginGetAPIVersion() reports
    uint32_t features = 4;          // CorsairPluginGetFeatures(): DetachedMode
    int32_t deviceType = 4;         // same as Corsair's own NVIDIA GPU plugin
    int32_t ledCount = 1;           // virtual LEDs shown in iCUE (1..50), averaged to one colour
    uint32_t frameMs = 33;          // minimum time between two colour writes (>= 10)
    uint32_t initDelayMs = 0;       // per-register delay of the init sequence
    bool turnOffOnExit = false;     // switch the LEDs off when iCUE stops
    bool verbose = false;           // log every colour change
    uint32_t detectRetryMs = 2000;  // first detection retry delay, doubles up to 60 s (not in settings.ini)
};

// Reads the [plugin] section of `iniPath`. Missing file or keys keep their
// defaults; out-of-range values are clamped.
Settings LoadSettings(const std::wstring& iniPath);

// %ProgramData%\MsiGpuForIcue\settings.ini
std::wstring DefaultSettingsPath();

// %LOCALAPPDATA%\MsiGpuForIcue\plugin.log
std::wstring DefaultLogPath();

// Settings of this process, loaded from DefaultSettingsPath() on first use.
const Settings& GetSettings();

// Replaces the process settings (tests only).
void SetSettings(const Settings& settings);

// Appends a timestamped printf-style line to the log. The file is started
// over once it grows beyond 1 MB. Never throws, failures are ignored.
void Log(const char* fmt, ...);

// Redirects the log (tests only). The folder is created on first write.
void SetLogPath(const std::wstring& path);
