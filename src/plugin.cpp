// SPDX-License-Identifier: GPL-2.0-or-later
//
// iCUE 5 device plugin that exposes the RGB lighting of MSI GeForce RTX 50
// cards as a normal iCUE device.
//
// Life cycle as driven by iCUEDevicePluginHost.exe:
//   CorsairPluginGetAPIVersion / GetFeatures / GetInstance
//   subscribe(context, callback)   -> worker thread detects the card (retrying
//                                     with backoff) and announces it
//   getDeviceInfo / getDeviceView  -> device description and pictures
//   setLedsColors (many times)     -> ColorStreamer writes to the card
//   unsubscribe / FreeInstance
// See docs/architecture.md.
#include "abi_guard.h"
#include "color_streamer.h"
#include "cue_plugin_abi.h"
#include "device_description.h"
#include "msi_gpu.h"
#include "settings.h"
#include "version.h"

#include <windows.h>

#include <algorithm>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

EXTERN_C IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr std::chrono::milliseconds kMaxDetectRetry{60000};

// Lock order: lifecycle -> announce -> mutex. No lock but `announce` is held
// while calling into iCUE.
struct PluginState {
    std::mutex lifecycle;   // serialises subscribe / unsubscribe, held across join()
    std::mutex announce;    // held while the worker calls iCUE's callback
    std::mutex mutex;       // guards everything below
    std::condition_variable wake;   // interrupts the detection backoff
    std::unique_ptr<MsiGpu> gpu;
    std::unique_ptr<ColorStreamer> streamer;
    std::string deviceId;
    void* context = nullptr;
    cue_abi::ConnectionCallback callback = nullptr;
    std::thread worker;
    bool stopping = false;
};

// Deliberately never destroyed: if the host exits without unsubscribe, the
// destructor of a still joinable std::thread would call std::terminate().
PluginState& g_state = *new PluginState;
SystemClock g_clock;

std::wstring PluginDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(reinterpret_cast<HINSTANCE>(&__ImageBase), path, MAX_PATH);
    std::wstring dir(path);
    return dir.substr(0, dir.find_last_of(L'\\'));
}

StreamerOptions OptionsFrom(const Settings& s)
{
    StreamerOptions o;
    o.frame = std::chrono::milliseconds(s.frameMs);
    o.initDelayMs = s.initDelayMs;
    o.turnOffOnExit = s.turnOffOnExit;
    o.verbose = s.verbose;
    return o;
}

// Detects the card until it is found or unsubscribe is called. NvAPI can
// fail right after login while the driver is still starting.
std::unique_ptr<MsiGpu> DetectWithRetry()
{
    std::chrono::milliseconds delay(GetSettings().detectRetryMs);
    std::string lastLog;
    std::unique_ptr<MsiGpu> gpu;
    for (;;) {
        std::string log;
        gpu = MsiGpu::Detect(&log);
        if (log != lastLog)
            Log("detect: %s", log.c_str());   // repeated identical results are logged once
        lastLog = log;
        if (gpu)
            break;

        std::unique_lock<std::mutex> lock(g_state.mutex);
        if (g_state.wake.wait_for(lock, delay, [] { return g_state.stopping; }))
            break;
        delay = std::min(delay * 2, kMaxDetectRetry);
    }
    return gpu;   // null when unsubscribed before a card was found
}

// Detects the card, announces it and streams colours until unsubscribe.
void WorkerMain()
{
    std::unique_ptr<MsiGpu> gpu = DetectWithRetry();
    if (!gpu)
        return;

    std::string id = device::DeviceIdFor(gpu->Info());
    ColorStreamer* streamer;
    {
        // Holding `announce` across the check and the callback guarantees
        // that no announcement arrives after unsubscribe() has returned.
        std::lock_guard<std::mutex> announce(g_state.announce);
        void* context;
        cue_abi::ConnectionCallback callback;
        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            if (g_state.stopping)
                return;
            g_state.streamer = std::make_unique<ColorStreamer>(*gpu, g_clock, OptionsFrom(GetSettings()));
            g_state.gpu = std::move(gpu);
            g_state.deviceId = id;
            streamer = g_state.streamer.get();
            context = g_state.context;
            callback = g_state.callback;
        }
        if (callback) {
            Log("announcing device %s", id.c_str());
            callback(context, id.c_str(), 1);
        }
    }
    streamer->Run();
}

bool IsOurDevice(const char* deviceId)
{
    return deviceId && !g_state.deviceId.empty() && g_state.deviceId == deviceId;
}

cue_abi::DeviceInfo* GetDeviceInfo(const char* deviceId)
{
    return Guarded("getDeviceInfo", static_cast<cue_abi::DeviceInfo*>(nullptr), [&] {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (!IsOurDevice(deviceId)) {
            Log("getDeviceInfo(%s): unknown device", deviceId ? deviceId : "null");
            return static_cast<cue_abi::DeviceInfo*>(nullptr);
        }
        const Settings& cfg = GetSettings();
        Log("getDeviceInfo(%s): type %d, %d led(s)", deviceId, cfg.deviceType, cfg.ledCount);
        return device::BuildDeviceInfo(g_state.gpu->Name(), g_state.deviceId, cfg.deviceType, cfg.ledCount,
                                       g_state.gpu->Style(), PluginDir());
    });
}

cue_abi::DeviceView* GetDeviceView(const char* deviceId, int32_t index)
{
    return Guarded("getDeviceView", static_cast<cue_abi::DeviceView*>(nullptr), [&] {
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (!IsOurDevice(deviceId) || index != 0)
            return static_cast<cue_abi::DeviceView*>(nullptr);
        return device::BuildDeviceView(GetSettings().ledCount, g_state.gpu->Style(), PluginDir());
    });
}

bool SetLedsColors(const char* deviceId, int32_t size, cue_abi::LedColor* colors)
{
    return Guarded("setLedsColors", false, [&] {
        Rgb color;
        if (!device::AverageColor(colors, size, GetSettings().ledCount, color))
            return false;
        std::lock_guard<std::mutex> lock(g_state.mutex);
        if (!IsOurDevice(deviceId))
            return false;
        g_state.streamer->Submit(color);
        return true;
    });
}

void Subscribe(void* context, cue_abi::ConnectionCallback callback)
{
    GuardedVoid("subscribe", [&] {
        Log("subscribe");
        std::lock_guard<std::mutex> life(g_state.lifecycle);
        std::lock_guard<std::mutex> lock(g_state.mutex);
        g_state.context = context;
        g_state.callback = callback;
        // The worker only ends through unsubscribe(), which also joins it, so
        // a joinable worker is a running one.
        if (!g_state.worker.joinable()) {
            g_state.stopping = false;
            g_state.worker = std::thread([] { GuardedVoid("worker", WorkerMain); });
        }
    });
}

void Unsubscribe()
{
    GuardedVoid("unsubscribe", [] {
        Log("unsubscribe");
        std::lock_guard<std::mutex> life(g_state.lifecycle);
        std::thread worker;
        {
            std::lock_guard<std::mutex> lock(g_state.mutex);
            g_state.stopping = true;
            g_state.callback = nullptr;
            g_state.context = nullptr;
            if (g_state.streamer)
                g_state.streamer->Stop();
            worker = std::move(g_state.worker);
        }
        g_state.wake.notify_all();
        { std::lock_guard<std::mutex> barrier(g_state.announce); }   // let an announcement in flight finish
        if (worker.joinable())
            worker.join();

        std::lock_guard<std::mutex> lock(g_state.mutex);
        g_state.streamer.reset();
        g_state.gpu.reset();
        g_state.deviceId.clear();
    });
}

void SetMode(int32_t mode)
{
    GuardedVoid("setMode", [&] { Log("setMode(%d)", mode); });
}

void FreeDeviceInfo(cue_abi::DeviceInfo* info)
{
    GuardedVoid("freeDeviceInfo", [&] { device::FreeDeviceInfo(info); });
}

void FreeDeviceView(cue_abi::DeviceView* view)
{
    GuardedVoid("freeDeviceView", [&] { device::FreeDeviceView(view); });
}

} // namespace

extern "C" {

__declspec(dllexport) int32_t CorsairPluginGetAPIVersion()
{
    return Guarded("getApiVersion", Settings{}.apiVersion, [] {
        static std::once_flag once;
        std::call_once(once, [] {
            char exe[MAX_PATH] = {};
            GetModuleFileNameA(nullptr, exe, MAX_PATH);
            Log("msi-gpu-for-icue %s loaded by %s", MSIGPU_VERSION_STRING, exe);
        });
        return GetSettings().apiVersion;
    });
}

__declspec(dllexport) uint32_t CorsairPluginGetFeatures()
{
    return Guarded("getFeatures", Settings{}.features, [] { return GetSettings().features; });
}

__declspec(dllexport) cue_abi::Instance* CorsairPluginGetInstance()
{
    return Guarded("getInstance", static_cast<cue_abi::Instance*>(nullptr), [] {
        Log("getInstance (api 0x%X, features 0x%X)", GetSettings().apiVersion, GetSettings().features);
        auto* inst = new cue_abi::Instance{};
        inst->getDeviceInfo = GetDeviceInfo;
        inst->setLedsColors = SetLedsColors;
        inst->subscribe = Subscribe;
        inst->unsubscribe = Unsubscribe;
        inst->getDeviceView = GetDeviceView;
        inst->freeDeviceInfo = FreeDeviceInfo;
        inst->freeDeviceView = FreeDeviceView;
        inst->setMode = SetMode;
        return inst;
    });
}

__declspec(dllexport) void CorsairPluginFreeInstance(cue_abi::Instance* instance)
{
    GuardedVoid("freeInstance", [&] {
        Log("freeInstance");
        Unsubscribe();   // the worker must be gone before the DLL can be unloaded
        delete instance;
    });
}

} // extern "C"
