// SPDX-License-Identifier: GPL-2.0-or-later
//
#include "msi_gpu.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>

namespace {

constexpr uint8_t kAddress = 0x68;

constexpr uint16_t kNvidiaVendor = 0x10DE;
constexpr uint16_t kMsiVendor = 0x1462;

enum Reg : uint8_t {
    RegMode = 0x22,
    RegFlags = 0x2D,      // written as 0 on init (SignalRGB), meaning unknown
    RegUnknown = 0x2E,    // written as 0 before every mode change (OpenRGB)
    RegRed = 0x30,
    RegGreen = 0x31,
    RegBlue = 0x32,
    RegBrightness = 0x36,
    RegSpeed = 0x38,      // effect speed; also how slowly colour changes fade
};

enum ModeValue : uint8_t {
    ModeOff = 0x01,
    ModeStatic = 0x13,
    ModeIdle = 0x1C,      // used as "start programming"
};

constexpr uint8_t kBrightnessMax = 5 * 20;   // 5 steps of 20, i.e. 100 %

struct KnownCard {
    uint16_t device;
    uint16_t subDevice;
    const char* name;
    CardStyle style;   // which pictures iCUE shows
};

// MSI RTX 50 cards that OpenRGB drives with the v2 controller at 0x68.
constexpr KnownCard kKnownCards[] = {
    {0x2C02, 0x5315, "MSI GeForce RTX 5080 Gaming Trio OC", CardStyle::GamingTrio},
    {0x2C02, 0x5314, "MSI GeForce RTX 5080 VANGUARD SOC", CardStyle::Vanguard},
    {0x2C02, 0x5313, "MSI GeForce RTX 5080 SUPRIM SOC", CardStyle::Suprim},
    {0x2C02, 0x5312, "MSI GeForce RTX 5080 SUPRIM LIQUID SOC", CardStyle::SuprimLiquid},
    {0x2B85, 0x5303, "MSI GeForce RTX 5090 Gaming Trio OC", CardStyle::GamingTrio},
    {0x2B85, 0x5302, "MSI GeForce RTX 5090 VANGUARD SOC", CardStyle::Vanguard},
    {0x2B85, 0x5301, "MSI GeForce RTX 5090 SUPRIM SOC", CardStyle::Suprim},
    {0x2B85, 0x5300, "MSI GeForce RTX 5090 SUPRIM LIQUID SOC", CardStyle::SuprimLiquid},
    {0x2C05, 0x5315, "MSI GeForce RTX 5070 Ti Gaming Trio OC Plus", CardStyle::GamingTrio},
    {0x2C05, 0x5314, "MSI GeForce RTX 5070 Ti VANGUARD SOC", CardStyle::Vanguard},
    {0x2F04, 0x5322, "MSI GeForce RTX 5070 Gaming Trio", CardStyle::GamingTrio},
    {0x2D04, 0x5354, "MSI GeForce RTX 5060 Ti Gaming OC", CardStyle::Gaming},
    {0x2D04, 0x5353, "MSI GeForce RTX 5060 Ti 16G Gaming Trio OC", CardStyle::GamingTrio},
    {0x2D05, 0x5372, "MSI GeForce RTX 5060 8G Gaming Trio OC", CardStyle::GamingTrio},
};

std::string Hex4(uint16_t v)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%04X", v);
    return buf;
}

const KnownCard* FindCard(const nvi2c::GpuInfo& gpu)
{
    if (gpu.vendor != kNvidiaVendor || gpu.subVendor != kMsiVendor)
        return nullptr;
    for (const auto& card : kKnownCards) {
        if (card.device == gpu.device && card.subDevice == gpu.subDevice)
            return &card;
    }
    return nullptr;
}

} // namespace

std::unique_ptr<MsiGpu> MsiGpu::Detect(std::string* log)
{
    std::string message;
    std::unique_ptr<MsiGpu> result;

    if (!nvi2c::Initialize(&message)) {
        if (log)
            *log = message;
        return nullptr;
    }

    std::string seen;
    for (const auto& gpu : nvi2c::EnumerateGpus()) {
        seen += " " + Hex4(gpu.vendor) + ":" + Hex4(gpu.device) + " sub " + Hex4(gpu.subVendor) + ":" +
                Hex4(gpu.subDevice) + ";";
        const KnownCard* card = FindCard(gpu);
        if (card && !result) {
            result.reset(new MsiGpu(gpu, card->name, card->style));
            message = std::string("found ") + card->name;
        }
    }

    if (!result)
        message = "no supported MSI card found";
    if (log)
        *log = message + "; GPUs:" + (seen.empty() ? std::string(" none") : seen);
    return result;
}

bool MsiGpu::Write(uint8_t reg, uint8_t value)
{
    bool ok = nvi2c::WriteByte(m_info.handle, kAddress, reg, value);
    if (m_writeDelayMs)
        Sleep(m_writeDelayMs);
    return ok;
}

int MsiGpu::ReadRegister(uint8_t reg)
{
    return nvi2c::ReadByte(m_info.handle, kAddress, reg);
}

bool MsiGpu::EnterDirect(Rgb color)
{
    bool ok = true;
    ok &= Write(RegUnknown, 0x00);
    ok &= Write(RegFlags, 0x00);
    ok &= Write(RegMode, ModeStatic);
    ok &= Write(RegBrightness, kBrightnessMax);
    ok &= Write(RegSpeed, 0x00);
    ok &= DirectFrame(color);
    return ok;
}

bool MsiGpu::DirectFrame(Rgb color)
{
    bool ok = true;
    ok &= Write(RegMode, ModeStatic);
    ok &= Write(RegRed, color.r);
    ok &= Write(RegGreen, color.g);
    ok &= Write(RegBlue, color.b);
    return ok;
}

bool MsiGpu::TurnOff()
{
    bool ok = true;
    ok &= Write(RegUnknown, 0x00);
    ok &= Write(RegMode, ModeIdle);
    ok &= Write(RegMode, ModeOff);
    return ok;
}
