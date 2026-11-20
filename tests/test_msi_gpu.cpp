// SPDX-License-Identifier: GPL-2.0-or-later
//
// MsiGpu: card detection and the exact register sequences on the bus.
#include "../src/msi_gpu.h"
#include "helpers.h"

#include <utility>
#include <vector>

namespace {

using Writes = std::vector<std::pair<int, int>>;

std::unique_ptr<MsiGpu> DetectWith(const FakeNvConfig& config, std::string* log = nullptr)
{
    nvi2c::Reset();
    fake_nv::Configure(config);
    return MsiGpu::Detect(log);
}

std::unique_ptr<MsiGpu> Card()
{
    auto gpu = DetectWith(fake_nv::Rtx5080SuprimLiquid());
    fake_nv::Configure(fake_nv::Rtx5080SuprimLiquid());   // clear the log
    return gpu;
}

} // namespace

TEST(MsiGpu_DetectReportsNvapiFailure)
{
    FakeNvConfig c;
    c.initStatus = -1;
    std::string log;
    CHECK(DetectWith(c, &log) == nullptr);
    CHECK_EQ(log, std::string("NvAPI_Initialize failed with status -1"));
    CHECK(DetectWith(c) == nullptr);
}

TEST(MsiGpu_DetectWithoutGpus)
{
    std::string log;
    CHECK(DetectWith(FakeNvConfig{}, &log) == nullptr);
    CHECK_EQ(log, std::string("no supported MSI card found; GPUs: none"));
}

TEST(MsiGpu_DetectIgnoresUnsupportedCards)
{
    FakeNvConfig c;
    c.gpuCount = 3;
    c.deviceId[0] = 0x2C021002;      // not NVIDIA
    c.subSystemId[0] = 0x53121462;
    c.deviceId[1] = 0x2C0210DE;      // NVIDIA, but not an MSI board
    c.subSystemId[1] = 0x53123842;
    c.deviceId[2] = 0x2C0210DE;      // MSI, unknown model
    c.subSystemId[2] = 0x99991462;
    std::string log;
    CHECK(DetectWith(c, &log) == nullptr);
    CHECK_EQ(log, std::string("no supported MSI card found; GPUs: 1002:2C02 sub 1462:5312; "
                              "10DE:2C02 sub 3842:5312; 10DE:2C02 sub 1462:9999;"));
}

TEST(MsiGpu_DetectFindsFirstSupportedCard)
{
    FakeNvConfig c;
    c.gpuCount = 3;
    c.deviceId[0] = 0x2C0210DE;
    c.subSystemId[0] = 0x00001462;
    c.deviceId[1] = 0x2C0210DE;
    c.subSystemId[1] = 0x53121462;
    c.deviceId[2] = 0x2B8510DE;      // also supported, but second
    c.subSystemId[2] = 0x53001462;
    std::string log;
    auto gpu = DetectWith(c, &log);
    REQUIRE(gpu != nullptr);
    CHECK_EQ(gpu->Name(), std::string("MSI GeForce RTX 5080 SUPRIM LIQUID SOC"));
    CHECK(gpu->Info().handle == reinterpret_cast<void*>(kFakeGpuHandleBase + 1));
    CHECK_EQ(gpu->Info().subDevice, uint16_t(0x5312));
    CHECK_EQ(log, std::string("found MSI GeForce RTX 5080 SUPRIM LIQUID SOC; GPUs: 10DE:2C02 sub 1462:0000; "
                              "10DE:2C02 sub 1462:5312; 10DE:2B85 sub 1462:5300;"));
}

TEST(MsiGpu_DetectKnowsEveryListedCard)
{
    struct Ids {
        uint16_t device;
        uint16_t subDevice;
        CardStyle style;
    };
    const Ids cards[] = {
        {0x2C02, 0x5315, CardStyle::GamingTrio}, {0x2C02, 0x5314, CardStyle::Vanguard},
        {0x2C02, 0x5313, CardStyle::Suprim},     {0x2C02, 0x5312, CardStyle::SuprimLiquid},
        {0x2B85, 0x5303, CardStyle::GamingTrio}, {0x2B85, 0x5302, CardStyle::Vanguard},
        {0x2B85, 0x5301, CardStyle::Suprim},     {0x2B85, 0x5300, CardStyle::SuprimLiquid},
        {0x2C05, 0x5315, CardStyle::GamingTrio}, {0x2C05, 0x5314, CardStyle::Vanguard},
        {0x2F04, 0x5322, CardStyle::GamingTrio}, {0x2D04, 0x5354, CardStyle::Gaming},
        {0x2D04, 0x5353, CardStyle::GamingTrio}, {0x2D05, 0x5372, CardStyle::GamingTrio},
    };
    for (const auto& card : cards) {
        auto gpu = DetectWith(fake_nv::OneGpu(card.device, card.subDevice));
        REQUIRE(gpu != nullptr);
        CHECK(gpu->Style() == card.style);
    }
}

TEST(MsiGpu_EnterDirectSequence)
{
    auto gpu = Card();
    REQUIRE(gpu != nullptr);
    CHECK(gpu->EnterDirect(Rgb{10, 20, 30}));
    Writes expected = {{0x2E, 0x00}, {0x2D, 0x00}, {0x22, 0x13}, {0x36, 100}, {0x38, 0x00},
                       {0x22, 0x13}, {0x30, 10},   {0x31, 20},   {0x32, 30}};
    CHECK(fake_nv::Writes() == expected);
    for (const auto& t : fake_nv::Transfers())
        CHECK_EQ(t.devAddress, uint8_t(0xD0));
}

TEST(MsiGpu_DirectFrameSequence)
{
    auto gpu = Card();
    REQUIRE(gpu != nullptr);
    CHECK(gpu->DirectFrame(Rgb{255, 128, 0}));
    Writes expected = {{0x22, 0x13}, {0x30, 255}, {0x31, 128}, {0x32, 0}};
    CHECK(fake_nv::Writes() == expected);
}

TEST(MsiGpu_TurnOffSequence)
{
    auto gpu = Card();
    REQUIRE(gpu != nullptr);
    CHECK(gpu->TurnOff());
    Writes expected = {{0x2E, 0x00}, {0x22, 0x1C}, {0x22, 0x01}};
    CHECK(fake_nv::Writes() == expected);
}

TEST(MsiGpu_NeverWritesSaveRegister)
{
    auto gpu = Card();
    REQUIRE(gpu != nullptr);
    gpu->EnterDirect(Rgb{1, 2, 3});
    gpu->DirectFrame(Rgb{4, 5, 6});
    gpu->TurnOff();
    for (const auto& w : fake_nv::Writes())
        CHECK(w.first != 0x3F);
}

TEST(MsiGpu_OneFailedWriteFailsTheSequence)
{
    auto gpu = Card();
    REQUIRE(gpu != nullptr);
    FakeNvConfig c = fake_nv::Rtx5080SuprimLiquid();
    c.failWriteNumber = 3;
    fake_nv::Configure(c);
    CHECK(!gpu->EnterDirect(Rgb{1, 2, 3}));
    CHECK_EQ(fake_nv::Writes().size(), size_t(9));   // keeps going after the failure

    fake_nv::Configure(c);
    CHECK(!gpu->DirectFrame(Rgb{1, 2, 3}));
    c.failWriteNumber = 0;
    fake_nv::Configure(c);
    CHECK(!gpu->TurnOff());
}

TEST(MsiGpu_ReadRegister)
{
    auto gpu = Card();
    REQUIRE(gpu != nullptr);
    FakeNvConfig c = fake_nv::Rtx5080SuprimLiquid();
    c.readValue = 0x13;
    fake_nv::Configure(c);
    CHECK_EQ(gpu->ReadRegister(0x22), 0x13);
    c.readStatus = -1;
    fake_nv::Configure(c);
    CHECK_EQ(gpu->ReadRegister(0x22), -1);
}

TEST(MsiGpu_WriteDelayIsApplied)
{
    auto gpu = Card();
    REQUIRE(gpu != nullptr);
    gpu->SetWriteDelayMs(5);
    auto start = std::chrono::steady_clock::now();
    CHECK(gpu->TurnOff());
    CHECK(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(10));
    gpu->SetWriteDelayMs(0);
}
