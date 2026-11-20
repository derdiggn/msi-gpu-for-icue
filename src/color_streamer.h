// SPDX-License-Identifier: GPL-2.0-or-later
//
// Sends colours from iCUE to a LightDevice on a dedicated thread.
//
// iCUE calls setLedsColors at its own frame rate (often 60 fps or more) from
// its own thread. Writing every colour would flood the I2C bus, and the
// controller needs a full init sequence after power events. ColorStreamer
// keeps only the newest colour and writes it:
//   - at most once per `frame`,
//   - only when it differs from what the device already shows,
//   - after the rate-limit pause it re-reads the newest colour, so the last
//     colour of a fade is never lost,
//   - with a full EnterDirect() on the first frame, after a failed write and
//     after a gap of more than `resumeGap` (sleep / hibernate),
//   - after failures with exponential backoff (`retryDelay` .. `maxRetryDelay`),
//     logging only the 1st, 2nd, 4th, 8th, ... failure in a row.
// Stop() takes effect after the current pause, i.e. within `frame` or the
// current retry delay.
#pragma once

#include "light_device.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

// Time source. Injected so the tests can run the streamer deterministically.
class Clock {
public:
    using time_point = std::chrono::steady_clock::time_point;
    using duration = std::chrono::steady_clock::duration;

    virtual ~Clock() = default;
    virtual time_point Now() = 0;
    virtual void SleepFor(duration d) = 0;
};

class SystemClock final : public Clock {
public:
    time_point Now() override;
    void SleepFor(duration d) override;
};

struct StreamerOptions {
    std::chrono::milliseconds frame{33};        // minimum time between two writes
    unsigned initDelayMs = 0;                    // per-register delay of EnterDirect/TurnOff
    bool turnOffOnExit = false;                  // TurnOff() when Run() returns
    bool verbose = false;                        // log every frame
    std::chrono::milliseconds poll{500};         // wake-up interval without new colours
    std::chrono::milliseconds resumeGap{5000};   // loop gap that counts as "machine slept"
    std::chrono::milliseconds retryDelay{200};   // pause after the first failed write, doubles per failure
    std::chrono::milliseconds maxRetryDelay{5000};
};

struct StreamerStats {
    uint64_t written = 0;   // successful EnterDirect/DirectFrame calls
    uint64_t skipped = 0;   // colours dropped because the device already shows them
    uint64_t failed = 0;    // failed EnterDirect/DirectFrame calls
};

class ColorStreamer {
public:
    ColorStreamer(LightDevice& device, Clock& clock, const StreamerOptions& options);

    ColorStreamer(const ColorStreamer&) = delete;
    ColorStreamer& operator=(const ColorStreamer&) = delete;

    // Queues `color`, replacing any colour not written yet. Thread safe.
    void Submit(Rgb color);

    // Makes Run() return as soon as possible. Thread safe; may be called
    // before Run().
    void Stop();

    // Streams until Stop(). Call from one thread only.
    void Run();

    StreamerStats Stats();

private:
    bool TakePendingLocked(Rgb& color);
    bool TakeNewestAfterPause(Rgb& color);   // false if stopped meanwhile
    void RequeueUnlessNewer(Rgb color);
    void CountSkipped();
    Clock::duration RetryDelay(uint32_t failures) const;
    bool Write(Rgb color, bool init);

    LightDevice& m_device;
    Clock& m_clock;
    StreamerOptions m_options;

    std::mutex m_mutex;   // guards the members below
    std::condition_variable m_wake;
    bool m_stop = false;
    bool m_hasPending = false;
    Rgb m_pending;
    StreamerStats m_stats;
};
