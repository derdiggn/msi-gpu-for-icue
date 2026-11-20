// SPDX-License-Identifier: GPL-2.0-or-later
//
// ColorStreamer: rate limiting, de-duplication, re-init, backoff and
// shutdown, with a fake device and a fake clock.
#include "../src/color_streamer.h"
#include "../src/settings.h"
#include "helpers.h"

#include <atomic>
#include <thread>

namespace {

using std::chrono::milliseconds;

const Rgb kRed{255, 0, 0};
const Rgb kGreen{0, 255, 0};
const Rgb kBlue{0, 0, 255};

StreamerOptions FastOptions()
{
    StreamerOptions o;
    o.frame = milliseconds(33);
    o.initDelayMs = 7;
    o.poll = milliseconds(1);
    o.resumeGap = milliseconds(5000);
    o.retryDelay = milliseconds(200);
    o.maxRetryDelay = milliseconds(5000);
    return o;
}

// Runs a ColorStreamer on its own thread for the lifetime of the object.
// Anything a clock hook captures must be declared before this object, so it
// outlives the thread even when a REQUIRE aborts the test.
class Running {
public:
    explicit Running(const StreamerOptions& options = FastOptions())
        : streamer(device, clock, options), m_thread([this] { streamer.Run(); })
    {
    }
    ~Running() { StopAndJoin(); }

    void StopAndJoin()
    {
        streamer.Stop();
        if (m_thread.joinable())
            m_thread.join();
        clock.OnSleep(nullptr);
    }

    FakeDevice device;
    FakeClock clock;
    ColorStreamer streamer;

private:
    std::thread m_thread;
};

} // namespace

TEST(Streamer_FirstColourInitialisesThenStreams)
{
    Running r;
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));
    r.streamer.Submit(kGreen);
    REQUIRE(r.device.WaitForCalls(2));
    r.StopAndJoin();

    auto calls = r.device.Calls();
    REQUIRE(calls.size() == 2);
    CHECK_EQ(calls[0].kind, 'E');
    CHECK(calls[0].color == kRed);
    CHECK_EQ(calls[1].kind, 'F');
    CHECK(calls[1].color == kGreen);

    // The init sequence runs with the configured per-register delay.
    auto delays = r.device.Delays();
    REQUIRE(delays.size() == 2);
    CHECK_EQ(delays[0], 7u);
    CHECK_EQ(delays[1], 0u);
    CHECK_EQ(r.streamer.Stats().written, uint64_t(2));
}

TEST(Streamer_RateLimitsToFrameTime)
{
    Running r;
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));
    CHECK(r.clock.Sleeps().empty());   // the first frame is never delayed

    r.streamer.Submit(kGreen);
    REQUIRE(r.device.WaitForCalls(2));
    auto sleeps = r.clock.Sleeps();
    REQUIRE(sleeps.size() == 1);
    CHECK(sleeps[0] == milliseconds(33));
}

TEST(Streamer_SkipsColourAlreadyShown)
{
    Running r;
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));
    r.streamer.Submit(kRed);
    CHECK(WaitUntil([&] { return r.streamer.Stats().skipped == 1; }));
    r.streamer.Submit(kBlue);
    REQUIRE(r.device.WaitForCalls(2));
    auto calls = r.device.Calls();
    REQUIRE(calls.size() == 2);
    CHECK(calls[1].color == kBlue);
}

TEST(Streamer_NewestColourWinsAfterRateLimit)
{
    std::atomic<bool> once{false};
    Running r;
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));

    // Blue arrives while the streamer waits for the next frame slot.
    r.clock.OnSleep([&] {
        if (!once.exchange(true))
            r.streamer.Submit(kBlue);
    });
    r.streamer.Submit(kGreen);
    REQUIRE(r.device.WaitForCalls(2));
    CHECK(r.device.Calls()[1].color == kBlue);
}

TEST(Streamer_SkipsWhenPauseEndsOnShownColour)
{
    std::atomic<bool> once{false};
    Running r;
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));

    // Green is replaced by red again during the pause: nothing to write.
    r.clock.OnSleep([&] {
        if (!once.exchange(true))
            r.streamer.Submit(kRed);
    });
    r.streamer.Submit(kGreen);
    CHECK(WaitUntil([&] { return r.streamer.Stats().skipped == 1; }));
    CHECK_EQ(r.device.Calls().size(), size_t(1));
}

TEST(Streamer_StopDuringRateLimitWritesNothingMore)
{
    std::atomic<bool> stopped{false};
    Running r;
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));
    r.clock.OnSleep([&] {
        r.streamer.Stop();
        stopped = true;
    });
    r.streamer.Submit(kGreen);
    REQUIRE(WaitUntil([&] { return stopped.load(); }));   // Stop() came from inside the pause
    r.StopAndJoin();
    CHECK_EQ(r.device.Calls().size(), size_t(1));
}

TEST(Streamer_FailedWriteRetriesWithInit)
{
    Running r;
    r.device.FailNext(1);
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(2));
    r.StopAndJoin();

    auto calls = r.device.Calls();
    CHECK_EQ(calls[0].kind, 'E');
    CHECK_EQ(calls[1].kind, 'E');
    CHECK(calls[1].color == kRed);
    auto sleeps = r.clock.Sleeps();
    REQUIRE(!sleeps.empty());
    CHECK(sleeps[0] == milliseconds(200));
    CHECK_EQ(r.streamer.Stats().failed, uint64_t(1));
    CHECK_EQ(r.streamer.Stats().written, uint64_t(1));
}

TEST(Streamer_BackoffDoublesUpToTheCap)
{
    StreamerOptions o = FastOptions();
    o.maxRetryDelay = milliseconds(500);
    Running r(o);
    r.device.FailNext(4);
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(5));
    r.StopAndJoin();
    auto sleeps = r.clock.Sleeps();
    REQUIRE(sleeps.size() >= 4);
    CHECK(sleeps[0] == milliseconds(200));
    CHECK(sleeps[1] == milliseconds(400));
    CHECK(sleeps[2] == milliseconds(500));
    CHECK(sleeps[3] == milliseconds(500));
}

TEST(Streamer_FailureLoggingIsThrottled)
{
    LogCapture log;
    {
        Running r;
        r.device.FailNext(5);
        r.streamer.Submit(kRed);
        REQUIRE(r.device.WaitForCalls(6));
    }
    std::string text = log.Text();
    CHECK(text.find("write failed 1 time(s) in a row (255,0,0)") != std::string::npos);
    CHECK(text.find("write failed 2 time(s)") != std::string::npos);
    CHECK(text.find("write failed 3 time(s)") == std::string::npos);
    CHECK(text.find("write failed 4 time(s)") != std::string::npos);
    CHECK(text.find("write failed 5 time(s)") == std::string::npos);
    CHECK(text.find("recovered after 5 failed write(s)") != std::string::npos);
    CHECK(text.find("direct mode set (255,0,0)") != std::string::npos);
}

TEST(Streamer_RetryPrefersNewerColour)
{
    std::atomic<bool> submitted{false};
    Running r;
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));

    // DirectFrame(green) fails; blue arrives during the retry pause.
    r.device.FailNext(1);
    r.clock.OnSleep([&] {
        if (r.clock.Sleeps().back() == milliseconds(200) && !submitted.exchange(true))
            r.streamer.Submit(kBlue);
    });
    r.streamer.Submit(kGreen);
    REQUIRE(r.device.WaitForCalls(3));
    auto calls = r.device.Calls();
    CHECK_EQ(calls[1].kind, 'F');
    CHECK(calls[1].color == kGreen);
    CHECK_EQ(calls[2].kind, 'E');   // full init after the failure
    CHECK(calls[2].color == kBlue);
}

TEST(Streamer_ResumeGapReinitialisesLastColour)
{
    Running r;
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));
    r.clock.Advance(milliseconds(6000));   // e.g. the PC was asleep
    REQUIRE(r.device.WaitForCalls(2));
    auto calls = r.device.Calls();
    CHECK_EQ(calls[1].kind, 'E');
    CHECK(calls[1].color == kRed);
}

TEST(Streamer_ResumeGapWithNewColour)
{
    StreamerOptions o = FastOptions();
    o.poll = milliseconds(60000);   // only Submit() wakes the streamer
    Running r(o);
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));
    r.clock.Advance(milliseconds(6000));
    r.streamer.Submit(kGreen);
    REQUIRE(r.device.WaitForCalls(2));
    auto calls = r.device.Calls();
    CHECK_EQ(calls[1].kind, 'E');
    CHECK(calls[1].color == kGreen);
}

TEST(Streamer_NoGapNoColourDoesNothing)
{
    Running r;
    // Wait until Run() has taken its start time and polled a few times.
    REQUIRE(WaitUntil([&] { return r.clock.NowCalls() >= 3; }));
    r.clock.Advance(milliseconds(6000));   // gap, but nothing was ever written
    int calls = r.clock.NowCalls();
    REQUIRE(WaitUntil([&] { return r.clock.NowCalls() >= calls + 3; }));   // the gap was seen
    r.StopAndJoin();
    CHECK(r.device.Calls().empty());
}

TEST(Streamer_TurnOffOnExit)
{
    StreamerOptions o = FastOptions();
    o.turnOffOnExit = true;
    Running r(o);
    r.streamer.Submit(kRed);
    REQUIRE(r.device.WaitForCalls(1));
    r.StopAndJoin();
    auto calls = r.device.Calls();
    REQUIRE(calls.size() == 2);
    CHECK_EQ(calls[1].kind, 'O');
    CHECK_EQ(r.device.Delays().back(), 7u);
}

TEST(Streamer_VerboseLogsEveryFrame)
{
    LogCapture log;
    StreamerOptions o = FastOptions();
    o.verbose = true;
    {
        Running r(o);
        r.streamer.Submit(kRed);
        REQUIRE(r.device.WaitForCalls(1));
        r.device.FailNext(1);
        r.streamer.Submit(kGreen);
        REQUIRE(r.device.WaitForCalls(3));
    }
    std::string text = log.Text();
    CHECK(text.find("direct mode set (255,0,0)") != std::string::npos);
    CHECK(text.find("color (0,255,0) FAILED") != std::string::npos);
    CHECK(text.find("streamer stopped") != std::string::npos);
}

TEST(Streamer_StopBeforeRunReturnsImmediately)
{
    FakeDevice device;
    FakeClock clock;
    ColorStreamer streamer(device, clock, FastOptions());
    streamer.Stop();
    streamer.Submit(kRed);
    streamer.Run();
    CHECK(device.Calls().empty());
}

TEST(SystemClock_SleepsAndAdvances)
{
    SystemClock clock;
    auto start = clock.Now();
    clock.SleepFor(milliseconds(5));
    CHECK(clock.Now() - start >= milliseconds(5));
}
