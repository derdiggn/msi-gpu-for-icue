// SPDX-License-Identifier: GPL-2.0-or-later
//
// Standalone hardware test for the MSI GPU RGB controller (no iCUE needed).
//
//   gputest detect                 list GPUs and whether the card is supported
//   gputest dump                   read registers 0x20..0x47 (read only)
//   gputest static R G B           show one colour (streaming init sequence)
//   gputest direct FRAME_MS SECS   stream a colour fade like the plugin does
//   gputest off                    turn the LEDs off
//
// Nothing here writes the "save" register, so a reboot or MSI Center restores
// whatever was configured before.
#include "../src/msi_gpu.h"

#include <windows.h>
#include <timeapi.h>
#pragma comment(lib, "winmm.lib")

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void Hsv(double h, uint8_t& r, uint8_t& g, uint8_t& b)
{
    double x = 1.0 - std::fabs(std::fmod(h / 60.0, 2.0) - 1.0);
    double rr = 0, gg = 0, bb = 0;
    if (h < 60) { rr = 1; gg = x; }
    else if (h < 120) { rr = x; gg = 1; }
    else if (h < 180) { gg = 1; bb = x; }
    else if (h < 240) { gg = x; bb = 1; }
    else if (h < 300) { rr = x; bb = 1; }
    else { rr = 1; bb = x; }
    r = (uint8_t)(rr * 255);
    g = (uint8_t)(gg * 255);
    b = (uint8_t)(bb * 255);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        printf("usage: gputest detect | dump | static R G B | direct FRAME_MS SECS | off\n");
        return 2;
    }

    std::string log;
    auto gpu = MsiGpu::Detect(&log);
    printf("%s\n", log.c_str());
    if (!gpu)
        return 1;

    const char* cmd = argv[1];
    if (!strcmp(cmd, "detect"))
        return 0;

    if (!strcmp(cmd, "dump")) {
        for (int reg = 0x20; reg < 0x48; ++reg) {
            int v = gpu->ReadRegister((uint8_t)reg);
            printf("%02X=%s%02X%s", reg, v < 0 ? "!" : "", v < 0 ? 0 : v, (reg % 8 == 7) ? "\n" : " ");
        }
        return 0;
    }

    if (!strcmp(cmd, "static") && argc >= 5) {
        gpu->SetWriteDelayMs(20);
        Rgb color{(uint8_t)atoi(argv[2]), (uint8_t)atoi(argv[3]), (uint8_t)atoi(argv[4])};
        bool ok = gpu->EnterDirect(color);
        printf("static: %s\n", ok ? "ok" : "I2C write failed");
        return ok ? 0 : 1;
    }

    if (!strcmp(cmd, "direct") && argc >= 4) {
        unsigned frameMs = (unsigned)atoi(argv[2]);
        double seconds = atof(argv[3]);
        if (seconds <= 0) {
            printf("SECS must be greater than 0\n");
            return 2;
        }
        timeBeginPeriod(1);
        gpu->EnterDirect(Rgb{255, 0, 0});
        auto start = std::chrono::steady_clock::now();
        int frames = 0, failures = 0;
        for (;;) {
            double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            if (t > seconds)
                break;
            Rgb color;
            Hsv(std::fmod(t * 90.0, 360.0), color.r, color.g, color.b);
            if (!gpu->DirectFrame(color))
                ++failures;
            ++frames;
            if (frameMs)
                Sleep(frameMs);
        }
        timeEndPeriod(1);
        printf("direct: %d frames in %.0f s (%.1f fps), %d failed\n", frames, seconds, frames / seconds, failures);
        return 0;
    }

    if (!strcmp(cmd, "off")) {
        gpu->SetWriteDelayMs(20);
        bool ok = gpu->TurnOff();
        printf("off: %s\n", ok ? "ok" : "I2C write failed");
        return ok ? 0 : 1;
    }

    printf("unknown command\n");
    return 2;
}
