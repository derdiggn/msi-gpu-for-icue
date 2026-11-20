// SPDX-License-Identifier: GPL-2.0-or-later
//
// Loads MsiGpuForIcue.dll the way iCUE's plugin host does and drives it:
// announce -> device info/view -> 5 s colour fade -> unsubscribe.
//
//   plugintest [path\to\MsiGpuForIcue.dll]
#include "../src/cue_plugin_abi.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

static std::atomic<bool> g_announced{false};
static std::string g_id;

static int32_t OnConnection(void* context, const char* deviceId, int32_t connected)
{
    printf("callback: context=%p device=%s connected=%d\n", context, deviceId, connected);
    if (connected) {
        g_id = deviceId;
        g_announced = true;
    }
    return 0;
}

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "MsiGpuForIcue.dll";
    HMODULE dll = LoadLibraryA(path);
    if (!dll) {
        printf("LoadLibrary(%s) failed: %lu\n", path, GetLastError());
        return 1;
    }

    auto getApi = reinterpret_cast<int32_t (*)()>(GetProcAddress(dll, "CorsairPluginGetAPIVersion"));
    auto getFeatures = reinterpret_cast<uint32_t (*)()>(GetProcAddress(dll, "CorsairPluginGetFeatures"));
    auto getInstance = reinterpret_cast<cue_abi::Instance* (*)()>(GetProcAddress(dll, "CorsairPluginGetInstance"));
    auto freeInstance = reinterpret_cast<void (*)(cue_abi::Instance*)>(GetProcAddress(dll, "CorsairPluginFreeInstance"));
    if (!getApi || !getFeatures || !getInstance || !freeInstance) {
        printf("missing exports\n");
        return 1;
    }

    printf("api 0x%X features 0x%X\n", getApi(), getFeatures());
    cue_abi::Instance* inst = getInstance();

    int ctx = 42;
    inst->subscribe(&ctx, OnConnection);
    for (int i = 0; i < 100 && !g_announced; ++i)
        Sleep(50);
    if (!g_announced) {
        printf("device was not announced (see %%LOCALAPPDATA%%\\MsiGpuForIcue\\plugin.log)\n");
        inst->unsubscribe();
        return 1;
    }

    cue_abi::DeviceInfo* info = inst->getDeviceInfo(g_id.c_str());
    if (!info) {
        printf("getDeviceInfo returned null\n");
        return 1;
    }
    printf("name=%s type=%d id=%s views=%d leds=%d\n", info->deviceName, info->deviceType, info->deviceId,
           info->numberOfDeviceView, info->ledPositions->numberOfLed);
    printf("thumbnail=%s %s\npromo=%s %s\n", info->thumbnail->path, info->thumbnail->hash, info->promoImage->path,
           info->promoImage->hash);
    int ledCount = info->ledPositions->numberOfLed;
    int firstLed = info->ledPositions->ledPosition[0].ledId;
    inst->freeDeviceInfo(info);

    cue_abi::DeviceView* view = inst->getDeviceView(g_id.c_str(), 0);
    if (view) {
        printf("view=%s %s\n", view->view->path, view->view->hash);
        for (int i = 0; i < view->ledView->numberOfLed; ++i)
            printf("  led %d: %s\n", view->ledView->view[i].ledId, view->ledView->view[i].path);
        inst->freeDeviceView(view);
    }

    printf("fading for 5 s...\n");
    auto start = std::chrono::steady_clock::now();
    cue_abi::LedColor colors[50];
    for (;;) {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (t > 5.0)
            break;
        for (int i = 0; i < ledCount; ++i) {
            colors[i].ledId = firstLed + i;
            colors[i].r = int(127.5 + 127.5 * std::sin(t * 2.0));
            colors[i].g = int(127.5 + 127.5 * std::sin(t * 2.0 + 2.1));
            colors[i].b = int(127.5 + 127.5 * std::sin(t * 2.0 + 4.2));
        }
        inst->setLedsColors(g_id.c_str(), ledCount, colors);
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }

    for (int i = 0; i < ledCount; ++i)
        colors[i] = {firstLed + i, 0, 255, 0};
    inst->setLedsColors(g_id.c_str(), ledCount, colors);
    Sleep(300);
    printf("final colour green; unsubscribing\n");

    inst->setMode(static_cast<int32_t>(cue_abi::Mode::Detached));
    inst->unsubscribe();
    freeInstance(inst);
    FreeLibrary(dll);
    printf("done\n");
    return 0;
}
