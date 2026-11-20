// SPDX-License-Identifier: GPL-2.0-or-later
//
// The exception barrier used by every function iCUE calls.
#include "../src/abi_guard.h"
#include "helpers.h"

#include <new>
#include <stdexcept>

TEST(Guard_PassesResultThrough)
{
    CHECK_EQ(Guarded("x", -1, [] { return 42; }), 42);
    int calls = 0;
    GuardedVoid("x", [&] { ++calls; });
    CHECK_EQ(calls, 1);
}

TEST(Guard_TurnsExceptionsIntoFallback)
{
    LogCapture log;
    CHECK_EQ(Guarded("getThing", -1, []() -> int { throw std::bad_alloc(); }), -1);
    GuardedVoid("doThing", [] { throw std::runtime_error("boom"); });
    std::string text = log.Text();
    CHECK(text.find("getThing: unexpected exception") != std::string::npos);
    CHECK(text.find("doThing: unexpected exception") != std::string::npos);
}
