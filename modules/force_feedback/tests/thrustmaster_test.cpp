// Thrustmaster: the boot switch (hid-thrustmaster.c, tmdrv,
// crossover-wheel), the T150 and TMX (crossover-wheel's PROTOCOL.md) and
// the T300 family (hid-tmff2's hid-tmt300rs.c).

#include "drivers/thrustmaster/boot_switch.h"
#include "drivers/thrustmaster/t150.h"
#include "drivers/thrustmaster/t300.h"
#include "recording_transport.h"
#include "test.h"


TEST(thrustmaster_boot_switch_values) {
    // A T150 answering the model query, as crossover-wheel's probe_ep0
    // measured it (RESEARCH.md A6): attachment 0x06, model 0x03.
    const Bytes t150 = {0x49, 0x00, 0x21, 0x00, 0x00, 0x00, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    CHECK_EQ(thrustmaster::wheel_type(t150), std::uint16_t{0x0306});
    CHECK_EQ(thrustmaster::switch_at_boot(t150, 0).value_or(0), std::uint16_t{0x0006});
    // hid-thrustmaster.c's T300RS rows all switch with 5.
    for (const std::uint8_t attachment : {0x00, 0x06, 0x09, 0x0A, 0x04}) {
        const Bytes t300 = {0x49, 0x00, 0x00, 0x00, 0x00, 0x00, attachment, 0x02};
        CHECK_EQ(thrustmaster::switch_at_boot(t300, 0).value_or(0), std::uint16_t{0x0005});
    }
    // The TMX by its model (the kernel's commented-out row 0x0407) or by its
    // first step: tmdrv switches it with 1 at B67E, then 7 at B65D.
    CHECK_EQ(thrustmaster::first_step(0xB67E).value_or(0), std::uint16_t{0x0001});
    CHECK_EQ(thrustmaster::switch_at_boot({0x49, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x04}, 0).value_or(0),
             std::uint16_t{0x0007});
    CHECK_EQ(thrustmaster::switch_at_boot({}, 0xB67E).value_or(0), std::uint16_t{0x0007});
    // The TX: 1 at B664, then 4 at B65D (tmdrv). The TS-XW: 0x0A at B691.
    CHECK_EQ(thrustmaster::first_step(0xB664).value_or(0), std::uint16_t{0x0001});
    CHECK_EQ(thrustmaster::switch_at_boot({}, 0xB664).value_or(0), std::uint16_t{0x0004});
    CHECK_EQ(thrustmaster::first_step(0xB691).value_or(0), std::uint16_t{0x000A});
    // A wheel it doesn't know is left alone, as is the T500RS.
    CHECK(!thrustmaster::switch_at_boot({0x49, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00}, 0).has_value());
    CHECK(thrustmaster::is_boot(0xB65D));
    CHECK(!thrustmaster::is_boot(0xB677));
}

TEST(t150_open) {
    RecordingTransport wheel;
    T150Driver driver(wheel);
    CHECK(driver.open());
    CHECK_EQ(wheel.reports(), (std::vector<Bytes>{
                                  {0x42, 0x04},              // the input opened: nothing plays without it
                                  {0x40, 0x03, 0x00, 0x00},  // its centring's force to nothing, which frees it
                                  {0x40, 0x04, 0x00, 0x00},
                                  {0x43, 0x80},              // gain full: 0x80, not 0xFF
                                  // The constant force in slot 0: ff_first (key 0x1C), ff_update (key 0x0E), ff_commit
                                  // (type 0x4000, endless), then played once.
                                  {0x02, 0x1C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                                  {0x03, 0x0E, 0x00, 0x00},
                                  {0x01, 0x00, 0x00, 0x40, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x0E, 0x00, 0x1C, 0x00, 0x00, 0x00},
                                  {0x41, 0x00, 0x41, 0x01},
                                  // The damper in slot 1 (keys 0x38 and 0x2A), its ff_first ending 64 64
                                  // as Thrustmaster's driver sends it, type 0x4041.
                                  {0x05, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x64},
                                  {0x05, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x64},
                                  {0x01, 0x01, 0x41, 0x40, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x2A, 0x00, 0x38, 0x00, 0x00, 0x00},
                                  {0x41, 0x01, 0x41, 0x01},
                              }));
    CHECK_EQ(int(wheel.sent[0].id), 0);  // unnumbered
}

TEST(t150_force_each_way) {
    RecordingTransport wheel;
    T150Driver driver(wheel);
    driver.open();
    const std::size_t from = wheel.sent.size();
    // crossover-wheel's DirectInput conversion projects a force onto X by the
    // sine of its polar direction: a positive one-axis X force is 90 degrees,
    // so it reaches the wire positive, and in DirectInput a positive X force
    // turns the wheel to the left. Full scale is 64 (the vendor halves it).
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(-1.0));
    CHECK(driver.set_force(-1.0));
    CHECK(driver.set_force(0.5));
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0x03, 0x0E, 0x00, 0x40},
                                      {0x03, 0x0E, 0x00, 0xC0},
                                      {0x03, 0x0E, 0x00, 0x20},
                                  }));
}

TEST(t150_damper_stops_short_of_its_buzz) {
    RecordingTransport wheel;
    T150Driver driver(wheel);
    driver.open();
    const std::size_t from = wheel.sent.size();
    // Measured by crossover-wheel (RESEARCH.md A46): it buzzes above 80 of 100.
    CHECK(driver.set_damper(1.0));
    CHECK(driver.set_damper(0.9));  // also 80: nothing new
    CHECK(driver.set_damper(0.5));
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0x05, 0x2A, 0x00, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x64, 0x64},
                                      {0x05, 0x2A, 0x00, 0x32, 0x32, 0x00, 0x00, 0x00, 0x00, 0x64, 0x64},
                                  }));
}

TEST(t150_close_hands_the_input_back) {
    RecordingTransport wheel;
    T150Driver driver(wheel);
    driver.open();
    driver.set_force(0.25);
    const std::size_t from = wheel.sent.size();
    driver.close();
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0x03, 0x0E, 0x00, 0x00},
                                      {0x41, 0x00, 0x00, 0x01},
                                      {0x41, 0x01, 0x00, 0x01},
                                      {0x42, 0x05},
                                      {0x42, 0x05},
                                      {0x42, 0x00},
                                  }));
}

TEST(t300_open) {
    RecordingTransport wheel;
    T300Driver driver(wheel, t300::kBuffer, t300::Opening::t300rs, 64);
    CHECK(driver.open());
    const std::vector<Bytes> sent = wheel.reports();
    CHECK_EQ(sent.size(), std::size_t{8});
    for (const Sent& one : wheel.sent) {
        CHECK_EQ(int(one.id), 0x60);
        CHECK_EQ(one.report.size(), std::size_t{64});
    }
    CHECK_EQ(sent[0], padded({0x60, 0x01, 0x05}, 64));  // open: must come first
    CHECK_EQ(sent[1], padded({0x60, 0x02, 0xFF}, 64));  // gain full
    CHECK_EQ(sent[2], padded({0x60, 0x08, 0x04, 0x01}, 64));
    CHECK_EQ(sent[3], padded({0x60, 0x08, 0x03, 0x00}, 64));  // its centring at nothing
    // t300rs_upload_constant: effect 0 as ID 1, level, an envelope of
    // nothing, then the timing block: 4F, endless FFFF, no delay, FFFF.
    CHECK_EQ(sent[4], padded({0x60, 0x00, 0x01, 0x6A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x4F, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF},
                             64));
    CHECK_EQ(sent[5], padded({0x60, 0x00, 0x01, 0x89, 0x41, 0x01, 0x00}, 64));  // played once
    // t300rs_upload_condition for the damper (effect 1 as ID 2), its saturations 0x7FFC.
    CHECK_EQ(sent[6], padded({0x60, 0x00, 0x02, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x7F,
                              0xFC, 0x7F, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF, 0xFC, 0x7F, 0xFC, 0x7F,
                              0x07, 0x4F, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF},
                             64));
    CHECK_EQ(sent[7], padded({0x60, 0x00, 0x02, 0x89, 0x41, 0x01, 0x00}, 64));
}

TEST(t300_two_step_models_open_and_close_their_way) {
    // hid-tmt248.c (and the TX, TS-XW and TS-PC files): 01 04 then 01 05 to
    // open, 01 05 then 01 00 to close.
    RecordingTransport wheel;
    T300Driver driver(wheel, t300::kBuffer, t300::Opening::two_step, 64);
    CHECK(driver.open());
    CHECK_EQ(wheel.reports()[0], padded({0x60, 0x01, 0x04}, 64));
    CHECK_EQ(wheel.reports()[1], padded({0x60, 0x01, 0x05}, 64));
    const std::size_t from = wheel.sent.size();
    driver.close();
    const std::vector<Bytes> closing = wheel.reports(from);
    CHECK_EQ(closing.size(), std::size_t{4});
    CHECK_EQ(closing[0], padded({0x60, 0x00, 0x01, 0x89}, 64));
    CHECK_EQ(closing[1], padded({0x60, 0x00, 0x02, 0x89}, 64));
    CHECK_EQ(closing[2], padded({0x60, 0x01, 0x05}, 64));
    CHECK_EQ(closing[3], padded({0x60, 0x01, 0x00}, 64));
}

TEST(t300_force_each_way) {
    RecordingTransport wheel;
    T300Driver driver(wheel, t300::kPs4Buffer, t300::Opening::t300rs, 32);
    driver.open();
    const std::size_t from = wheel.sent.size();
    // hid-tmt300rs.c halves a Linux level projected through sin(direction)
    // (0x4000 for a wheel), so a positive force reaches the wire positive;
    // Akellacom's driver centres an ETS2 wheel with -steering, and ETS2's
    // steering is counterclockwise-positive, so positive turns it left.
    CHECK(driver.set_force(1.0));
    CHECK(driver.set_force(-1.0));
    const std::vector<Bytes> sent = wheel.reports(from);
    CHECK_EQ(sent.size(), std::size_t{2});
    // t300rs_update_constant: level, envelope, 00, 45, endless, no delay.
    CHECK_EQ(sent[0], padded({0x60, 0x00, 0x01, 0x6A, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x45, 0xFF, 0xFF, 0x00, 0x00},
                             32));
    CHECK_EQ(sent[1][4], std::uint8_t{0x00});
    CHECK_EQ(sent[1][5], std::uint8_t{0xC0});
}

TEST(t300_damper_update) {
    RecordingTransport wheel;
    T300Driver driver(wheel, t300::kBuffer, t300::Opening::t300rs, 64);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK(driver.set_damper(1.0));
    // t300rs_update_condition: coefficients, dead bands, saturations, 06, 45, endless, no delay.
    CHECK_EQ(wheel.reports(from)[0],
             padded({0x60, 0x00, 0x02, 0x4C, 0xFF, 0x7F, 0xFF, 0x7F, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x7F, 0xFC, 0x7F,
                     0x06, 0x45, 0xFF, 0xFF, 0x00, 0x00},
                    64));
}

TEST(t300_too_short_a_report_isnt_opened) {
    // macOS cutting a PS3-mode report to 62 bytes (Akellacom's notes).
    RecordingTransport wheel;
    T300Driver driver(wheel, t300::kBuffer, t300::Opening::t300rs, 63);
    CHECK(!driver.open());
    CHECK(wheel.sent.empty());
}

TEST(t300_plays_at_the_wheels_pace) {
    // hid-tmff2 updates every 8 ms (DEFAULT_TIMER_PERIOD); sending faster
    // fills the wheel's buffer, so the thread plays to it 125 times a second
    // and each tick's changes all go.
    RecordingTransport wheel;
    T300Driver driver(wheel, t300::kBuffer, t300::Opening::t300rs, 64);
    CHECK_EQ(driver.max_update_hz(), 125);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK(driver.play(0.5, 0.5, 1.0));
    CHECK_EQ(wheel.sent.size(), from + 2);  // the force and the damper; no friction on this wheel
    CHECK(driver.play(0.6, 0.5, 1.0));
    CHECK_EQ(wheel.sent.size(), from + 3);
}

TEST(t300_rotation) {
    // t300rs_set_range: 08 11, the degrees times 0x3C little-endian, from 40
    // to 1080 on the T300RS; the T248 (and TX) from 140 to 900. Back to the
    // most once let go.
    RecordingTransport wheel;
    T300Driver driver(wheel, t300::kBuffer, t300::Opening::t300rs, 64, 40, 1080);
    driver.open();
    std::size_t from = wheel.sent.size();
    CHECK_EQ(driver.set_rotation(900), 900);
    CHECK_EQ(driver.set_rotation(2000), 1080);
    CHECK_EQ(driver.set_rotation(10), 40);
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      padded({0x60, 0x08, 0x11, 0xF0, 0xD2}, 64),
                                      padded({0x60, 0x08, 0x11, 0x20, 0xFD}, 64),
                                      padded({0x60, 0x08, 0x11, 0x60, 0x09}, 64),
                                  }));
    from = wheel.sent.size();
    driver.close();
    CHECK_EQ(wheel.reports(from)[0], padded({0x60, 0x08, 0x11, 0x20, 0xFD}, 64));

    RecordingTransport t248;
    T300Driver narrower(t248, t300::kBuffer, t300::Opening::two_step, 64, 140, 900);
    narrower.open();
    CHECK_EQ(narrower.set_rotation(1080), 900);
    CHECK_EQ(narrower.set_rotation(90), 140);
}

TEST(t150_rotation) {
    // crossover-wheel's PROTOCOL.md: 40 11, degrees * 0xFFFF / 1080, with the
    // vendor's own tokens for 270, 360 and 900; t150_driver clamps to 270..1080.
    RecordingTransport wheel;
    T150Driver driver(wheel);
    driver.open();
    std::size_t from = wheel.sent.size();
    CHECK_EQ(driver.set_rotation(900), 900);
    CHECK_EQ(driver.set_rotation(270), 270);
    CHECK_EQ(driver.set_rotation(360), 360);
    CHECK_EQ(driver.set_rotation(540), 540);
    CHECK_EQ(driver.set_rotation(100), 270);
    CHECK_EQ(driver.set_rotation(1500), 1080);
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0x40, 0x11, 0x55, 0xD5},
                                      {0x40, 0x11, 0xFF, 0x3F},
                                      {0x40, 0x11, 0x55, 0x55},
                                      {0x40, 0x11, 0xFF, 0x7F},
                                      {0x40, 0x11, 0xFF, 0x3F},
                                      {0x40, 0x11, 0xFF, 0xFF},
                                  }));
    // Let go, back to t150_driver's 0xFFFF before the input is handed back.
    driver.set_rotation(540);
    from = wheel.sent.size();
    driver.close();
    CHECK_EQ(wheel.reports(from)[0], (Bytes{0x40, 0x11, 0xFF, 0xFF}));
    // The TMX turns 900 at most.
    RecordingTransport tmx;
    T150Driver smaller(tmx, 900);
    smaller.open();
    CHECK_EQ(smaller.set_rotation(1080), 900);
}

TEST(t150_friction_is_added_to_the_damper) {
    // crossover-wheel: the T150 has no friction effect, so it's played as
    // damping, still stopping short of the damper's buzz.
    RecordingTransport wheel;
    T150Driver driver(wheel);
    driver.open();
    const std::size_t from = wheel.sent.size();
    CHECK(driver.play(0.0, 0.2, 0.1));
    CHECK(driver.play(0.0, 0.6, 0.6));
    CHECK_EQ(wheel.reports(from), (std::vector<Bytes>{
                                      {0x05, 0x2A, 0x00, 0x1E, 0x1E, 0x00, 0x00, 0x00, 0x00, 0x64, 0x64},
                                      {0x05, 0x2A, 0x00, 0x50, 0x50, 0x00, 0x00, 0x00, 0x00, 0x64, 0x64},
                                  }));
}
