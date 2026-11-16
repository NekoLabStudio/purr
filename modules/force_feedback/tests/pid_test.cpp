// HID PID, as the kernel's hid-pidff.c drives it, against OpenFFBoard's
// real descriptor (descriptors/openffboard.h).

#include "descriptors/openffboard.h"
#include "drivers/hid_pid/pid.h"
#include "recording_transport.h"
#include "test.h"

namespace {

ReportDescriptor openffboard() {
    return ReportDescriptor::parse(Bytes(std::begin(kOpenFfboardDescriptor), std::end(kOpenFfboardDescriptor)));
}

// OpenFFBoard answering: its Pool (report 0x13: 40 bytes of memory, 40
// effects at once, the device managing the pool) and Block Load (0x12) for
// three effects made in turn, blocks 1 to 3, each a success (status 1, the
// first usage of its array).
void answers(RecordingTransport& board) {
    board.features[0x13] = {{0x13, 0x28, 0x00, 0x28, 0x03}};
    board.features[0x12] = {
        {0x12, 0x01, 0x01, 0x00, 0x00}, {0x12, 0x02, 0x01, 0x00, 0x00}, {0x12, 0x03, 0x01, 0x00, 0x00}};
}

}  // namespace

TEST(pid_finds_openffboards_reports) {
    RecordingTransport board;
    PidDriver driver(board, openffboard());
    CHECK_EQ(driver.missing(), std::string());
}

TEST(pid_open) {
    RecordingTransport board;
    answers(board);
    PidDriver driver(board, openffboard());
    CHECK(driver.open());
    const std::vector<Bytes> sent = board.reports();
    CHECK_EQ(sent, (std::vector<Bytes>{
                       // Device Control as OpenFFBoard declares it, a bitmask: Reset twice, Stop
                       // All Effects, Enable Actuators (pidff_reset).
                       {0x0C, 0x08},
                       {0x0C, 0x08},
                       {0x0C, 0x04},
                       {0x0C, 0x01},
                       {0x0D, 0xFF},  // Device Gain full
                       // Create New Effect (a feature report): constant force, the first
                       // effect type, so 1 (its place plus one).
                       {0x11, 0x01, 0x00, 0x00},
                       // Set Effect for block 1: duration all ones (endless), no trigger repeat,
                       // sample period or delay, gain full, trigger button 0, direction enable
                       // set, direction 90 degrees (0x4000 of 0xFFFF across 0 to 36000: 9000).
                       {0x01, 0x01, 0x01, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x02, 0x28,
                        0x23, 0x00, 0x00},
                       {0x05, 0x01, 0x00, 0x00},  // Set Constant: nothing yet
                       {0x0A, 0x01, 0x01, 0x01},  // Effect Operation: start, once
                       // The damper: type 0x41 is the ninth effect type.
                       {0x11, 0x09, 0x00, 0x00},
                       {0x01, 0x02, 0x09, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x02, 0x28,
                        0x23, 0x00, 0x00},
                       // Set Condition for each axis (it has a parameter block offset): no
                       // coefficient, saturation full on the wheel's axis.
                       {0x03, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x00},
                       {0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                       {0x0A, 0x02, 0x01, 0x01},
                       // Friction: type 0x43, the eleventh, a condition as the damper is.
                       {0x11, 0x0B, 0x00, 0x00},
                       {0x01, 0x03, 0x0B, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x02, 0x28,
                        0x23, 0x00, 0x00},
                       {0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x00},
                       {0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                       {0x0A, 0x03, 0x01, 0x01},
                   }));
    CHECK(board.sent[5].type == ReportType::feature);
    CHECK(board.sent[0].type == ReportType::output);
}

TEST(pid_force_each_way) {
    RecordingTransport board;
    answers(board);
    PidDriver driver(board, openffboard());
    driver.open();
    const std::size_t from = board.sent.size();
    // At 90 degrees with direction enable, a positive magnitude turns the
    // wheel to the left (Wine's DirectInput, OpenFFBoard's firmware and
    // DirectInput's convention agree; pid.h). Magnitude -32767 to 32767.
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(-1.0));
    CHECK(driver.set_force(0.5));
    CHECK_EQ(board.reports(from), (std::vector<Bytes>{
                                      {0x05, 0x01, 0xFF, 0x7F},
                                      {0x05, 0x01, 0x01, 0x80},
                                      {0x05, 0x01, 0x00, 0x40},
                                  }));
}

TEST(pid_damper_and_close) {
    RecordingTransport board;
    answers(board);
    PidDriver driver(board, openffboard());
    driver.open();
    std::size_t from = board.sent.size();
    CHECK(driver.set_damper(1.0));
    CHECK_EQ(board.reports(from),
             (std::vector<Bytes>{
                 {0x03, 0x02, 0x00, 0x00, 0x00, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x00},
                 {0x03, 0x02, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
             }));
    driver.set_force(0.25);
    from = board.sent.size();
    driver.close();
    const std::vector<Bytes> closing = board.reports(from);
    CHECK_EQ(closing.size(), std::size_t{10});
    CHECK_EQ(closing[0], (Bytes{0x05, 0x01, 0x00, 0x00}));       // no force
    CHECK_EQ(closing[3], (Bytes{0x0A, 0x01, 0x03, 0x00}));       // stopped: the third operation
    CHECK_EQ(closing[4], (Bytes{0x0B, 0x01}));                   // freed
    CHECK_EQ(closing[6], (Bytes{0x0B, 0x02}));
    CHECK_EQ(closing[8], (Bytes{0x0B, 0x03}));
    CHECK_EQ(closing[9], (Bytes{0x0C, 0x04}));                   // everything stopped
}

TEST(pid_full_memory_doesnt_open) {
    RecordingTransport board;
    answers(board);
    board.features[0x12] = {{0x12, 0x00, 0x02, 0x00, 0x00}};  // status 2: full
    PidDriver driver(board, openffboard());
    CHECK(!driver.open());
}

TEST(pid_unmanaged_pool_doesnt_open) {
    // The kernel turns away a device that leaves effect memory to the host.
    RecordingTransport board;
    answers(board);
    board.features[0x13] = {{0x13, 0x28, 0x00, 0x28, 0x02}};
    PidDriver driver(board, openffboard());
    CHECK(!driver.open());
}

TEST(pid_without_reports_says_what_it_lacks) {
    RecordingTransport board;
    PidDriver driver(board, ReportDescriptor::parse({0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0xC0}));
    CHECK(!driver.missing().empty());
    CHECK(!driver.open());
    CHECK(board.sent.empty());
}

TEST(pid_friction) {
    RecordingTransport board;
    answers(board);
    PidDriver driver(board, openffboard());
    driver.open();
    const std::size_t from = board.sent.size();
    CHECK(driver.set_friction(0.5));
    CHECK(driver.set_friction(0.5));
    CHECK_EQ(board.reports(from),
             (std::vector<Bytes>{
                 {0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x40, 0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x00},
                 {0x03, 0x03, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
             }));
    CHECK_EQ(driver.max_update_hz(), 1000);
}

TEST(pid_without_room_for_friction_plays_on) {
    // A device that makes two effects at most still plays force and damper.
    RecordingTransport board;
    answers(board);
    board.features[0x12] = {{0x12, 0x01, 0x01, 0x00, 0x00}, {0x12, 0x02, 0x01, 0x00, 0x00}, {0x12, 0x00, 0x02, 0x00, 0x00}};
    PidDriver driver(board, openffboard());
    CHECK(driver.open());
    const std::size_t from = board.sent.size();
    CHECK(driver.set_friction(1.0));
    CHECK_EQ(board.sent.size(), from);
}

TEST(pid_endless_duration) {
    // hid-pidff's pidff_set_duration: all ones in a 16-bit field (0xFFFF,
    // PID_INFINITE), whatever its logical maximum (OpenFFBoard declares
    // 0x7FFF and takes 0xFFFF as endless). A 32-bit field with a logical
    // maximum of 0x7FFFFFFF gets that, in its range and endless either way.
    HidField sixteen;
    sixteen.size = 16;
    sixteen.logical_max = 0x7FFF;
    CHECK_EQ(PidDriver::endless(sixteen), std::int64_t{0xFFFF});
    HidField eight;
    eight.size = 8;
    eight.logical_max = 0xFF;
    CHECK_EQ(PidDriver::endless(eight), std::int64_t{0xFF});
    HidField wide;
    wide.size = 32;
    wide.logical_max = 0x7FFFFFFF;
    CHECK_EQ(PidDriver::endless(wide), std::int64_t{0x7FFFFFFF});
    wide.logical_max = 1000;
    CHECK_EQ(PidDriver::endless(wide), std::int64_t{0xFFFF});
}
