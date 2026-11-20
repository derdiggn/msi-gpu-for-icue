// SPDX-License-Identifier: GPL-2.0-or-later
//
// The exported plugin interface, driven the way iCUEDevicePluginHost does,
// against the fake NvAPI. Plus a check of the real DLL's export table.
#include "../src/cue_plugin_abi.h"
#include "../src/nvapi_i2c.h"
#include "../src/settings.h"
#include "../src/version.h"
#include "helpers.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "version.lib")

extern "C" {
int32_t CorsairPluginGetAPIVersion();
uint32_t CorsairPluginGetFeatures();
cue_abi::Instance* CorsairPluginGetInstance();
void CorsairPluginFreeInstance(cue_abi::Instance* instance);
}

namespace {

const char kDeviceId[] = "MSIGPU_10DE2C02_14625312";

struct Connection {
    std::mutex mutex;
    std::string deviceId;
    void* context = nullptr;
    int calls = 0;
};

Connection g_connection;

int32_t OnConnection(void* context, const char* deviceId, int32_t connected)
{
    std::lock_guard<std::mutex> lock(g_connection.mutex);
    g_connection.deviceId = deviceId;
    g_connection.context = context;
    g_connection.calls += connected;
    return 0;
}

int Announcements()
{
    std::lock_guard<std::mutex> lock(g_connection.mutex);
    return g_connection.calls;
}

// Fresh plugin instance with the given settings and the RTX 5080 attached.
class PluginFixture {
public:
    explicit PluginFixture(Settings settings = FastSettings(), FakeNvConfig nv = fake_nv::Rtx5080SuprimLiquid())
    {
        {
            std::lock_guard<std::mutex> lock(g_connection.mutex);
            g_connection.deviceId.clear();
            g_connection.context = nullptr;
            g_connection.calls = 0;
        }
        m_saved = GetSettings();
        SetSettings(settings);
        fake_nv::Configure(nv);
        inst = CorsairPluginGetInstance();
    }
    ~PluginFixture()
    {
        if (inst) {
            inst->unsubscribe();
            CorsairPluginFreeInstance(inst);
        }
        SetSettings(m_saved);
    }

    static Settings FastSettings()
    {
        Settings s;
        s.frameMs = 10;
        s.detectRetryMs = 5;
        return s;
    }

    bool SubscribeAndWait()
    {
        inst->subscribe(&m_context, OnConnection);
        return WaitUntil([] { return Announcements() == 1; });
    }

    bool Send(const char* id, int r, int g, int b)
    {
        cue_abi::LedColor c{cue_abi::kLedGpuZone1, r, g, b};
        return inst->setLedsColors(id, 1, &c);
    }

    cue_abi::Instance* inst = nullptr;
    int m_context = 7;

private:
    Settings m_saved;
};

bool Wrote(int reg, int value)
{
    for (const auto& w : fake_nv::Writes()) {
        if (w.first == reg && w.second == value)
            return true;
    }
    return false;
}

bool WroteRed(int value)
{
    return Wrote(0x30, value);
}

} // namespace

TEST(Plugin_ReportsApiVersionAndFeatures)
{
    Settings saved = GetSettings();
    Settings s;
    s.apiVersion = 0x6A;
    s.features = 0x54;
    SetSettings(s);
    CHECK_EQ(CorsairPluginGetAPIVersion(), 0x6A);
    CHECK_EQ(CorsairPluginGetAPIVersion(), 0x6A);   // logs only once
    CHECK_EQ(CorsairPluginGetFeatures(), 0x54u);
    SetSettings(saved);
}

TEST(Plugin_InstanceTable)
{
    cue_abi::Instance* inst = CorsairPluginGetInstance();
    REQUIRE(inst != nullptr);
    CHECK(inst->getDeviceInfo && inst->setLedsColors && inst->subscribe && inst->unsubscribe);
    CHECK(inst->getDeviceView && inst->freeDeviceInfo && inst->freeDeviceView && inst->setMode);
    CHECK(inst->configureKeyEvent == nullptr);
    CHECK(inst->subscribeForEvents == nullptr);
    CHECK(inst->getPluginPropertyInfo == nullptr);
    CHECK(inst->setLedsColorsAtChannel == nullptr);
    inst->setMode(static_cast<int32_t>(cue_abi::Mode::Detached));
    CorsairPluginFreeInstance(inst);
}

TEST(Plugin_AnnouncesDetectedCard)
{
    PluginFixture p;
    REQUIRE(p.SubscribeAndWait());
    std::lock_guard<std::mutex> lock(g_connection.mutex);
    CHECK_EQ(g_connection.deviceId, std::string(kDeviceId));
    CHECK(g_connection.context == &p.m_context);
}

TEST(Plugin_NoCardNoAnnouncement)
{
    PluginFixture p(PluginFixture::FastSettings(), FakeNvConfig{});
    int before = fake_nv::EnumCount();
    p.inst->subscribe(&p.m_context, OnConnection);
    REQUIRE(WaitUntil([&] { return fake_nv::EnumCount() >= before + 2; }));   // looked, retried
    p.inst->unsubscribe();
    CHECK_EQ(Announcements(), 0);
    CHECK(p.inst->getDeviceInfo(kDeviceId) == nullptr);
    CHECK(!p.Send(kDeviceId, 1, 2, 3));
}

TEST(Plugin_RetriesDetectionUntilTheCardAppears)
{
    // NvAPI reports no GPU at first (driver still starting after login).
    PluginFixture p(PluginFixture::FastSettings(), FakeNvConfig{});
    int before = fake_nv::EnumCount();
    p.inst->subscribe(&p.m_context, OnConnection);
    REQUIRE(WaitUntil([&] { return fake_nv::EnumCount() >= before + 3; }));
    CHECK_EQ(Announcements(), 0);
    fake_nv::Configure(fake_nv::Rtx5080SuprimLiquid());
    CHECK(WaitUntil([] { return Announcements() == 1; }));
}

TEST(Plugin_DeviceInfoAndView)
{
    PluginFixture p;
    REQUIRE(p.SubscribeAndWait());

    CHECK(p.inst->getDeviceInfo(nullptr) == nullptr);
    CHECK(p.inst->getDeviceInfo("OTHER") == nullptr);
    cue_abi::DeviceInfo* info = p.inst->getDeviceInfo(kDeviceId);
    REQUIRE(info != nullptr);
    CHECK_EQ(std::string(info->deviceName), std::string("MSI GeForce RTX 5080 SUPRIM LIQUID SOC"));
    CHECK_EQ(std::string(info->deviceId), std::string(kDeviceId));
    CHECK_EQ(info->deviceType, 4);
    CHECK_EQ(strlen(info->thumbnail->hash), size_t(64));
    CHECK_EQ(std::string(info->thumbnail->path), std::string("images/suprim_liquid/thumbnail.png"));
    CHECK_EQ(info->ledPositions->numberOfLed, 1);
    p.inst->freeDeviceInfo(info);

    CHECK(p.inst->getDeviceView(kDeviceId, 1) == nullptr);
    CHECK(p.inst->getDeviceView("OTHER", 0) == nullptr);
    cue_abi::DeviceView* view = p.inst->getDeviceView(kDeviceId, 0);
    REQUIRE(view != nullptr);
    CHECK_EQ(std::string(view->view->path), std::string("images/suprim_liquid/device_view.png"));
    CHECK_EQ(view->ledView->numberOfLed, 1);
    p.inst->freeDeviceView(view);
}

TEST(Plugin_StreamsColoursToTheCard)
{
    PluginFixture p;
    REQUIRE(p.SubscribeAndWait());

    CHECK(!p.Send("OTHER", 1, 2, 3));
    cue_abi::LedColor foreign{1462, 1, 2, 3};
    CHECK(!p.inst->setLedsColors(kDeviceId, 1, &foreign));
    CHECK(!p.inst->setLedsColors(kDeviceId, 0, nullptr));

    CHECK(p.Send(kDeviceId, 201, 0, 0));
    CHECK(WaitUntil([] { return WroteRed(201); }));
    CHECK(p.Send(kDeviceId, 202, 0, 0));
    CHECK(WaitUntil([] { return WroteRed(202); }));
    CHECK(Wrote(0x38, 0));   // the first frame ran the init sequence: effect speed 0
}

TEST(Plugin_SeveralZonesAreAveraged)
{
    Settings s = PluginFixture::FastSettings();
    s.ledCount = 3;
    PluginFixture p(s);
    REQUIRE(p.SubscribeAndWait());

    cue_abi::DeviceInfo* info = p.inst->getDeviceInfo(kDeviceId);
    REQUIRE(info != nullptr);
    CHECK_EQ(info->ledPositions->numberOfLed, 3);
    p.inst->freeDeviceInfo(info);
    cue_abi::DeviceView* view = p.inst->getDeviceView(kDeviceId, 0);
    REQUIRE(view != nullptr);
    CHECK_EQ(view->ledView->numberOfLed, 3);
    p.inst->freeDeviceView(view);

    cue_abi::LedColor colors[] = {
        {cue_abi::kLedGpuZone1, 30, 0, 0},
        {cue_abi::kLedGpuZone1 + 1, 60, 0, 0},
        {cue_abi::kLedGpuZone1 + 2, 90, 0, 0},
    };
    CHECK(p.inst->setLedsColors(kDeviceId, 3, colors));
    CHECK(WaitUntil([] { return WroteRed(60); }));
}

TEST(Plugin_SecondSubscribeKeepsWorker)
{
    PluginFixture p;
    REQUIRE(p.SubscribeAndWait());
    int initsBefore = fake_nv::InitCount();
    int enumsBefore = fake_nv::EnumCount();
    p.inst->subscribe(&p.m_context, OnConnection);
    CHECK(p.Send(kDeviceId, 77, 0, 0));   // the existing streamer still works
    CHECK(WaitUntil([] { return WroteRed(77); }));
    CHECK_EQ(Announcements(), 1);
    CHECK_EQ(fake_nv::InitCount(), initsBefore);
    CHECK_EQ(fake_nv::EnumCount(), enumsBefore);
}

TEST(Plugin_UnsubscribeForgetsDevice)
{
    PluginFixture p;
    REQUIRE(p.SubscribeAndWait());
    p.inst->unsubscribe();
    CHECK(p.inst->getDeviceInfo(kDeviceId) == nullptr);
    CHECK(!p.Send(kDeviceId, 1, 2, 3));

    // ... and can be subscribed again.
    p.inst->subscribe(&p.m_context, OnConnection);
    CHECK(WaitUntil([] { return Announcements() == 2; }));
    CHECK(p.Send(kDeviceId, 1, 2, 3));
}

TEST(Plugin_FreeInstanceStopsTheWorker)
{
    PluginFixture p;
    REQUIRE(p.SubscribeAndWait());
    CorsairPluginFreeInstance(p.inst);   // without unsubscribe, like a host shutting down
    p.inst = nullptr;
    cue_abi::Instance* again = CorsairPluginGetInstance();
    CHECK(again->getDeviceInfo(kDeviceId) == nullptr);   // the device is gone
    CorsairPluginFreeInstance(again);
}

TEST(Plugin_TurnOffOnExit)
{
    Settings s = PluginFixture::FastSettings();
    s.turnOffOnExit = true;
    PluginFixture p(s);
    REQUIRE(p.SubscribeAndWait());
    CHECK(p.Send(kDeviceId, 99, 0, 0));
    CHECK(WaitUntil([] { return WroteRed(99); }));
    p.inst->unsubscribe();
    auto writes = fake_nv::Writes();
    REQUIRE(!writes.empty());
    CHECK(writes.back() == (std::pair<int, int>{0x22, 0x01}));
}

TEST(Plugin_WorksWithoutCallback)
{
    PluginFixture p;
    p.inst->subscribe(nullptr, nullptr);
    CHECK(WaitUntil([&] {
        cue_abi::DeviceInfo* info = p.inst->getDeviceInfo(kDeviceId);
        p.inst->freeDeviceInfo(info);
        return info != nullptr;
    }));
    CHECK_EQ(Announcements(), 0);
}

TEST(Plugin_UnsubscribeDuringDetection)
{
    nvi2c::Reset();   // make the next detection go through NvAPI_Initialize
    FakeNvConfig nv = fake_nv::Rtx5080SuprimLiquid();
    nv.initSleepMs = 1000;   // the worker sits inside NvAPI_Initialize
    PluginFixture p(PluginFixture::FastSettings(), nv);
    int before = fake_nv::InitCount();
    p.inst->subscribe(&p.m_context, OnConnection);
    REQUIRE(WaitUntil([&] { return fake_nv::InitCount() > before; }));
    p.inst->unsubscribe();   // marks stopping well before Initialize returns, then joins
    CHECK_EQ(Announcements(), 0);
    CHECK(p.inst->getDeviceInfo(kDeviceId) == nullptr);
}

TEST(Plugin_ConcurrentSubscribeAndUnsubscribe)
{
    PluginFixture p;
    std::atomic<bool> go{false};
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&, t] {
            while (!go)
                std::this_thread::yield();
            for (int i = 0; i < 25; ++i) {
                if ((i + t) % 2)
                    p.inst->subscribe(&p.m_context, OnConnection);
                else
                    p.inst->unsubscribe();
                p.Send(kDeviceId, i, 0, 0);
            }
        });
    }
    go = true;
    for (auto& th : threads)
        th.join();
    p.inst->unsubscribe();
    CHECK(p.inst->getDeviceInfo(kDeviceId) == nullptr);
}

TEST(Dll_ExportsAndVersionResource)
{
    std::wstring path = ExeDir() + L"\\..\\MsiGpuForIcue.dll";
    HMODULE dll = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    REQUIRE(dll != nullptr);
    CHECK(GetProcAddress(dll, "CorsairPluginGetAPIVersion") != nullptr);
    CHECK(GetProcAddress(dll, "CorsairPluginGetFeatures") != nullptr);
    CHECK(GetProcAddress(dll, "CorsairPluginGetInstance") != nullptr);
    CHECK(GetProcAddress(dll, "CorsairPluginFreeInstance") != nullptr);
    FreeLibrary(dll);

    DWORD handle = 0;
    DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    REQUIRE(size > 0);
    std::vector<char> data(size);
    REQUIRE(GetFileVersionInfoW(path.c_str(), 0, size, data.data()));
    wchar_t* version = nullptr;
    UINT len = 0;
    REQUIRE(VerQueryValueW(data.data(), L"\\StringFileInfo\\040904B0\\ProductVersion",
                           reinterpret_cast<void**>(&version), &len));
    std::wstring expected;
    for (const char* c = MSIGPU_VERSION_STRING; *c; ++c)
        expected += static_cast<wchar_t>(*c);
    CHECK(std::wstring(version) == expected);
}
