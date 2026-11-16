#pragma once

// What the runtime needs from the operating system, the same on macOS, Linux and Windows:
// platform_posix.cpp and platform_windows.cpp.

#include <cstddef>
#include <cstdint>
#include <string>

namespace platform {

// A steady clock for OpenXR's times, and the wall clock (the streamer compares that one).
std::uint64_t monotonic_ns();
std::uint64_t unix_ns();
void sleep_until(std::uint64_t monotonic_target_ns);

std::uint64_t process_id();
// With a trailing separator.
std::string temporary_directory();
// The folder holding this library (and the streamer beside it), with a trailing separator.
std::string library_directory();
void make_directories(const std::string& path);

// A file both processes map: the runtime writes frames, the streamer reads them.
struct Mapping {
    void* view = nullptr;
    std::size_t size = 0;
#ifdef _WIN32
    void* file = nullptr;
    void* section = nullptr;
#endif
};
// Creates (or empties) the file at this size and maps it; false if it can't.
bool map_shared_file(const std::string& path, std::size_t size, Mapping& out);
void unmap_shared_file(Mapping& mapping, const std::string& path);

// The streamer's executable name in library_directory().
const char* streamer_executable();

}  // namespace platform
