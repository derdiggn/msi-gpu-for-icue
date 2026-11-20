// SPDX-License-Identifier: GPL-2.0-or-later
//
// Driver for the RGB controller on MSI GeForce RTX 50 cards
// ("MSI GPU v2", ITE controller at I2C address 0x68 on GPU port 1).
//
// Register map from OpenRGB's MSIGPUv2Controller (GPL-2.0-or-later). The
// streaming sequence follows SignalRGB's MSI GPU script and was verified on an
// RTX 5080 SUPRIM LIQUID SOC:
//   - the colour registers are only applied together with a mode write,
//   - with the effect speed register left at its default (2) the controller
//     fades slowly and drops most updates; with speed 0 it follows at >25 fps.
// See docs/protocol.md for the full description.
#pragma once

#include "card_style.h"
#include "light_device.h"
#include "nvapi_i2c.h"

#include <cstdint>
#include <memory>
#include <string>

class MsiGpu final : public LightDevice {
public:
    // Finds the first supported MSI card. `log` (optional) receives a human
    // readable description of what was found, or why nothing was found.
    static std::unique_ptr<MsiGpu> Detect(std::string* log);

    const std::string& Name() const { return m_name; }
    const nvi2c::GpuInfo& Info() const { return m_info; }
    CardStyle Style() const { return m_style; }

    // Static mode, full brightness, effect speed 0, then DirectFrame(color).
    bool EnterDirect(Rgb color) override;

    // Mode register, then the three colour registers.
    bool DirectFrame(Rgb color) override;

    bool TurnOff() override;

    void SetWriteDelayMs(unsigned ms) override { m_writeDelayMs = ms; }

    // Raw register read, -1 on failure (diagnostics only).
    int ReadRegister(uint8_t reg);

private:
    MsiGpu(const nvi2c::GpuInfo& info, std::string name, CardStyle style)
        : m_info(info), m_name(std::move(name)), m_style(style)
    {
    }

    bool Write(uint8_t reg, uint8_t value);

    nvi2c::GpuInfo m_info;
    std::string m_name;
    CardStyle m_style;
    unsigned m_writeDelayMs = 0;
};
