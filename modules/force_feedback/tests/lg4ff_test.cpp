// lg4ff: berarma/new-lg4ff's hid-lg4ff.c for the effects and the range
// (lg4ff_update_slot, lg4ff_init_slots, lg4ff_set_range_g25 and _dfp), the
// constant force's level and sign as the game sent them before this driver
// (felt right on Ivo's G923), and hid-lg4ff.c's compatibility mode switches.

#include "drivers/lg4ff/lg4ff.h"
#include "recording_transport.h"
#include "test.h"

namespace {

constexpr std::uint16_t kG923 = 0xC266;
constexpr std::uint16_t kG27 = 0xC29B;
constexpr std::uint16_t kDrivingForcePro = 0xC298;
constexpr std::uint16_t kMomo = 0xC295;

}  // namespace

TEST(lg4ff_open_stops_everything_frees_the_wheel_and_downloads_the_force) {
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    CHECK(driver.open());
    CHECK_EQ(wheel.reports(), (std::vector<Bytes>{
                                  {0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // stop all four slots
                                  {0xF5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},  // autocentre off
                                  // lg4ff_init_slots: slot 0's constant force downloaded at nothing.
                                  {0x11, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00},
                              }));
    CHECK(wheel.sent[0].type == ReportType::output);
    CHECK_EQ(int(wheel.sent[0].id), 0);
    CHECK_EQ(driver.max_update_hz(), 500);  // new-lg4ff's 2 ms timer
}

TEST(lg4ff_formula_force_ex_takes_its_own_autocentre_command) {
    // hid-lg4ff.c, lg4ff_set_autocenter_ffex with no magnitude.
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, 0xC294, true);
    CHECK(driver.open());
    CHECK_EQ(wheel.reports()[1], (Bytes{0xFE, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00}));
    CHECK(lg4ff::is_formula_force_ex(0xC294, 0x2100));
    CHECK(!lg4ff::is_formula_force_ex(0xC294, 0x1350));
}

TEST(lg4ff_force_each_way_is_refreshed_never_stopped) {
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    driver.open();
    const std::size_t from = wheel.sent.size();
    // Above 0x80 turns the wheel to the left: measured on a G923. Once
    // downloaded, the force is refreshed (op 0x0C), and through zero it's
    // refreshed to nothing (0x80) rather than stopped and downloaded again:
    // lg4ff_update_slot only ever stops a condition slot.
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(1.0));  // a repeat sends nothing
    CHECK(driver.set_force(-1.0));
    CHECK(driver.set_force(0.5));
    CHECK(driver.set_force(0.0));
    CHECK(driver.set_force(-0.3));
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0x1C, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00},
                                      {0x1C, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00},
                                      {0x1C, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00},
                                      {0x1C, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00},
                                      {0x1C, 0x00, 0x5A, 0x00, 0x00, 0x00, 0x00},
                                  }));
}

TEST(lg4ff_damper_resists_in_sixteen_steps) {
    // lg4ff_update_slot's FF_DAMPER in slot 2 (0x40): type 0x0C, SCALE_COEFF
    // to 4 bits each way with the sign bytes 0 (a positive coefficient,
    // resisting), clip full. Downloaded (op 1) as it starts, refreshed (0x0C)
    // as it changes, stopped (3) at nothing.
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK(driver.set_damper(1.0));
    CHECK(driver.set_damper(2.0));    // clamped: the same as 1, so nothing new
    CHECK(driver.set_damper(0.5));   // 16384 * 2 >> 12 = 8
    CHECK(driver.set_damper(0.52));  // still 8: nothing new
    CHECK(driver.set_damper(0.0));
    CHECK(driver.set_damper(0.0));
    CHECK(driver.set_damper(0.3));    // 9830 * 2 >> 12 = 4, downloaded again
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0x41, 0x0C, 0x0F, 0x00, 0x0F, 0x00, 0xFF},
                                      {0x4C, 0x0C, 0x08, 0x00, 0x08, 0x00, 0xFF},
                                      {0x43, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                      {0x41, 0x0C, 0x04, 0x00, 0x04, 0x00, 0xFF},
                                  }));
}

TEST(lg4ff_friction_where_the_wheel_has_it) {
    // A G27 has a friction effect (LG4FF_CAP_FRICTION): type 0x0E in slot 3
    // (0x80), SCALE_COEFF to 8 bits, clip full, the signs 0.
    RecordingTransport g27;
    Lg4ffDriver with(g27, kG27, false);
    with.open();
    std::size_t from = g27.sent.size();
    CHECK(with.set_friction(1.0));
    CHECK(with.set_friction(0.5));
    CHECK(with.set_friction(0.0));
    CHECK_EQ(g27.reports(from), (std::vector<Bytes>{
                                    {0x81, 0x0E, 0xFF, 0xFF, 0xFF, 0x00, 0x00},
                                    {0x8C, 0x0E, 0x80, 0x80, 0xFF, 0x00, 0x00},
                                    {0x83, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                }));
    // The G923 hasn't: new-lg4ff casts friction to a damper in its slot.
    RecordingTransport g923;
    Lg4ffDriver without(g923, kG923, false);
    without.open();
    from = g923.sent.size();
    CHECK(without.set_friction(1.0));
    CHECK_EQ(g923.reports(from), (std::vector<Bytes>{{0x81, 0x0C, 0x0F, 0x00, 0x0F, 0x00, 0xFF}}));
}

TEST(lg4ff_play_sends_each_effect_that_changed) {
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK(driver.play(0.5, 1.0, 0.0));
    CHECK(driver.play(0.5, 1.0, 0.0));  // nothing new
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0x1C, 0x00, 0xC0, 0x00, 0x00, 0x00, 0x00},
                                      {0x41, 0x0C, 0x0F, 0x00, 0x0F, 0x00, 0xFF},
                                  }));
}

TEST(lg4ff_rotation) {
    // lg4ff_set_range_g25: F8 81, the degrees little-endian, from 40 to 900.
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    driver.open();
    std::size_t from = wheel.sent.size();
    CHECK_EQ(driver.rotation(), 0);  // it can't be read
    CHECK_EQ(driver.set_rotation(540), 540);
    CHECK_EQ(driver.set_rotation(540), 540);  // nothing new
    CHECK_EQ(driver.set_rotation(1080), 900);
    CHECK_EQ(driver.set_rotation(10), 40);
    CHECK_EQ(driver.rotation(), 40);
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0xF8, 0x81, 0x1C, 0x02, 0x00, 0x00, 0x00},
                                      {0xF8, 0x81, 0x84, 0x03, 0x00, 0x00, 0x00},
                                      {0xF8, 0x81, 0x28, 0x00, 0x00, 0x00, 0x00},
                                  }));
    // Let go, it goes back to 900, where new-lg4ff starts every wheel.
    from = wheel.sent.size();
    driver.close();
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                      {0xF8, 0x81, 0x84, 0x03, 0x00, 0x00, 0x00},
                                  }));
}

TEST(lg4ff_rotation_left_alone_unless_set) {
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    driver.open();
    const std::size_t from = wheel.sent.size();
    driver.close();
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{{0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}));
}

TEST(lg4ff_driving_force_pro_rotation) {
    // lg4ff_set_range_dfp: a coarse limit (F8 03 for 900, F8 02 for 200),
    // then a fine one inside it; none at 200 or 900.
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kDrivingForcePro, false);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK_EQ(driver.set_rotation(540), 540);
    CHECK_EQ(driver.set_rotation(900), 900);
    CHECK_EQ(driver.set_rotation(200), 200);
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      // Left starts at (900 - 540 + 1) * 2047 / 900 = 821, right at 0xFFF - 821.
                                      {0xF8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
                                      {0x81, 0x0B, 0x33, 0xCC, 0xFF, 0xA4, 0xFF},
                                      {0xF8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00},
                                      {0x81, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00},
                                      {0xF8, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00},
                                      {0x81, 0x0B, 0x00, 0x00, 0x00, 0x00, 0x00},
                                  }));
}

TEST(lg4ff_momo_cant_set_its_rotation) {
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kMomo, false);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK_EQ(driver.set_rotation(540), 0);
    CHECK_EQ(wheel.sent.size(), from);
}

TEST(lg4ff_stop_lets_go_and_starts_again) {
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    driver.open();
    driver.set_force(0.3);
    driver.set_damper(0.5);
    const std::size_t from = wheel.sent.size();
    driver.stop();
    driver.set_force(0.3);  // downloaded again after a stop
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0xF3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                      {0x11, 0x00, 0xA6, 0x00, 0x00, 0x00, 0x00},
                                  }));
}

TEST(lg4ff_unplugged_wheel_stops_answering) {
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    driver.open();
    wheel.writes_fail = true;
    CHECK(!driver.set_force(0.5));
}

TEST(lg4ff_native_mode_switches) {
    // hid-lg4ff.c: a wheel reporting itself as a Driving Force (C294) is told
    // by its release number and switched with its own native mode command.
    const Bytes revert = {0xF8, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto g29 = lg4ff::native_mode_switch(0xC294, 0x1350);
    CHECK(g29.has_value());
    CHECK_EQ(g29->native_product, 0xC24F);
    CHECK_EQ(g29->commands, (std::vector<Bytes>{revert, {0xF8, 0x09, 0x05, 0x01, 0x01, 0x00, 0x00}}));
    CHECK_EQ(lg4ff::native_mode_switch(0xC294, 0x8901)->native_product, 0xC24F);
    const auto g27 = lg4ff::native_mode_switch(0xC294, 0x1238);
    CHECK_EQ(g27->native_product, 0xC29B);
    CHECK_EQ(g27->commands, (std::vector<Bytes>{revert, {0xF8, 0x09, 0x04, 0x01, 0x00, 0x00, 0x00}}));
    const auto dfgt = lg4ff::native_mode_switch(0xC294, 0x1300);
    CHECK_EQ(dfgt->native_product, 0xC29A);
    CHECK_EQ(dfgt->commands[1], (Bytes{0xF8, 0x09, 0x03, 0x01, 0x00, 0x00, 0x00}));
    const auto g25 = lg4ff::native_mode_switch(0xC294, 0x1200);
    CHECK_EQ(g25->native_product, 0xC299);
    CHECK_EQ(g25->commands, (std::vector<Bytes>{{0xF8, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00}}));
    const auto dfp = lg4ff::native_mode_switch(0xC294, 0x1000);
    CHECK_EQ(dfp->native_product, 0xC298);
    CHECK_EQ(dfp->commands, (std::vector<Bytes>{{0xF8, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00}}));
    // A real Driving Force or Formula Force EX has no other mode.
    CHECK(!lg4ff::native_mode_switch(0xC294, 0x2100).has_value());
    // new-lg4ff: the G923 in PlayStation mode, on report 0x30.
    const auto g923 = lg4ff::native_mode_switch(0xC267, 0x3800);
    CHECK_EQ(int(g923->report_id), 0x30);
    CHECK_EQ(g923->native_product, 0xC266);
    CHECK_EQ(g923->commands, (std::vector<Bytes>{{0x30, 0xF8, 0x09, 0x07, 0x01, 0x01, 0x00, 0x00}}));
    CHECK(!lg4ff::native_mode_switch(0xC24F, 0x1350).has_value());
}

TEST(lg4ff_rev_lights) {
    // lg4ff_set_leds: F8 12 and five bits, sent only as they change, out as the wheel is let go.
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kG923, false);
    driver.open();
    wheel.sent.clear();
    CHECK(driver.set_leds(0x07));
    CHECK(driver.set_leds(0x07));
    CHECK(driver.set_leds(0xFF));  // only five lights
    CHECK_EQ(wheel.reports(), (std::vector<Bytes>{
                                  {0xF8, 0x12, 0x07, 0x00, 0x00, 0x00, 0x00},
                                  {0xF8, 0x12, 0x1F, 0x00, 0x00, 0x00, 0x00},
                              }));
    wheel.sent.clear();
    driver.close();
    CHECK_EQ(wheel.reports().back(), (Bytes{0xF8, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00}));
}

TEST(lg4ff_wheels_without_rev_lights_ignore_them) {
    RecordingTransport wheel;
    Lg4ffDriver driver(wheel, kDrivingForcePro, false);
    driver.open();
    wheel.sent.clear();
    CHECK(driver.set_leds(0x1F));
    CHECK(wheel.sent.empty());
}
