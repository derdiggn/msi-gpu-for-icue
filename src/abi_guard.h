// SPDX-License-Identifier: GPL-2.0-or-later
//
// Exception barrier for everything iCUE calls. A C++ exception must never
// unwind into iCUE's plugin host (different CRT, C ABI): it would be undefined
// behaviour or terminate the host. Realistically only std::bad_alloc and
// std::system_error (thread creation) can occur.
#pragma once

#include "settings.h"

#include <utility>

template <typename R, typename F>
R Guarded(const char* what, R fallback, F&& fn) noexcept
{
    try {
        return std::forward<F>(fn)();
    } catch (...) {
        Log("%s: unexpected exception", what);
    }
    return fallback;
}

template <typename F>
void GuardedVoid(const char* what, F&& fn) noexcept
{
    try {
        std::forward<F>(fn)();
    } catch (...) {
        Log("%s: unexpected exception", what);
    }
}
