// SPDX-License-Identifier: GPL-2.0-or-later
//
// settings.ini parsing and the log file.
#include "../src/settings.h"
#include "helpers.h"

#include <windows.h>

#include <filesystem>

namespace {

bool EndsWith(const std::wstring& s, const std::wstring& suffix)
{
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace

TEST(Settings_MissingFileGivesDefaults)
{
    TempDir dir;
    Settings s = LoadSettings(dir.File(L"absent.ini"));
    Settings d;
    CHECK_EQ(s.apiVersion, d.apiVersion);
    CHECK_EQ(s.apiVersion, 0x69);
    CHECK_EQ(s.features, 4u);
    CHECK_EQ(s.deviceType, 4);
    CHECK_EQ(s.ledCount, 1);
    CHECK_EQ(s.frameMs, 33u);
    CHECK_EQ(s.initDelayMs, 0u);
    CHECK(!s.turnOffOnExit);
    CHECK(!s.verbose);
}

TEST(Settings_ReadsAllKeys)
{
    TempDir dir;
    dir.Write(L"s.ini", "[plugin]\r\nApiVersion=106\r\nFeatures=84\r\nDeviceType=7\r\nLedCount=3\r\n"
                        "FrameMs=40\r\nInitDelayMs=20\r\nTurnOffOnExit=1\r\nVerbose=1\r\n");
    Settings s = LoadSettings(dir.File(L"s.ini"));
    CHECK_EQ(s.apiVersion, 106);
    CHECK_EQ(s.features, 4u);   // 84 = 0x54: only the implemented detached-mode bit survives
    CHECK_EQ(s.deviceType, 7);
    CHECK_EQ(s.ledCount, 3);
    CHECK_EQ(s.frameMs, 40u);
    CHECK_EQ(s.initDelayMs, 20u);
    CHECK(s.turnOffOnExit);
    CHECK(s.verbose);
}

TEST(Settings_ClampsOutOfRangeValues)
{
    TempDir dir;
    dir.Write(L"low.ini", "[plugin]\r\nLedCount=0\r\nFrameMs=1\r\n");
    Settings low = LoadSettings(dir.File(L"low.ini"));
    CHECK_EQ(low.ledCount, 1);
    CHECK_EQ(low.frameMs, 10u);

    dir.Write(L"high.ini", "[plugin]\r\nLedCount=99\r\nFrameMs=5000\r\nInitDelayMs=999\r\n");
    Settings high = LoadSettings(dir.File(L"high.ini"));
    CHECK_EQ(high.ledCount, 50);
    CHECK_EQ(high.frameMs, 1000u);
    CHECK_EQ(high.initDelayMs, 100u);
}

TEST(Settings_IgnoresOtherSections)
{
    TempDir dir;
    dir.Write(L"s.ini", "[other]\r\nFrameMs=50\r\n");
    CHECK_EQ(LoadSettings(dir.File(L"s.ini")).frameMs, 33u);
}

TEST(Settings_DefaultPaths)
{
    std::wstring ini = DefaultSettingsPath();
    std::wstring log = DefaultLogPath();
    CHECK(EndsWith(ini, L"\\MsiGpuForIcue\\settings.ini"));
    CHECK(EndsWith(log, L"\\MsiGpuForIcue\\plugin.log"));
    CHECK(ini.size() > 30 && ini[1] == L':');
    CHECK(log.size() > 30 && log[1] == L':');
}

TEST(Settings_ProcessSettingsCanBeReplaced)
{
    Settings original = GetSettings();
    Settings s;
    s.frameMs = 77;
    SetSettings(s);
    CHECK_EQ(GetSettings().frameMs, 77u);
    SetSettings(original);
}

TEST(Log_CreatesFolderAndAppends)
{
    TempDir dir;
    std::wstring file = dir.Path() + L"\\sub\\plugin.log";
    SetLogPath(file);
    Log("hello %d", 42);
    Log("second %s", "line");
    SetLogPath(TestLogPath());

    std::string text = ReadFileText(file);
    CHECK(text.find("] hello 42\r\n") != std::string::npos);   // text mode: CRLF
    CHECK(text.find("] second line\r\n") != std::string::npos);
    CHECK(text.find("[" + std::to_string(GetCurrentProcessId()) + "]") != std::string::npos);
}

TEST(Log_StartsOverAfterOneMegabyte)
{
    TempDir dir;
    dir.Write(L"plugin.log", std::string(1024 * 1024 + 1, 'x'));
    SetLogPath(dir.File(L"plugin.log"));
    Log("fresh");
    SetLogPath(TestLogPath());
    std::string text = ReadFileText(dir.File(L"plugin.log"));
    CHECK(text.size() < 100);
    CHECK(text.find("fresh") != std::string::npos);
}

TEST(Log_KeepsSmallFile)
{
    TempDir dir;
    dir.Write(L"plugin.log", "old\n");
    SetLogPath(dir.File(L"plugin.log"));
    Log("new");
    SetLogPath(TestLogPath());
    std::string text = ReadFileText(dir.File(L"plugin.log"));
    CHECK(text.rfind("old\n", 0) == 0);
    CHECK(text.find("new") != std::string::npos);
}

TEST(Log_UnwritablePathIsIgnored)
{
    TempDir dir;
    dir.Write(L"file", "not a folder");
    SetLogPath(dir.File(L"file") + L"\\plugin.log");   // parent is a file
    Log("dropped");
    SetLogPath(TestLogPath());
    CHECK(!std::filesystem::exists(dir.File(L"file") + L"\\plugin.log"));
}

TEST(Settings_UnimplementedFeatureBitsAreDropped)
{
    TempDir dir;
    dir.Write(L"s.ini", "[plugin]\r\nFeatures=127\r\n");
    CHECK_EQ(LoadSettings(dir.File(L"s.ini")).features, 4u);
    dir.Write(L"off.ini", "[plugin]\r\nFeatures=0\r\n");
    CHECK_EQ(LoadSettings(dir.File(L"off.ini")).features, 0u);
}

TEST(Settings_EmptyPathGivesDefaults)
{
    // An unresolvable ProgramData must not fall back to win.ini or the drive root.
    Settings s = LoadSettings(L"");
    CHECK_EQ(s.frameMs, 33u);
    CHECK_EQ(s.features, 4u);
}

TEST(Log_EmptyPathWritesNothing)
{
    SetLogPath(L"");
    Log("goes nowhere");
    SetLogPath(TestLogPath());
}

TEST(Log_FileWithoutFolder)
{
    // A bare file name (relative to the working directory) needs no folder.
    TempDir dir;
    wchar_t cwd[MAX_PATH] = {};
    GetCurrentDirectoryW(MAX_PATH, cwd);
    SetCurrentDirectoryW(dir.Path().c_str());
    SetLogPath(L"bare.log");
    Log("bare");
    SetLogPath(TestLogPath());
    SetCurrentDirectoryW(cwd);
    CHECK(ReadFileText(dir.File(L"bare.log")).find("bare") != std::string::npos);
}
