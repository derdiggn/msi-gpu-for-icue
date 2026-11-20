// SPDX-License-Identifier: GPL-2.0-or-later
//
#include "settings.h"

#include "cue_plugin_abi.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")

namespace {

constexpr wchar_t kFolderName[] = L"\\MsiGpuForIcue";
constexpr uint64_t kMaxLogBytes = 1024 * 1024;

// Feature bits the function table actually implements. Any other bit would
// make iCUE call entries that are null.
constexpr uint32_t kImplementedFeatures = cue_abi::FeatureDetachedMode;

// Empty if the folder cannot be resolved; callers then do nothing rather than
// fall back to a relative path (the root of the current drive, or win.ini).
std::wstring KnownFolder(REFKNOWNFOLDERID id, const wchar_t* file)
{
    PWSTR path = nullptr;
    std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &path)) && path && *path)
        result = std::wstring(path) + kFolderName + file;
    CoTaskMemFree(path);   // required even on failure, accepts null
    return result;
}

int ReadInt(const std::wstring& ini, const wchar_t* key, int def)
{
    return static_cast<int>(GetPrivateProfileIntW(L"plugin", key, def, ini.c_str()));
}

template <typename T>
T Clamp(T v, T lo, T hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

Settings& ProcessSettings()
{
    static Settings settings = LoadSettings(DefaultSettingsPath());
    return settings;
}

// The log target is prepared once, so that Log() itself never allocates and
// therefore cannot throw (it is called from the exception barrier).
struct LogTarget {
    std::wstring file;
    std::wstring dir;   // empty: nothing to create
};

std::mutex g_logMutex;   // guards Target() and the file

LogTarget MakeTarget(const std::wstring& file)
{
    LogTarget t;
    t.file = file;
    size_t slash = file.find_last_of(L'\\');
    if (slash != std::wstring::npos)
        t.dir = file.substr(0, slash);
    return t;
}

LogTarget& Target()
{
    static LogTarget target = MakeTarget(DefaultLogPath());
    return target;
}

} // namespace

Settings LoadSettings(const std::wstring& iniPath)
{
    Settings s;
    if (iniPath.empty())
        return s;
    s.apiVersion = ReadInt(iniPath, L"ApiVersion", s.apiVersion);
    s.features = static_cast<uint32_t>(ReadInt(iniPath, L"Features", static_cast<int>(s.features))) &
                 kImplementedFeatures;
    s.deviceType = ReadInt(iniPath, L"DeviceType", s.deviceType);
    s.ledCount = Clamp(ReadInt(iniPath, L"LedCount", s.ledCount), 1, 50);
    s.frameMs = static_cast<uint32_t>(Clamp(ReadInt(iniPath, L"FrameMs", static_cast<int>(s.frameMs)), 10, 1000));
    s.initDelayMs =
        static_cast<uint32_t>(Clamp(ReadInt(iniPath, L"InitDelayMs", static_cast<int>(s.initDelayMs)), 0, 100));
    s.turnOffOnExit = ReadInt(iniPath, L"TurnOffOnExit", s.turnOffOnExit ? 1 : 0) != 0;
    s.verbose = ReadInt(iniPath, L"Verbose", s.verbose ? 1 : 0) != 0;
    return s;
}

std::wstring DefaultSettingsPath()
{
    return KnownFolder(FOLDERID_ProgramData, L"\\settings.ini");
}

std::wstring DefaultLogPath()
{
    return KnownFolder(FOLDERID_LocalAppData, L"\\plugin.log");
}

const Settings& GetSettings()
{
    return ProcessSettings();
}

void SetSettings(const Settings& settings)
{
    ProcessSettings() = settings;
}

void SetLogPath(const std::wstring& path)
{
    LogTarget target = MakeTarget(path);
    std::lock_guard<std::mutex> lock(g_logMutex);
    Target() = std::move(target);
}

void Log(const char* fmt, ...)
{
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    SYSTEMTIME t;
    GetLocalTime(&t);

    std::lock_guard<std::mutex> lock(g_logMutex);
    const LogTarget& target = Target();
    if (target.file.empty())
        return;
    if (!target.dir.empty())
        CreateDirectoryW(target.dir.c_str(), nullptr);

    WIN32_FILE_ATTRIBUTE_DATA attr;
    if (GetFileAttributesExW(target.file.c_str(), GetFileExInfoStandard, &attr)) {
        uint64_t size = (static_cast<uint64_t>(attr.nFileSizeHigh) << 32) | attr.nFileSizeLow;
        if (size > kMaxLogBytes)
            DeleteFileW(target.file.c_str());
    }

    FILE* f = nullptr;
    if (_wfopen_s(&f, target.file.c_str(), L"a") != 0)
        return;
    fprintf(f, "%04u-%02u-%02u %02u:%02u:%02u.%03u [%lu] %s\n", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute,
            t.wSecond, t.wMilliseconds, GetCurrentProcessId(), msg);
    fclose(f);
}
