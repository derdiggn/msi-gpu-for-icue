// SPDX-License-Identifier: GPL-2.0-or-later
//
#include "helpers.h"

#include "../src/nvapi_i2c.h"
#include "../src/settings.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>

// --- framework -----------------------------------------------------------------

namespace t {
namespace {
int g_failures = 0;
}

void Fail(const char* file, int line, const std::string& message)
{
    ++g_failures;
    printf("    FAILED %s(%d): %s\n", file, line, message.c_str());
}

} // namespace t

// --- environment -------------------------------------------------------------

std::wstring ExeDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L'\\'));
}

std::wstring TestLogPath()
{
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    return std::wstring(tmp) + L"msigpu-test-" + std::to_wstring(GetCurrentProcessId()) + L".log";
}

TempDir::TempDir()
{
    static std::atomic<int> counter{0};
    wchar_t tmp[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmp);
    m_path = std::wstring(tmp) + L"msigpu-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
             std::to_wstring(counter++);
    std::filesystem::remove_all(m_path);
    std::filesystem::create_directories(m_path);
}

TempDir::~TempDir()
{
    std::error_code ec;
    std::filesystem::remove_all(m_path, ec);
}

void TempDir::Write(const wchar_t* name, const std::string& content) const
{
    std::ofstream f(File(name), std::ios::binary);
    f << content;
}

std::string ReadFileText(const std::wstring& path)
{
    std::ifstream f(path, std::ios::binary);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}

LogCapture::LogCapture()
{
    SetLogPath(m_dir.File(L"plugin.log"));
}

LogCapture::~LogCapture()
{
    SetLogPath(TestLogPath());
}

std::string LogCapture::Text() const
{
    return ReadFileText(m_dir.File(L"plugin.log"));
}

bool WaitUntil(const std::function<bool()>& pred, int timeoutMs)
{
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

// --- fake NvAPI ----------------------------------------------------------------

namespace fake_nv {
namespace {

std::wstring DllPath()
{
    return ExeDir() + L"\\fake_nvapi64.dll";
}

// The tests keep their own reference, so nvi2c::Reset() never unloads the
// fake and its state survives between tests.
HMODULE Module()
{
    static HMODULE module = LoadLibraryW(DllPath().c_str());
    return module;
}

template <typename Fn>
Fn Export(const char* name)
{
    return reinterpret_cast<Fn>(GetProcAddress(Module(), name));
}

} // namespace

void Configure(const FakeNvConfig& config)
{
    Export<FakeNvConfigureFn>("FakeNv_Configure")(&config);
}

std::vector<FakeNvTransfer> Transfers()
{
    auto fn = Export<FakeNvTransfersFn>("FakeNv_Transfers");
    std::vector<FakeNvTransfer> out(4096);
    int n = fn(out.data(), static_cast<int>(out.size()));
    out.resize(static_cast<size_t>(n));
    return out;
}

int InitCount()
{
    return Export<FakeNvCountFn>("FakeNv_InitCount")();
}

int EnumCount()
{
    return Export<FakeNvCountFn>("FakeNv_EnumCount")();
}

FakeNvConfig OneGpu(uint16_t device, uint16_t subDevice, uint16_t vendor, uint16_t subVendor)
{
    FakeNvConfig c;
    c.gpuCount = 1;
    c.deviceId[0] = (uint32_t(device) << 16) | vendor;
    c.subSystemId[0] = (uint32_t(subDevice) << 16) | subVendor;
    return c;
}

std::vector<std::pair<int, int>> Writes()
{
    std::vector<std::pair<int, int>> out;
    for (const auto& t : Transfers()) {
        if (t.write)
            out.emplace_back(t.reg, t.value);
    }
    return out;
}

} // namespace fake_nv

// --- doubles -----------------------------------------------------------------

void FakeDevice::SetWriteDelayMs(unsigned ms)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_delays.push_back(ms);
}

void FakeDevice::FailNext(int count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_failNext = count;
}

std::vector<FakeDevice::Call> FakeDevice::Calls()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_calls;
}

std::vector<unsigned> FakeDevice::Delays()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_delays;
}

bool FakeDevice::WaitForCalls(size_t count, int timeoutMs)
{
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_changed.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] { return m_calls.size() >= count; });
}

bool FakeDevice::Record(char kind, Rgb color)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_calls.push_back({kind, color});
    m_changed.notify_all();
    if (m_failNext > 0) {
        --m_failNext;
        return false;
    }
    return true;
}

Clock::time_point FakeClock::Now()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_nowCalls;
    return m_now;
}

int FakeClock::NowCalls()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_nowCalls;
}

void FakeClock::SleepFor(duration d)
{
    std::function<void()> hook;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_now += d;
        m_sleeps.push_back(d);
        hook = m_onSleep;
    }
    if (hook)
        hook();
}

void FakeClock::Advance(duration d)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_now += d;
}

std::vector<Clock::duration> FakeClock::Sleeps()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sleeps;
}

void FakeClock::OnSleep(std::function<void()> hook)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_onSleep = std::move(hook);
}

// --- main ----------------------------------------------------------------------

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;

    // Never touch the real driver or the user's log.
    nvi2c::SetLibraryName(ExeDir() + L"\\fake_nvapi64.dll");
    SetLogPath(TestLogPath());

    int run = 0;
    int failedTests = 0;
    for (const auto& c : t::Registry()) {
        if (filter && !strstr(c.name, filter))
            continue;
        ++run;
        int before = t::g_failures;
        printf("[ RUN  ] %s\n", c.name);
        try {
            c.fn();
        } catch (const t::Abort&) {
        } catch (const std::exception& e) {
            t::Fail(__FILE__, __LINE__, std::string("exception: ") + e.what());
        }
        bool ok = t::g_failures == before;
        failedTests += ok ? 0 : 1;
        printf("[ %s ] %s\n", ok ? " OK " : "FAIL", c.name);
    }
    printf("\n%d test(s), %d failed\n", run, failedTests);
    DeleteFileW(TestLogPath().c_str());
    return failedTests == 0 && run > 0 ? 0 : 1;
}
