#include "test.h"

#include "wheel_log.h"

namespace test {

namespace {

int failures = 0;
const char* current = "";

}  // namespace

std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

void fail(const std::string& what, const char* file, int line) {
    ++failures;
    std::fprintf(stderr, "FAILED %s (%s:%d): %s\n", current, file, line, what.c_str());
}

}  // namespace test

int main() {
    // Drivers' log lines stay out of the test output.
    set_wheel_log([](const std::string&) {});
    for (const test::Case& one : test::cases()) {
        test::current = one.name;
        one.run();
    }
    std::printf("%zu tests, %d failed checks\n", test::cases().size(), test::failures);
    return test::failures == 0 ? 0 : 1;
}
