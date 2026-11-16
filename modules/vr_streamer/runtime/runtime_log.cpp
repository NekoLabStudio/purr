#include "runtime_log.hpp"

#include "platform.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace {

std::mutex g_log_lock;
FILE* g_log = nullptr;

// A path Purr gives in the environment, or one in the temporary folder.
std::string from_environment(const char* name, const char* fallback) {
    const char* value = std::getenv(name);
    if (value && value[0] != '\0') {
        return value;
    }
    return platform::temporary_directory() + fallback;
}

std::string with_separator(std::string path) {
    if (!path.empty() && path.back() != '/' && path.back() != '\\') {
        path += '/';
    }
    return path;
}

}  // namespace

void runtime_log(const char* message) {
    std::lock_guard<std::mutex> lock(g_log_lock);
    if (!g_log) {
        const std::string path = from_environment("PURR_VR_LOG", "vr.log");
        platform::make_directories(path.substr(0, path.find_last_of("/\\") + 1));
        g_log = std::fopen(path.c_str(), "w");
        if (!g_log) {
            return;
        }
    }
    std::fprintf(g_log, "%s\n", message);
    std::fflush(g_log);
}

void runtime_logf(const char* format, ...) {
    char buffer[1024] = {};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    runtime_log(buffer);
}

const char* runtime_directory() {
    static const std::string directory = platform::library_directory();
    return directory.c_str();
}

const char* runtime_data_directory() {
    static const std::string directory = [] {
        const std::string path = with_separator(from_environment("PURR_VR_DATA", "vr/"));
        platform::make_directories(path);
        return path;
    }();
    return directory.c_str();
}
