// SPDX-License-Identifier: GPL-2.0-or-later
//
// Device id, pictures per card style, DeviceInfo/DeviceView layout and colour
// averaging.
#include "../src/device_description.h"
#include "helpers.h"

#include <cstdio>
#include <cstring>
#include <set>

TEST(Device_IdFormat)
{
    nvi2c::GpuInfo gpu;
    gpu.vendor = 0x10DE;
    gpu.device = 0x2C02;
    gpu.subVendor = 0x1462;
    gpu.subDevice = 0x5312;
    CHECK_EQ(device::DeviceIdFor(gpu), std::string("MSIGPU_10DE2C02_14625312"));
}

TEST(Device_Sha256OfKnownContent)
{
    TempDir dir;
    dir.Write(L"abc", "abc");
    dir.Write(L"empty", "");
    CHECK_EQ(device::Sha256File(dir.File(L"abc")),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    CHECK_EQ(device::Sha256File(dir.File(L"empty")),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    CHECK_EQ(device::Sha256File(dir.File(L"missing")), std::string(""));
}

TEST(Device_Sha256OfLargeFile)
{
    TempDir dir;
    dir.Write(L"big", std::string(40000, 'a'));   // more than one read buffer
    CHECK_EQ(device::Sha256File(dir.File(L"big")).size(), size_t(64));
}

namespace {

CardStyle StyleAt(int i)
{
    return static_cast<CardStyle>(i);
}

// Absolute path of a picture shipped next to tests.exe.
std::wstring Shipped(const char* relPath)
{
    std::wstring w = ExeDir() + L"\\";
    for (const char* c = relPath; *c; ++c)
        w += static_cast<wchar_t>(*c == '/' ? '\\' : *c);
    return w;
}

} // namespace

TEST(Device_ImagePathUsesTheStyleFolder)
{
    CHECK_EQ(device::ImagePath(CardStyle::SuprimLiquid, "promo.png"), std::string("images/suprim_liquid/promo.png"));
    CHECK_EQ(device::ImagePath(CardStyle::GamingTrio, "thumbnail.png"),
             std::string("images/gaming_trio/thumbnail.png"));
    CHECK_EQ(std::string(device::ArtFor(CardStyle::Vanguard).folder), std::string("vanguard"));
    CHECK_EQ(std::string(device::ArtFor(CardStyle::Suprim).folder), std::string("suprim"));
    CHECK_EQ(std::string(device::ArtFor(CardStyle::Gaming).folder), std::string("gaming"));
}

TEST(Device_EveryStyleShipsItsPictures)
{
    std::set<std::string> folders;
    for (int i = 0; i < kCardStyleCount; ++i) {
        const CardStyle style = StyleAt(i);
        folders.insert(device::ArtFor(style).folder);

        cue_abi::DeviceInfo* info = device::BuildDeviceInfo("Card", "ID", 4, 1, style, ExeDir());
        CHECK_EQ(std::string(info->thumbnail->path), device::ImagePath(style, "thumbnail.png"));
        CHECK_EQ(std::string(info->promoImage->path), device::ImagePath(style, "promo.png"));
        CHECK_EQ(strlen(info->thumbnail->hash), size_t(64));
        CHECK_EQ(device::Sha256File(Shipped(info->thumbnail->path)), std::string(info->thumbnail->hash));
        CHECK_EQ(device::Sha256File(Shipped(info->promoImage->path)), std::string(info->promoImage->hash));
        device::FreeDeviceInfo(info);

        cue_abi::DeviceView* view = device::BuildDeviceView(1, style, ExeDir());
        CHECK_EQ(std::string(view->view->path), device::ImagePath(style, "device_view.png"));
        CHECK_EQ(std::string(view->mask->path), device::ImagePath(style, "device_view_mask.png"));
        CHECK_EQ(strlen(view->mask->hash), size_t(64));
        CHECK_EQ(device::Sha256File(Shipped(view->view->path)), std::string(view->view->hash));
        CHECK_EQ(device::Sha256File(Shipped(view->mask->path)), std::string(view->mask->hash));
        device::FreeDeviceView(view);
    }
    CHECK_EQ(folders.size(), size_t(kCardStyleCount));   // no two styles share pictures
}

TEST(Device_LightBarsLieInsideTheDeviceView)
{
    // device_view.png is 615 x 408 for every style.
    for (int i = 0; i < kCardStyleCount; ++i) {
        const device::CardArt& art = device::ArtFor(StyleAt(i));
        CHECK(art.barLeft > 0 && art.barLeft < art.barRight && art.barRight < 615);
        CHECK(art.barTop > 0 && art.barTop < art.barBottom && art.barBottom < 408);
        CHECK(art.barRight - art.barLeft >= 100);   // room for several zones
    }
}

TEST(Device_InfoDescribesTheCard)
{
    cue_abi::DeviceInfo* info = device::BuildDeviceInfo("Card", "ID_1", 4, 3, CardStyle::SuprimLiquid, ExeDir());
    REQUIRE(info != nullptr);
    CHECK_EQ(std::string(info->deviceName), std::string("Card"));
    CHECK_EQ(std::string(info->deviceId), std::string("ID_1"));
    CHECK_EQ(info->deviceType, 4);
    CHECK_EQ(info->numberOfDeviceView, 1);
    CHECK_EQ(std::string(info->thumbnail->path), std::string("images/suprim_liquid/thumbnail.png"));
    CHECK_EQ(std::string(info->promoImage->path), std::string("images/suprim_liquid/promo.png"));
    CHECK_EQ(strlen(info->thumbnail->hash), size_t(64));
    CHECK_EQ(strlen(info->promoImage->hash), size_t(64));
    CHECK_EQ(device::Sha256File(Shipped(info->promoImage->path)), std::string(info->promoImage->hash));

    REQUIRE(info->ledPositions->numberOfLed == 3);
    for (int i = 0; i < 3; ++i) {
        const auto& p = info->ledPositions->ledPosition[i];
        CHECK_EQ(p.ledId, cue_abi::kLedGpuZone1 + i);
        CHECK_EQ(p.x, 10.0 + 20.0 * i);
        CHECK_EQ(p.y, 10.0);
        CHECK_EQ(p.unk04, 0);
    }
    device::FreeDeviceInfo(info);
}

TEST(Device_MissingImagesGiveEmptyHash)
{
    TempDir dir;
    cue_abi::DeviceInfo* info = device::BuildDeviceInfo("Card", "ID", 4, 1, CardStyle::Gaming, dir.Path());
    CHECK_EQ(std::string(info->thumbnail->hash), std::string(""));
    CHECK_EQ(std::string(info->thumbnail->path), std::string("images/gaming/thumbnail.png"));
    device::FreeDeviceInfo(info);
}

TEST(Device_ViewWithOneZoneCoversTheBar)
{
    cue_abi::DeviceView* view = device::BuildDeviceView(1, CardStyle::SuprimLiquid, ExeDir());
    REQUIRE(view != nullptr);
    CHECK_EQ(std::string(view->view->path), std::string("images/suprim_liquid/device_view.png"));
    CHECK_EQ(std::string(view->mask->path), std::string("images/suprim_liquid/device_view_mask.png"));
    CHECK_EQ(strlen(view->view->hash), size_t(64));
    CHECK_EQ(strlen(view->mask->hash), size_t(64));
    CHECK(view->actionZones == nullptr);
    REQUIRE(view->ledView->numberOfLed == 1);
    const auto& v = view->ledView->view[0];
    CHECK_EQ(v.ledId, cue_abi::kLedGpuZone1);
    CHECK_EQ(std::string(v.path), std::string("M 51 60 L 236 60 L 236 72 L 51 72 Z"));
    CHECK(v.appearance == cue_abi::ZoneAppearance::SvgPath);
    CHECK(v.text == nullptr);
    device::FreeDeviceView(view);
}

TEST(Device_ViewSplitsBarIntoZones)
{
    cue_abi::DeviceView* view = device::BuildDeviceView(3, CardStyle::SuprimLiquid, ExeDir());
    REQUIRE(view->ledView->numberOfLed == 3);
    CHECK_EQ(std::string(view->ledView->view[0].path), std::string("M 51 60 L 110 60 L 110 72 L 51 72 Z"));
    CHECK_EQ(std::string(view->ledView->view[1].path), std::string("M 114 60 L 172 60 L 172 72 L 114 72 Z"));
    CHECK_EQ(std::string(view->ledView->view[2].path), std::string("M 176 60 L 236 60 L 236 72 L 176 72 Z"));
    CHECK_EQ(view->ledView->view[2].ledId, cue_abi::kLedGpuZone1 + 2);
    device::FreeDeviceView(view);
}

TEST(Device_ViewZonesFollowTheStyleBar)
{
    const device::CardArt& art = device::ArtFor(CardStyle::GamingTrio);
    cue_abi::DeviceView* view = device::BuildDeviceView(1, CardStyle::GamingTrio, ExeDir());
    char expected[160];
    snprintf(expected, sizeof(expected), "M %d %d L %d %d L %d %d L %d %d Z", art.barLeft, art.barTop, art.barRight,
             art.barTop, art.barRight, art.barBottom, art.barLeft, art.barBottom);
    CHECK_EQ(std::string(view->ledView->view[0].path), std::string(expected));
    device::FreeDeviceView(view);
}

TEST(Device_FreeAcceptsNull)
{
    device::FreeDeviceInfo(nullptr);
    device::FreeDeviceView(nullptr);
}

TEST(Device_AverageColorRejectsEmptyInput)
{
    Rgb out{1, 2, 3};
    cue_abi::LedColor c{cue_abi::kLedGpuZone1, 9, 9, 9};
    CHECK(!device::AverageColor(nullptr, 1, 1, out));
    CHECK(!device::AverageColor(&c, 0, 1, out));
    CHECK(!device::AverageColor(&c, -1, 1, out));
    CHECK(out == (Rgb{1, 2, 3}));
}

TEST(Device_AverageColorIgnoresForeignLeds)
{
    cue_abi::LedColor colors[] = {
        {cue_abi::kLedGpuZone1 - 1, 255, 255, 255},   // not ours
        {cue_abi::kLedGpuZone1, 100, 0, 50},
        {cue_abi::kLedGpuZone1 + 1, 200, 100, 0},
        {cue_abi::kLedGpuZone1 + 2, 255, 255, 255},   // beyond ledCount
        {1462, 255, 255, 255},                        // old SDK id
    };
    Rgb out;
    CHECK(device::AverageColor(colors, 5, 2, out));
    CHECK(out == (Rgb{150, 50, 25}));

    cue_abi::LedColor foreign[] = {{1462, 1, 2, 3}};
    CHECK(!device::AverageColor(foreign, 1, 2, out));
}

TEST(Device_AverageColorClampsChannels)
{
    cue_abi::LedColor colors[] = {{cue_abi::kLedGpuZone1, 300, -20, 128}};
    Rgb out;
    CHECK(device::AverageColor(colors, 1, 1, out));
    CHECK(out == (Rgb{255, 0, 128}));
}
