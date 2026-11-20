// SPDX-License-Identifier: GPL-2.0-or-later
//
#include "color_streamer.h"

#include "settings.h"

#include <thread>

Clock::time_point SystemClock::Now()
{
    return std::chrono::steady_clock::now();
}

void SystemClock::SleepFor(duration d)
{
    std::this_thread::sleep_for(d);
}

ColorStreamer::ColorStreamer(LightDevice& device, Clock& clock, const StreamerOptions& options)
    : m_device(device), m_clock(clock), m_options(options)
{
}

void ColorStreamer::Submit(Rgb color)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = color;
        m_hasPending = true;
    }
    m_wake.notify_one();
}

void ColorStreamer::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
    }
    m_wake.notify_all();
}

StreamerStats ColorStreamer::Stats()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_stats;
}

bool ColorStreamer::TakePendingLocked(Rgb& color)
{
    if (!m_hasPending)
        return false;
    color = m_pending;
    m_hasPending = false;
    return true;
}

void ColorStreamer::CountSkipped()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    ++m_stats.skipped;
}

bool ColorStreamer::Write(Rgb color, bool init)
{
    bool ok;
    if (init) {
        m_device.SetWriteDelayMs(m_options.initDelayMs);
        ok = m_device.EnterDirect(color);
        m_device.SetWriteDelayMs(0);
        if (ok)
            Log("direct mode set (%u,%u,%u)", color.r, color.g, color.b);
    } else {
        ok = m_device.DirectFrame(color);
        if (m_options.verbose)
            Log("color (%u,%u,%u) %s", color.r, color.g, color.b, ok ? "ok" : "FAILED");
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    if (ok)
        ++m_stats.written;
    else
        ++m_stats.failed;
    return ok;
}

bool ColorStreamer::TakeNewestAfterPause(Rgb& color)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_stop)
        return false;
    TakePendingLocked(color);
    return true;
}

void ColorStreamer::RequeueUnlessNewer(Rgb color)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hasPending) {
        m_pending = color;
        m_hasPending = true;
    }
}

Clock::duration ColorStreamer::RetryDelay(uint32_t failures) const
{
    Clock::duration delay = m_options.retryDelay;
    for (uint32_t i = 1; i < failures && delay < m_options.maxRetryDelay; ++i)
        delay *= 2;
    return delay < m_options.maxRetryDelay ? delay : Clock::duration(m_options.maxRetryDelay);
}

void ColorStreamer::Run()
{
    bool needInit = true;
    bool haveWritten = false;
    uint32_t failures = 0;   // consecutive failed writes
    Rgb written;
    Clock::time_point lastLoop = m_clock.Now();
    Clock::time_point lastWrite = lastLoop - m_options.frame;   // never delay the first frame

    for (;;) {
        Rgb color;
        bool have;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait_for(lock, m_options.poll, [this] { return m_stop || m_hasPending; });
            if (m_stop)
                break;
            have = TakePendingLocked(color);
        }

        Clock::time_point now = m_clock.Now();
        if (now - lastLoop > m_options.resumeGap) {
            // The machine probably slept; the controller may have lost its mode.
            Log("time gap detected, re-initialising lighting mode");
            needInit = true;
            if (haveWritten && !have) {
                color = written;
                have = true;
            }
        }
        lastLoop = now;

        if (!have)
            continue;
        if (!needInit && haveWritten && color == written) {
            CountSkipped();
            continue;
        }

        Clock::duration sinceWrite = now - lastWrite;
        if (sinceWrite < m_options.frame) {
            m_clock.SleepFor(m_options.frame - sinceWrite);
            if (!TakeNewestAfterPause(color))   // the newest colour wins
                break;
            if (!needInit && haveWritten && color == written) {
                CountSkipped();   // e.g. a fade that came back to the shown colour
                continue;
            }
        }

        bool ok = Write(color, needInit);
        lastWrite = m_clock.Now();
        if (ok) {
            if (failures)
                Log("recovered after %u failed write(s)", failures);
            failures = 0;
            needInit = false;
            haveWritten = true;
            written = color;
            continue;
        }

        // Retry the full init sequence with exponential backoff. Keep a newer
        // colour if one arrived meanwhile.
        needInit = true;
        ++failures;
        if ((failures & (failures - 1)) == 0)
            Log("write failed %u time(s) in a row (%u,%u,%u), retrying", failures, color.r, color.g, color.b);
        m_clock.SleepFor(RetryDelay(failures));
        RequeueUnlessNewer(color);
    }

    if (m_options.turnOffOnExit) {
        m_device.SetWriteDelayMs(m_options.initDelayMs);
        m_device.TurnOff();
    }
    Log("streamer stopped");
}
