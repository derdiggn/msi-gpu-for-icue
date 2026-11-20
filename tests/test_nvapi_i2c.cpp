// SPDX-License-Identifier: GPL-2.0-or-later
//
// nvi2c: library loading, GPU enumeration and the I2C request encoding.
#include "../src/nvapi_i2c.h"
#include "helpers.h"

namespace {

const std::wstring& FakeLibrary()
{
    static std::wstring path = ExeDir() + L"\\fake_nvapi64.dll";
    return path;
}

// Re-initialises nvi2c against the fake with `config`.
bool InitWith(const FakeNvConfig& config, std::string* error = nullptr)
{
    nvi2c::Reset();
    nvi2c::SetLibraryName(FakeLibrary());
    fake_nv::Configure(config);
    return nvi2c::Initialize(error);
}

nvi2c::GpuHandle Handle(int index)
{
    return reinterpret_cast<nvi2c::GpuHandle>(kFakeGpuHandleBase + index);
}

} // namespace

TEST(Nvapi_MissingLibraryIsReported)
{
    nvi2c::Reset();
    nvi2c::SetLibraryName(L"msigpu-no-such-library.dll");
    std::string error;
    CHECK(!nvi2c::Initialize(&error));
    CHECK_EQ(error, std::string("nvapi64.dll not found (NVIDIA driver missing?)"));
    CHECK(!nvi2c::Initialize());   // error pointer is optional
    nvi2c::SetLibraryName(FakeLibrary());
}

TEST(Nvapi_LibraryWithoutQueryInterfaceIsRejected)
{
    nvi2c::Reset();
    nvi2c::SetLibraryName(L"kernel32.dll");
    std::string error;
    CHECK(!nvi2c::Initialize(&error));
    CHECK_EQ(error, std::string("nvapi_QueryInterface not exported"));
    CHECK(!nvi2c::Initialize());
    nvi2c::SetLibraryName(FakeLibrary());
}

TEST(Nvapi_MissingEntryPointIsRejected)
{
    const uint32_t ids[] = {0x0150E828, 0xE5AC921F, 0x2DDFB66E, 0x283AC65A, 0x4D7B0709};
    for (uint32_t id : ids) {
        FakeNvConfig c;
        c.hiddenFunction = id;
        std::string error;
        CHECK(!InitWith(c, &error));
        CHECK_EQ(error, std::string("required NvAPI functions are unavailable"));
    }
}

TEST(Nvapi_InitializeFailureCarriesStatus)
{
    FakeNvConfig c;
    c.initStatus = -104;
    std::string error;
    CHECK(!InitWith(c, &error));
    CHECK_EQ(error, std::string("NvAPI_Initialize failed with status -104"));
}

TEST(Nvapi_InitializeIsIdempotent)
{
    int before = fake_nv::InitCount();
    CHECK(InitWith(FakeNvConfig{}));
    CHECK(nvi2c::Initialize());
    CHECK(nvi2c::Initialize(nullptr));
    CHECK_EQ(fake_nv::InitCount(), before + 1);
}

TEST(Nvapi_CallsFailWhenNotInitialised)
{
    nvi2c::Reset();
    CHECK(nvi2c::EnumerateGpus().empty());
    CHECK(!nvi2c::WriteByte(Handle(0), 0x68, 0x22, 0x13));
    CHECK_EQ(nvi2c::ReadByte(Handle(0), 0x68, 0x22), -1);
    CHECK(fake_nv::Transfers().empty());
}

TEST(Nvapi_EnumerateDecodesPciIds)
{
    FakeNvConfig c;
    c.gpuCount = 3;
    c.deviceId[0] = 0x2C0210DE;
    c.subSystemId[0] = 0x53121462;
    c.deviceId[1] = 0x11112222;   // GetPCIIdentifiers fails for this one
    c.deviceId[2] = 0x2B8510DE;
    c.subSystemId[2] = 0x53001462;
    c.pciFailMask = 1u << 1;
    REQUIRE(InitWith(c));

    auto gpus = nvi2c::EnumerateGpus();
    REQUIRE(gpus.size() == 2);
    CHECK(gpus[0].handle == Handle(0));
    CHECK_EQ(gpus[0].vendor, uint16_t(0x10DE));
    CHECK_EQ(gpus[0].device, uint16_t(0x2C02));
    CHECK_EQ(gpus[0].subVendor, uint16_t(0x1462));
    CHECK_EQ(gpus[0].subDevice, uint16_t(0x5312));
    CHECK(gpus[1].handle == Handle(2));
    CHECK_EQ(gpus[1].device, uint16_t(0x2B85));
    CHECK_EQ(gpus[1].subDevice, uint16_t(0x5300));
}

TEST(Nvapi_EnumerateErrorGivesNoGpus)
{
    FakeNvConfig c = fake_nv::Rtx5080SuprimLiquid();
    c.enumStatus = -1;
    REQUIRE(InitWith(c));
    CHECK(nvi2c::EnumerateGpus().empty());
}

TEST(Nvapi_EnumerateIsCappedAt64Gpus)
{
    FakeNvConfig c;
    c.gpuCount = 70;
    REQUIRE(InitWith(c));
    CHECK_EQ(nvi2c::EnumerateGpus().size(), size_t(64));
}

TEST(Nvapi_WriteByteEncodesI2CInfoV3)
{
    REQUIRE(InitWith(FakeNvConfig{}));
    CHECK(nvi2c::WriteByte(Handle(0), 0x68, 0x30, 0xAB));

    auto transfers = fake_nv::Transfers();
    REQUIRE(transfers.size() == 1);
    const FakeNvTransfer& t = transfers[0];
    CHECK_EQ(t.write, 1);
    CHECK(t.gpu == Handle(0));
    CHECK_EQ(t.version, uint32_t(0x40 | (3u << 16)));
    CHECK_EQ(t.displayMask, uint32_t(0));
    CHECK_EQ(t.isDdcPort, uint8_t(0));
    CHECK_EQ(t.devAddress, uint8_t(0xD0));   // 0x68 << 1
    CHECK_EQ(t.regAddressSize, uint32_t(1));
    CHECK_EQ(t.reg, uint8_t(0x30));
    CHECK_EQ(t.size, uint32_t(1));
    CHECK_EQ(t.value, uint8_t(0xAB));
    CHECK_EQ(t.speed, uint32_t(0xFFFF));
    CHECK_EQ(t.speedKhz, uint32_t(0));
    CHECK_EQ(t.portId, uint8_t(1));
    CHECK_EQ(t.isPortIdSet, uint32_t(1));
}

TEST(Nvapi_WriteByteReportsDriverError)
{
    FakeNvConfig c;
    c.writeStatus = -5;
    REQUIRE(InitWith(c));
    CHECK(!nvi2c::WriteByte(Handle(0), 0x68, 0x22, 0x13));
}

TEST(Nvapi_ReadByteReturnsValue)
{
    FakeNvConfig c;
    c.readValue = 0x9C;
    REQUIRE(InitWith(c));
    CHECK_EQ(nvi2c::ReadByte(Handle(0), 0x68, 0x36), 0x9C);

    auto transfers = fake_nv::Transfers();
    REQUIRE(transfers.size() == 1);
    CHECK_EQ(transfers[0].write, 0);
    CHECK_EQ(transfers[0].reg, uint8_t(0x36));
    CHECK_EQ(transfers[0].devAddress, uint8_t(0xD0));
}

TEST(Nvapi_ReadByteReportsDriverError)
{
    FakeNvConfig c;
    c.readStatus = -1;
    REQUIRE(InitWith(c));
    CHECK_EQ(nvi2c::ReadByte(Handle(0), 0x68, 0x36), -1);
}
