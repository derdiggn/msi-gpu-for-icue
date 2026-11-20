// SPDX-License-Identifier: GPL-2.0-or-later
//
// Tiny self-contained test framework (no third-party dependency).
//
//   TEST(Name) { CHECK(x); CHECK_EQ(a, b); REQUIRE(ptr); }
//
// CHECK* records a failure and continues, REQUIRE aborts the current test.
#pragma once

#include <cstdint>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace t {

struct Case {
    const char* name;
    void (*fn)();
};

struct Abort {};

inline std::vector<Case>& Registry()
{
    static std::vector<Case> cases;
    return cases;
}

struct Registrar {
    Registrar(const char* name, void (*fn)()) { Registry().push_back({name, fn}); }
};

void Fail(const char* file, int line, const std::string& message);

template <typename T>
std::string Str(const T& v)
{
    std::ostringstream s;
    if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, int8_t>)
        s << static_cast<int>(v);
    else
        s << v;
    return s.str();
}

} // namespace t

#define TEST(name)                                          \
    static void name();                                     \
    static ::t::Registrar name##_registrar(#name, name);    \
    static void name()

#define CHECK(cond)                                         \
    do {                                                    \
        if (!(cond))                                        \
            ::t::Fail(__FILE__, __LINE__, #cond);           \
    } while (0)

#define CHECK_EQ(a, b)                                                                                   \
    do {                                                                                                 \
        const auto check_a_ = (a); /* copies: (a) may refer into a temporary */                         \
        const auto check_b_ = (b);                                                                       \
        if (!(check_a_ == check_b_))                                                                     \
            ::t::Fail(__FILE__, __LINE__,                                                                \
                      std::string(#a " == " #b "  (") + ::t::Str(check_a_) + " vs " + ::t::Str(check_b_) + ")"); \
    } while (0)

#define REQUIRE(cond)                                       \
    do {                                                    \
        if (!(cond)) {                                      \
            ::t::Fail(__FILE__, __LINE__, #cond);           \
            throw ::t::Abort{};                             \
        }                                                   \
    } while (0)
