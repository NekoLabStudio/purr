#ifndef _WIN32

#include "platform.hpp"

#include <dlfcn.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <climits>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace platform {

std::uint64_t monotonic_ns() {
#ifdef __APPLE__
    return clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    timespec now{};
    clock_gettime(CLOCK_MONOTONIC, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(now.tv_nsec);
#endif
}

std::uint64_t unix_ns() {
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    return static_cast<std::uint64_t>(now.tv_sec) * 1000000000ULL + static_cast<std::uint64_t>(now.tv_nsec);
}

void sleep_until(std::uint64_t target) {
    for (;;) {
        const std::uint64_t now = monotonic_ns();
        if (now >= target) {
            return;
        }
        const std::uint64_t remaining = target - now;
        if (remaining > 2000000) {
            usleep(static_cast<useconds_t>((remaining - 1000000) / 1000));
        } else {
            sched_yield();
        }
    }
}

std::uint64_t process_id() {
    return static_cast<std::uint64_t>(getpid());
}

std::string temporary_directory() {
#ifdef __APPLE__
    char temporary[PATH_MAX] = {};
    if (confstr(_CS_DARWIN_USER_TEMP_DIR, temporary, sizeof(temporary)) != 0) {
        return temporary;
    }
#endif
    const char* value = std::getenv("TMPDIR");
    std::string folder = value && value[0] ? value : "/tmp";
    if (folder.back() != '/') {
        folder += '/';
    }
    return folder;
}

std::string library_directory() {
    Dl_info info{};
    if (!dladdr(reinterpret_cast<const void*>(&library_directory), &info) || !info.dli_fname) {
        return std::string();
    }
    const std::string path = info.dli_fname;
    return path.substr(0, path.rfind('/') + 1);
}

void make_directories(const std::string& path) {
    for (size_t at = 1; at < path.size(); ++at) {
        if (path[at] == '/') {
            mkdir(path.substr(0, at).c_str(), 0755);
        }
    }
}

bool map_shared_file(const std::string& path, std::size_t size, Mapping& out) {
    const int file = open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (file < 0 || ftruncate(file, static_cast<off_t>(size)) != 0) {
        if (file >= 0) {
            close(file);
        }
        return false;
    }
    void* view = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, file, 0);
    close(file);
    if (view == MAP_FAILED) {
        unlink(path.c_str());
        return false;
    }
    out.view = view;
    out.size = size;
    return true;
}

void unmap_shared_file(Mapping& mapping, const std::string& path) {
    if (mapping.view) {
        munmap(mapping.view, mapping.size);
    }
    mapping = Mapping{};
    unlink(path.c_str());
}

const char* streamer_executable() {
    return "vr_streamer";
}

}  // namespace platform

#endif  // !_WIN32
