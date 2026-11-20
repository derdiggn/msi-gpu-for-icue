// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared test doubles and utilities.
#pragma once

#include "../src/color_streamer.h"
#include "../src/light_device.h"
#include "fake_nvapi/fake_nvapi.h"
#include "test_framework.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// --- environment -------------------------------------------------------------

// Folder of tests.exe (images and fake_nvapi64.dll are copied next to it).
std::wstring ExeDir();

// Log file the test run writes to instead of the user's plugin.log.
std::wstring TestLogPath();

// Fresh, empty folder below %TEMP% that is removed when the object dies.
class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const std::wstring& Path() const { return m_path; }
    std::wstring File(const wchar_t* name) const { return m_path + L"\\" + name; }
    void Write(const wchar_t* name, const std::string& content) const;

private:
    std::wstring m_path;
};

std::string ReadFileText(const std::wstring& path);

// Redirects the plugin log into a temporary file for its lifetime.
class LogCapture {
public:
    LogCapture();
    ~LogCapture();
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::string Text() const;

private:
    TempDir m_dir;
};

// Polls `pred` every millisecond until it returns true or `timeoutMs` passes.
bool WaitUntil(const std::function<bool()>& pred, int timeoutMs = 3000);

// --- fake NvAPI ----------------------------------------------------------------

namespace fake_nv {

// Replaces the fake's configuration and clears its transfer log.
void Configure(const FakeNvConfig& config);

std::vector<FakeNvTransfer> Transfers();

int InitCount();
int EnumCount();

// A configuration with one NVIDIA GPU of the given PCI ids.
FakeNvConfig OneGpu(uint16_t device, uint16_t subDevice, uint16_t vendor = 0x10DE, uint16_t subVendor = 0x1462);

// The RTX 5080 SUPRIM LIQUID SOC the plugin was developed with.
inline FakeNvConfig Rtx5080SuprimLiquid() { return OneGpu(0x2C02, 0x5312); }

// (register, value) of every write, in order.
std::vector<std::pair<int, int>> Writes();

} // namespace fake_nv

// --- LightDevice / Clock doubles ---------------------------------------------

class FakeDevice final : public LightDevice {
public:
    struct Call {
        char kind;   // 'E' EnterDirect, 'F' DirectFrame, 'O' TurnOff
        Rgb color;
    };

    bool EnterDirect(Rgb color) override { return Record('E', color); }
    bool DirectFrame(Rgb color) override { return Record('F', color); }
    bool TurnOff() override { return Record('O', Rgb{}); }
    void SetWriteDelayMs(unsigned ms) override;

    void FailNext(int count);
    std::vector<Call> Calls();
    std::vector<unsigned> Delays();
    bool WaitForCalls(size_t count, int timeoutMs = 3000);

private:
    bool Record(char kind, Rgb color);

    std::mutex m_mutex;
    std::condition_variable m_changed;
    std::vector<Call> m_calls;
    std::vector<unsigned> m_delays;
    int m_failNext = 0;
};

// Time only moves through SleepFor() and Advance(), so tests are deterministic.
class FakeClock final : public Clock {
public:
    time_point Now() override;
    void SleepFor(duration d) override;

    void Advance(duration d);
    std::vector<duration> Sleeps();
    int NowCalls();

    // Called (outside the clock's lock) after every SleepFor().
    void OnSleep(std::function<void()> hook);

private:
    std::mutex m_mutex;
    time_point m_now{};
    int m_nowCalls = 0;
    std::vector<duration> m_sleeps;
    std::function<void()> m_onSleep;
};
