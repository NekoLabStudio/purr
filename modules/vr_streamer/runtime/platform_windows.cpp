#ifdef _WIN32

#include "platform.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <thread>

namespace platform {

std::uint64_t monotonic_ns() {
    static const LONGLONG frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value.QuadPart;
    }();
    LARGE_INTEGER now{};
    QueryPerformanceCounter(&now);
    return static_cast<std::uint64_t>(now.QuadPart / frequency) * 1000000000ULL +
           static_cast<std::uint64_t>(now.QuadPart % frequency) * 1000000000ULL / static_cast<std::uint64_t>(frequency);
}

std::uint64_t unix_ns() {
    FILETIME time{};
    GetSystemTimePreciseAsFileTime(&time);
    const std::uint64_t ticks = (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
    // 100 ns ticks since 1601.
    return (ticks - 116444736000000000ULL) * 100ULL;
}

void sleep_until(std::uint64_t target) {
    for (;;) {
        const std::uint64_t now = monotonic_ns();
        if (now >= target) {
            return;
        }
        const std::uint64_t remaining = target - now;
        if (remaining > 2000000) {
            Sleep(static_cast<DWORD>((remaining - 1000000) / 1000000));
        } else {
            std::this_thread::yield();
        }
    }
}

std::uint64_t process_id() {
    return GetCurrentProcessId();
}

namespace {

std::string narrow(const wchar_t* text) {
    const int length = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    std::string out(length > 0 ? length - 1 : 0, '\0');
    if (length > 1) {
        WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), length, nullptr, nullptr);
    }
    return out;
}

std::wstring widen(const std::string& text) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    std::wstring out(length > 0 ? length - 1 : 0, L'\0');
    if (length > 1) {
        MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, out.data(), length);
    }
    return out;
}

}  // namespace

std::string temporary_directory() {
    wchar_t buffer[MAX_PATH + 1] = {};
    const DWORD length = GetTempPathW(MAX_PATH + 1, buffer);
    std::string folder = length ? narrow(buffer) : std::string("C:\\Windows\\Temp\\");
    if (folder.back() != '\\' && folder.back() != '/') {
        folder += '\\';
    }
    return folder;
}

std::string library_directory() {
    HMODULE module = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&library_directory), &module)) {
        return std::string();
    }
    wchar_t buffer[MAX_PATH] = {};
    GetModuleFileNameW(module, buffer, MAX_PATH);
    const std::string path = narrow(buffer);
    return path.substr(0, path.find_last_of("\\/") + 1);
}

void make_directories(const std::string& path) {
    for (size_t at = 3; at < path.size(); ++at) {
        if (path[at] == '/' || path[at] == '\\') {
            CreateDirectoryW(widen(path.substr(0, at)).c_str(), nullptr);
        }
    }
}

bool map_shared_file(const std::string& path, std::size_t size, Mapping& out) {
    // Deleted when the last handle closes, so a crashed game leaves nothing behind.
    HANDLE file = CreateFileW(widen(path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    const ULARGE_INTEGER length{.QuadPart = static_cast<ULONGLONG>(size)};
    HANDLE section = CreateFileMappingW(file, nullptr, PAGE_READWRITE, length.HighPart, length.LowPart, nullptr);
    if (!section) {
        CloseHandle(file);
        return false;
    }
    void* view = MapViewOfFile(section, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!view) {
        CloseHandle(section);
        CloseHandle(file);
        return false;
    }
    out.view = view;
    out.size = size;
    out.file = file;
    out.section = section;
    return true;
}

void unmap_shared_file(Mapping& mapping, const std::string& path) {
    if (mapping.view) {
        UnmapViewOfFile(mapping.view);
    }
    if (mapping.section) {
        CloseHandle(mapping.section);
    }
    if (mapping.file) {
        CloseHandle(mapping.file);
    }
    mapping = Mapping{};
    DeleteFileW(widen(path).c_str());
}

const char* streamer_executable() {
    return "vr_streamer.exe";
}

}  // namespace platform

#endif  // _WIN32
