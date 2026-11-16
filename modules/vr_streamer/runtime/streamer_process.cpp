#include "streamer_process.hpp"

#include "pin_prompt.hpp"
#include "platform.hpp"
#include "runtime_log.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace streamer_process {

namespace {

// A running streamer, with its stdin on a pipe the runtime holds: if the game
// dies, the pipe closes and the streamer leaves too. On macOS its stdout comes
// here too: its status lines, which include the Steam Link PIN prompt.
struct Child {
#ifdef _WIN32
    HANDLE process = nullptr;
    HANDLE stdin_writer = nullptr;
#else
    pid_t pid = 0;
    int stdin_writer = -1;
    int stdout_reader = -1;
#endif
    bool running() const {
#ifdef _WIN32
        return process != nullptr;
#else
        return pid > 0;
#endif
    }
};

Child spawn(const std::vector<std::string>& options) {
    Child child;
    const std::string program = platform::library_directory() + platform::streamer_executable();
#ifdef _WIN32
    SECURITY_ATTRIBUTES inherit{sizeof(inherit), nullptr, TRUE};
    HANDLE read_end = nullptr;
    HANDLE write_end = nullptr;
    if (!CreatePipe(&read_end, &write_end, &inherit, 0)) {
        return child;
    }
    SetHandleInformation(write_end, HANDLE_FLAG_INHERIT, 0);
    std::string command = "\"" + program + "\"";
    for (const std::string& option : options) {
        command += " \"" + option + "\"";
    }
    std::wstring wide(command.size() * 2 + 1, L'\0');
    wide.resize(MultiByteToWideChar(CP_UTF8, 0, command.c_str(), -1, wide.data(), static_cast<int>(wide.size())) - 1);
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = read_end;
    startup.hStdOutput = nullptr;
    startup.hStdError = nullptr;
    PROCESS_INFORMATION info{};
    const BOOL started = CreateProcessW(nullptr, wide.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
                                        &startup, &info);
    CloseHandle(read_end);
    if (!started) {
        CloseHandle(write_end);
        runtime_logf("cannot start the streamer %s (%lu)", program.c_str(), GetLastError());
        return child;
    }
    CloseHandle(info.hThread);
    child.process = info.hProcess;
    child.stdin_writer = write_end;
#else
    int pipe_ends[2];
    if (pipe(pipe_ends) != 0) {
        return child;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, pipe_ends[0], STDIN_FILENO);
    posix_spawn_file_actions_addclose(&actions, pipe_ends[1]);
#ifdef __APPLE__
    int output_ends[2] = {-1, -1};
    if (pipe(output_ends) != 0) {
        close(pipe_ends[0]);
        close(pipe_ends[1]);
        posix_spawn_file_actions_destroy(&actions);
        return child;
    }
    posix_spawn_file_actions_adddup2(&actions, output_ends[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, output_ends[0]);
#else
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
#endif
    std::vector<std::string> arguments = {program};
    arguments.insert(arguments.end(), options.begin(), options.end());
    std::vector<char*> argv;
    for (std::string& argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int error = posix_spawn(&pid, program.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(pipe_ends[0]);
#ifdef __APPLE__
    close(output_ends[1]);
#endif
    if (error != 0) {
        close(pipe_ends[1]);
#ifdef __APPLE__
        close(output_ends[0]);
#endif
        runtime_logf("cannot start the streamer %s (%d)", program.c_str(), error);
        return child;
    }
    child.pid = pid;
    child.stdin_writer = pipe_ends[1];
#ifdef __APPLE__
    child.stdout_reader = output_ends[0];
#endif
#endif
    return child;
}

// Waits for it to leave (without touching its stdin); false if it failed.
bool wait_for(const Child& child) {
#ifdef _WIN32
    WaitForSingleObject(child.process, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(child.process, &code);
    // (The handle is closed by the keeper under the lock: stop() may still be ending it.)
    if (code != 0) {
        runtime_logf("the streamer stopped (status %lu)", code);
    }
    return code == 0;
#else
    int status = 0;
    waitpid(child.pid, &status, 0);
    const bool clean = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!clean) {
        runtime_logf("the streamer stopped (status %d)", status);
    }
    return clean;
#endif
}

// Closes the runtime's end of its stdin, once: the streamer then leaves.
void close_stdin(Child& child) {
#ifdef _WIN32
    if (child.stdin_writer) {
        CloseHandle(child.stdin_writer);
        child.stdin_writer = nullptr;
    }
#else
    if (child.stdin_writer >= 0) {
        close(child.stdin_writer);
        child.stdin_writer = -1;
    }
#endif
}

// Asks it to leave (it exits within its SHUTDOWN_TIME).
void ask_to_stop(Child& child) {
#ifndef _WIN32
    kill(child.pid, SIGTERM);
#endif
    close_stdin(child);
}

void end_now(const Child& child) {
#ifdef _WIN32
    TerminateProcess(child.process, 1);
#else
    kill(child.pid, SIGKILL);
#endif
}

std::atomic<bool> g_headset_connected{false};  // the current child streams to a headset
std::function<void(bool)> g_vision_pro_listener;
std::mutex g_lock;
std::condition_variable g_changed;
// The current streamer (below), for answering its PIN prompt.
Child g_child;

#ifdef __APPLE__
// A string field of one of the streamer's JSON status lines (flat, no escapes in what it sends).
std::string json_field(const std::string& line, const std::string& name) {
    const std::string key = "\"" + name + "\":\"";
    const std::size_t start = line.find(key);
    if (start == std::string::npos) {
        return {};
    }
    const std::size_t from = start + key.size();
    const std::size_t end = line.find('"', from);
    return end == std::string::npos ? std::string() : line.substr(from, end - from);
}

// Writes a command line to the current streamer's stdin, if it's the one that asked.
void tell(pid_t pid, const std::string& command) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (g_child.pid == pid && g_child.stdin_writer >= 0) {
        const std::string line = command + "\n";
        (void)!write(g_child.stdin_writer, line.data(), line.size());
    }
}

// Reads the streamer's status lines until it leaves (streamer/src/status.rs), acting on the
// Steam Link pairing ones; the rest (its state, once a second while streaming) is for programs
// that show it.
void read_status(int reader, pid_t pid) {
    std::string pending;
    char buffer[1024];
    bool wrong = false;
    for (;;) {
        const ssize_t count = read(reader, buffer, sizeof(buffer));
        if (count <= 0) {
            break;
        }
        pending.append(buffer, static_cast<std::size_t>(count));
        std::size_t end;
        while ((end = pending.find('\n')) != std::string::npos) {
            const std::string line = pending.substr(0, end);
            pending.erase(0, end + 1);
            const std::string event = json_field(line, "event");
            if (event == "connected") {
                g_headset_connected = true;
            } else if (event == "vision_pro") {
                std::function<void(bool)> listener;
                {
                    std::lock_guard<std::mutex> lock(g_lock);
                    listener = g_vision_pro_listener;
                }
                if (listener) {
                    listener(line.find("\"nearby\":true") != std::string::npos);
                }
            } else if (event == "pin_needed") {
                pin_prompt::ask(wrong, [pid](std::optional<std::string> pin) {
                    tell(pid, pin ? "pin " + *pin : "pin cancel");
                });
                wrong = false;
            } else if (event == "pin_wrong") {
                wrong = true;
            } else if (event == "pin_cancelled" || event == "paired") {
                pin_prompt::dismiss();
                wrong = false;
            }
        }
    }
    close(reader);
    pin_prompt::dismiss();
}
#endif
std::thread g_keeper;
bool g_wanted = false;
bool g_exited = false;  // set by the keeper when the current child has left

// Runs the streamer, starting it again whenever it leaves (each headset
// connection ends it), until it's no longer wanted.
void keep_running(std::vector<std::string> options) {
    std::unique_lock<std::mutex> lock(g_lock);
    while (g_wanted) {
        Child child = spawn(options);
        if (!child.running()) {
            g_wanted = false;  // not there to start: VR runs on without a headset
            break;
        }
        g_child = child;
        g_exited = false;
        lock.unlock();
#ifdef __APPLE__
        std::thread status_reader(read_status, child.stdout_reader, child.pid);
#endif
        wait_for(child);
#ifdef __APPLE__
        status_reader.join();  // its stdout closed as it left
#endif
        lock.lock();
        close_stdin(g_child);
#ifdef _WIN32
        CloseHandle(child.process);
#endif
        g_child = Child{};
        g_exited = true;
        g_headset_connected = false;
        g_changed.notify_all();
        // A moment before the next one, so one that fails at once doesn't spin.
        g_changed.wait_for(lock, std::chrono::seconds(1), [] { return !g_wanted; });
    }
}

}  // namespace

void start(const std::string& frames_path, unsigned eye_width, unsigned eye_height, double hz) {
    std::unique_lock<std::mutex> lock(g_lock);
    if (g_wanted) {
        return;
    }
    if (g_keeper.joinable()) {  // one that gave up, finished
        lock.unlock();
        g_keeper.join();
        lock.lock();
    }
    // Started by another program (PURR_VR_STREAMER=external): it reads the frames the same way.
    const char* streamer = std::getenv("PURR_VR_STREAMER");
    if (streamer && std::string(streamer) == "external") {
        return;
    }
    g_wanted = true;
    std::vector<std::string> options = {"--frames", frames_path, "--data", runtime_data_directory(),
                                        "--eye", std::to_string(eye_width) + "x" + std::to_string(eye_height),
                                        "--fps", std::to_string(static_cast<int>(hz))};
    const char* controllers = std::getenv("PURR_VR_CONTROLLERS");
    options.push_back("--controllers");
    options.push_back(controllers && std::string(controllers) == "0" ? "0" : "1");
    g_keeper = std::thread(keep_running, options);
}

void stop() {
    {
        std::unique_lock<std::mutex> lock(g_lock);
        if (!g_wanted) {
            lock.unlock();
            if (g_keeper.joinable()) {
                g_keeper.join();
            }
            return;
        }
        g_wanted = false;
        g_changed.notify_all();
        if (g_child.running()) {
            // A clean exit lets the headset go and gives the computer its
            // sound back; one that hangs is ended after a moment.
            const Child child = g_child;
            ask_to_stop(g_child);
            if (!g_changed.wait_for(lock, std::chrono::seconds(3), [] { return g_exited; })) {
                end_now(child);
            }
        }
    }
    g_keeper.join();
}

bool headset_connected() {
    return g_headset_connected;
}

void on_vision_pro(std::function<void(bool nearby)> listener) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_vision_pro_listener = std::move(listener);
}

}  // namespace streamer_process
