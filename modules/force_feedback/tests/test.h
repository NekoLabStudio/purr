#pragma once

// A small test runner: TEST(name) { CHECK(...); CHECK_EQ(a, b); }. Every
// test runs; the executable fails if any check did.

#include "drivers/wheel_driver.h"

#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace test {

struct Case {
    const char* name;
    std::function<void()> run;
};

std::vector<Case>& cases();
void fail(const std::string& what, const char* file, int line);

struct Registration {
    Registration(const char* name, std::function<void()> run) { cases().push_back({name, std::move(run)}); }
};

inline std::string show(const Bytes& bytes) {
    std::string out;
    char hex[4];
    for (const std::uint8_t byte : bytes) {
        std::snprintf(hex, sizeof(hex), "%02X ", byte);
        out += hex;
    }
    return out.empty() ? "(none)" : out.substr(0, out.size() - 1);
}

inline std::string show(const std::vector<Bytes>& reports) {
    std::string out;
    for (const Bytes& report : reports) {
        out += "\n      " + show(report);
    }
    return out.empty() ? "(none)" : out;
}

inline std::string show(const std::vector<std::size_t>& values) {
    std::string out;
    for (const std::size_t value : values) {
        out += (out.empty() ? "" : " ") + std::to_string(value);
    }
    return "[" + out + "]";
}

template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

template <typename A, typename B>
void check_equal(const A& actual, const B& expected, const char* text, const char* file, int line) {
    if (!(actual == expected)) {
        fail(std::string(text) + "\n    got      " + show(actual) + "\n    expected " + show(expected), file, line);
    }
}

}  // namespace test

#define TEST_JOIN2(a, b) a##b
#define TEST_JOIN(a, b) TEST_JOIN2(a, b)
#define TEST(name)                                                              \
    static void name();                                                         \
    static const test::Registration TEST_JOIN(name, _registration)(#name, name); \
    static void name()
#define CHECK(condition) \
    ((condition) ? (void)0 : test::fail("CHECK(" #condition ")", __FILE__, __LINE__))
#define CHECK_EQ(actual, expected) test::check_equal((actual), (expected), #actual, __FILE__, __LINE__)
