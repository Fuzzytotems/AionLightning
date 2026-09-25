// jlang/tests/jtest.h - minimal unit test harness shared by all jlang tests.
//
//   #include "jtest.h"
//   JTEST(StringSplit) { JCHECK(x == 1); JCHECK_EQ(a, b); JCHECK_THROWS(jlang::NumberFormatException, f()); }
//
// Every jlang/tests/*.cpp is linked into the single `jlang_tests` executable (main() lives in
// jtest_main.cpp, which calls jlang::init first). Run `jlang_tests [substring]` to filter.
#pragma once

#include <jlang/jlang.h>

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace jtest {

struct TestCase {
    const char* name;
    void (*fn)();
    const char* file;
    int line;
};

std::vector<TestCase>& registry();
void fail(const char* file, int line, const std::string& msg);  // records a failure, continues

struct Registrar {
    Registrar(const char* name, void (*fn)(), const char* file, int line) {
        registry().push_back(TestCase{name, fn, file, line});
    }
};

template<class T>
std::string show(const T& v) {
    if constexpr (std::is_same_v<T, jlang::String>) {
        return v.isNull() ? std::string("null") : "\"" + std::string(v) + "\"";
    } else if constexpr (std::is_same_v<T, char16_t>) {
        return "u+" + std::to_string(static_cast<unsigned>(v));
    } else if constexpr (std::is_convertible_v<const T&, const char*>) {
        const char* p = v;
        return p == nullptr ? std::string("nullptr") : std::string(p);
    } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, uint8_t>) {
        return std::to_string(static_cast<int>(v));
    } else if constexpr (requires(std::ostream& o) { o << v; }) {
        std::ostringstream os;
        os.precision(17);
        os << v;
        return os.str();
    } else {
        return "<value>";
    }
}

template<class... A>
std::string showAll(const A&... a) {
    std::string r;
    ((r += (r.empty() ? "" : " ") + show(a)), ...);
    return r;
}

}  // namespace jtest

#define JTEST(name)                                                                \
    static void jtest_fn_##name();                                                 \
    static const ::jtest::Registrar jtest_reg_##name(#name, &jtest_fn_##name, __FILE__, __LINE__); \
    static void jtest_fn_##name()

// JCHECK(cond) or JCHECK(cond, message-values...)
#define JCHECK(cond, ...)                                                          \
    do {                                                                           \
        if (!(cond))                                                               \
            ::jtest::fail(__FILE__, __LINE__,                                      \
                          std::string("CHECK(" #cond ") failed") __VA_OPT__(+": " + ::jtest::showAll(__VA_ARGS__))); \
    } while (0)

#define JCHECK_EQ(a, b)                                                            \
    do {                                                                           \
        const auto jtest_a_ = (a);                                                 \
        const auto jtest_b_ = (b);                                                 \
        if (!(jtest_a_ == jtest_b_))                                               \
            ::jtest::fail(__FILE__, __LINE__,                                      \
                          "CHECK_EQ(" #a ", " #b ") failed: " + ::jtest::show(jtest_a_) + \
                              " vs " + ::jtest::show(jtest_b_));                   \
    } while (0)

#define JCHECK_THROWS(ExType, ...)                                                 \
    do {                                                                           \
        bool jtest_thrown_ = false;                                                \
        try {                                                                      \
            (void)(__VA_ARGS__);                                                   \
        } catch (ExType&) {                                                        \
            jtest_thrown_ = true;                                                  \
        } catch (...) {                                                            \
            ::jtest::fail(__FILE__, __LINE__, "wrong exception type from: " #__VA_ARGS__); \
            jtest_thrown_ = true;                                                  \
        }                                                                          \
        if (!jtest_thrown_) ::jtest::fail(__FILE__, __LINE__, "expected " #ExType " from: " #__VA_ARGS__); \
    } while (0)
