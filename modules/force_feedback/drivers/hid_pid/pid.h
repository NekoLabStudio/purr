#pragma once

// The USB Physical Interface Device standard (HID PID 1.0): any wheel whose
// report descriptor carries the PID reports. Moza, Cammus, VRS, Asetek,
// Simagic, Simucube, PXN, FFBeast, OpenFFBoard and more speak it.
//
// It follows the kernel's drivers/hid/usbhid/hid-pidff.c, the same as
// JacKeTUs/universal-pidff with its quirks now upstream: reports are found
// by the usage of their logical collection, array fields take a usage's
// place in the field plus one, values scale to each field's logical range
// as pidff_rescale and pidff_rescale_signed do, and optional fields
// (start delay, parameter block offset, the negative coefficient and
// saturation, dead band) are left out where a device lacks them. Device
// control as a bitmask, and a device control field whose logical minimum
// isn't 1 (VRS), are told from the descriptor as the kernel does.
//
// Friction (usage 0x43) is a condition effect like the damper, made where
// the device lists it. An endless duration is all ones in a field of up to
// 16 bits (PID_INFINITE, as hid-pidff's pidff_set_duration writes it); a
// wider field takes its own largest value instead, which reads as endless
// whether the device takes 0xFFFF or its largest as never stopping.
//
// The effects play at a polar direction of 90 degrees (the kernel's fixed
// wheel direction, 0x4000 of 0xFFFF) with direction enable set, where a
// positive magnitude turns the wheel to the left: Wine's DirectInput sends a
// one-axis wheel's force that way, OpenFFBoard's firmware turns it into a
// push towards negative X, and DirectInput's convention says the same.

#include "../report_descriptor.h"
#include "../wheel_driver.h"

#include <cstdint>
#include <string>

class PidDriver : public WheelDriver {
public:
    PidDriver(Transport& transport, ReportDescriptor descriptor);
    PidDriver(const PidDriver&) = delete;
    PidDriver& operator=(const PidDriver&) = delete;

    // What the descriptor lacks for force feedback; empty when it has it all.
    const std::string& missing() const { return missing_; }

    bool open() override;
    bool set_force(double force) override;
    bool set_damper(double damper) override;
    bool set_friction(double friction) override;
    int max_update_hz() const override { return 1000; }
    void stop() override;
    void close() override;

    // A Set Effect duration field's value for an endless effect.
    static std::int64_t endless(const HidField& field);

private:
    struct Report {
        bool found = false;
        ReportType type = ReportType::output;
        std::uint8_t id = 0;
        std::size_t length = 0;
    };
    // One usage's slot in a field.
    struct Value {
        const HidField* field = nullptr;
        int slot = -1;
    };

    void find();
    Value value(const Report& report, std::uint16_t usage, bool required);
    const HidField* special(const Report& report, std::uint16_t usage, bool enforce_min);
    int key(const HidField* field, std::uint16_t usage, bool required);

    Bytes blank(const Report& report) const;
    bool send(const Report& report, const Bytes& data);
    void put(Bytes& data, const Value& value, std::int64_t number) const;
    std::optional<std::int64_t> read(const Bytes& data, const Value& value) const;

    bool control(int key);
    std::optional<std::int64_t> create(int type_key);
    bool set_effect(std::int64_t block, int type_key);
    bool start(std::int64_t block);
    bool send_constant(std::int64_t magnitude);
    // A condition effect (the damper or friction) at a share of its strongest, 0 to 1.
    bool send_condition(std::int64_t block, double share);
    // Makes and starts a condition effect; its block, or 0 if it couldn't.
    std::int64_t start_condition(int create_key, int set_key, const char* name);
    // A share of a signed field's range, -1 to 1 (pidff_set_signed).
    static std::int64_t signed_value(double share, const HidField& field);

    Transport& transport_;
    // Its own copy: the fields below point into it.
    const ReportDescriptor descriptor_;
    std::string missing_;

    Report set_effect_, effect_operation_, device_gain_, pool_, block_load_, block_free_, device_control_,
        create_new_effect_, set_constant_, set_condition_;

    Value effect_block_, duration_, gain_, trigger_button_, trigger_repeat_, direction_enable_, start_delay_;
    Value constant_block_, magnitude_;
    Value condition_block_, block_offset_, centre_, positive_coefficient_, negative_coefficient_,
        positive_saturation_, negative_saturation_, dead_band_;
    Value operation_block_, loop_count_;
    Value load_block_;
    Value free_block_;
    Value device_gain_value_;
    Value simultaneous_, managed_pool_;

    const HidField* create_type_ = nullptr;
    const HidField* effect_type_ = nullptr;
    const HidField* direction_ = nullptr;
    const HidField* control_field_ = nullptr;
    const HidField* load_status_ = nullptr;
    const HidField* operation_ = nullptr;

    int create_constant_ = 0, create_damper_ = 0, set_constant_type_ = 0, set_damper_type_ = 0;
    int create_friction_ = 0, set_friction_type_ = 0;
    int load_success_ = 0, load_full_ = 0, load_error_ = 0;
    int operation_start_ = 0, operation_stop_ = 0;
    int control_enable_ = 0, control_stop_all_ = 0, control_reset_ = 0;

    // The effects' block indices.
    std::int64_t constant_ = 0;
    std::int64_t damper_ = 0;
    std::int64_t friction_ = 0;
    std::int64_t sent_force_ = 0;
    std::int64_t sent_damper_ = 0;
    std::int64_t sent_friction_ = 0;
};
