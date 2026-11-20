// SPDX-License-Identifier: GPL-2.0-or-later
//
// Abstraction of a single-colour RGB device, so the streaming logic in
// color_streamer.* can be tested without hardware.
#pragma once

#include <cstdint>

struct Rgb {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;

    bool operator==(const Rgb& o) const { return r == o.r && g == o.g && b == o.b; }
    bool operator!=(const Rgb& o) const { return !(*this == o); }
};

class LightDevice {
public:
    virtual ~LightDevice() = default;

    // Puts the controller into a state where every DirectFrame() is applied
    // immediately, then shows `color`. Returns false if any write failed.
    virtual bool EnterDirect(Rgb color) = 0;

    // Shows `color`. Only valid after a successful EnterDirect().
    virtual bool DirectFrame(Rgb color) = 0;

    // Switches the LEDs off (not persisted).
    virtual bool TurnOff() = 0;

    // Delay after each register write; 0 disables it.
    virtual void SetWriteDelayMs(unsigned ms) = 0;
};
