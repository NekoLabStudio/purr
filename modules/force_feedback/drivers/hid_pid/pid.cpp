#include "pid.h"

#include "../../wheel_log.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr std::uint32_t kPage = 0x0F << 16;

// The reports, by the usage of their logical collection (pidff_reports).
constexpr std::uint16_t kSetEffect = 0x21;
constexpr std::uint16_t kEffectOperation = 0x77;
constexpr std::uint16_t kDeviceGain = 0x7D;
constexpr std::uint16_t kPool = 0x7F;
constexpr std::uint16_t kBlockLoad = 0x89;
constexpr std::uint16_t kBlockFree = 0x90;
constexpr std::uint16_t kDeviceControl = 0x96;
constexpr std::uint16_t kCreateNewEffect = 0xAB;
constexpr std::uint16_t kSetConstant = 0x73;
constexpr std::uint16_t kSetCondition = 0x5F;

// Their fields.
constexpr std::uint16_t kBlockIndex = 0x22;
constexpr std::uint16_t kDuration = 0x50;
constexpr std::uint16_t kGain = 0x52;
constexpr std::uint16_t kTriggerButton = 0x53;
constexpr std::uint16_t kTriggerRepeat = 0x54;
constexpr std::uint16_t kDirectionEnable = 0x56;
constexpr std::uint16_t kStartDelay = 0xA7;
constexpr std::uint16_t kMagnitude = 0x70;
constexpr std::uint16_t kBlockOffset = 0x23;
constexpr std::uint16_t kCentre = 0x60;
constexpr std::uint16_t kPositiveCoefficient = 0x61;
constexpr std::uint16_t kNegativeCoefficient = 0x62;
constexpr std::uint16_t kPositiveSaturation = 0x63;
constexpr std::uint16_t kNegativeSaturation = 0x64;
constexpr std::uint16_t kDeadBand = 0x65;
constexpr std::uint16_t kLoopCount = 0x7C;
constexpr std::uint16_t kDeviceGainValue = 0x7E;
constexpr std::uint16_t kSimultaneousMax = 0x83;
constexpr std::uint16_t kDeviceManagedPool = 0xA9;

// Array fields, by their collection, and the usages they choose between.
constexpr std::uint16_t kEffectType = 0x25;
constexpr std::uint16_t kDirection = 0x57;
constexpr std::uint16_t kBlockLoadStatus = 0x8B;
constexpr std::uint16_t kOperation = 0x78;
constexpr std::uint16_t kConstant = 0x26;
constexpr std::uint16_t kDamper = 0x41;
constexpr std::uint16_t kFriction = 0x43;
constexpr std::uint16_t kLoadSuccess = 0x8C;
constexpr std::uint16_t kLoadFull = 0x8D;
constexpr std::uint16_t kLoadError = 0x8E;
constexpr std::uint16_t kStart = 0x79;
constexpr std::uint16_t kStop = 0x7B;
constexpr std::uint16_t kEnableActuators = 0x97;
constexpr std::uint16_t kStopAll = 0x99;
constexpr std::uint16_t kReset = 0x9A;

constexpr int kBlockLoadTries = 60;
constexpr int kPoolTries = 20;
// The kernel's fixed wheel direction, 90 degrees of a turn as 0x4000 of 0xFFFF.
constexpr std::int64_t kWheelDirection = 0x4000;

// pidff_rescale: a share `i` of `max` across the field's logical range.
std::int64_t rescale(std::int64_t i, std::int64_t max, const HidField& field) {
    return (static_cast<std::int64_t>(field.logical_max) - field.logical_min) * i / max + field.logical_min;
}

// pidff_set_time for a time of nothing, kept inside the field's range.
std::int64_t no_time(const HidField& field) {
    return std::clamp<std::int64_t>(0, field.logical_min, field.logical_max);
}

}  // namespace

PidDriver::PidDriver(Transport& transport, ReportDescriptor descriptor)
    : transport_(transport), descriptor_(std::move(descriptor)) {
    find();
}

std::int64_t PidDriver::endless(const HidField& field) {
    if (field.size <= 16) {
        return static_cast<std::int64_t>((std::uint64_t{1} << field.size) - 1);
    }
    return field.logical_max > 0xFFFF ? field.logical_max : 0xFFFF;
}

std::int64_t PidDriver::signed_value(double share, const HidField& field) {
    share = std::clamp(share, -1.0, 1.0);
    if (field.logical_min < 0) {
        return share >= 0.0 ? std::llround(share * field.logical_max) : std::llround(-share * field.logical_min);
    }
    // An unsigned field takes the size alone, as the kernel sends it.
    return field.logical_min + std::llround(std::abs(share) * (static_cast<double>(field.logical_max) - field.logical_min));
}

void PidDriver::find() {
    struct Wanted {
        std::uint16_t usage;
        Report* report;
        const char* name;
    };
    const Wanted wanted[] = {
        {kSetEffect, &set_effect_, "Set Effect"},
        {kEffectOperation, &effect_operation_, "Effect Operation"},
        {kDeviceGain, &device_gain_, "Device Gain"},
        {kPool, &pool_, "Pool"},
        {kBlockLoad, &block_load_, "Block Load"},
        {kBlockFree, &block_free_, "Block Free"},
        {kDeviceControl, &device_control_, "Device Control"},
        {kCreateNewEffect, &create_new_effect_, "Create New Effect"},
        {kSetConstant, &set_constant_, "Set Constant"},
        {kSetCondition, &set_condition_, "Set Condition"},
    };
    // pidff_find_reports: output reports, then feature reports, each named by
    // the logical collection round its first field, or the one round that.
    for (const ReportType type : {ReportType::output, ReportType::feature}) {
        for (const std::uint8_t id : descriptor_.report_ids(type)) {
            const HidField* first = nullptr;
            for (const HidField& field : descriptor_.fields()) {
                if (field.type == type && field.report_id == id && !field.usages.empty()) {
                    first = &field;
                    break;
                }
            }
            if (!first) {
                continue;
            }
            for (const Wanted& want : wanted) {
                const bool direct = first->logical == (kPage | want.usage);
                const bool outer = first->outer_logical == (kPage | want.usage) && !want.report->found;
                if (direct || outer) {
                    *want.report = Report{true, type, id, descriptor_.length(type, id)};
                    break;
                }
            }
        }
    }
    for (const Wanted& want : wanted) {
        if (!want.report->found) {
            missing_ += (missing_.empty() ? "no " : ", ") + std::string(want.name);
        }
    }
    if (!missing_.empty()) {
        missing_ += " report";
        return;
    }

    effect_block_ = value(set_effect_, kBlockIndex, true);
    duration_ = value(set_effect_, kDuration, true);
    gain_ = value(set_effect_, kGain, true);
    trigger_button_ = value(set_effect_, kTriggerButton, true);
    trigger_repeat_ = value(set_effect_, kTriggerRepeat, true);
    direction_enable_ = value(set_effect_, kDirectionEnable, true);
    start_delay_ = value(set_effect_, kStartDelay, false);
    constant_block_ = value(set_constant_, kBlockIndex, true);
    magnitude_ = value(set_constant_, kMagnitude, true);
    condition_block_ = value(set_condition_, kBlockIndex, true);
    block_offset_ = value(set_condition_, kBlockOffset, false);
    centre_ = value(set_condition_, kCentre, true);
    positive_coefficient_ = value(set_condition_, kPositiveCoefficient, true);
    negative_coefficient_ = value(set_condition_, kNegativeCoefficient, false);
    positive_saturation_ = value(set_condition_, kPositiveSaturation, true);
    negative_saturation_ = value(set_condition_, kNegativeSaturation, false);
    dead_band_ = value(set_condition_, kDeadBand, false);
    operation_block_ = value(effect_operation_, kBlockIndex, true);
    loop_count_ = value(effect_operation_, kLoopCount, true);
    load_block_ = value(block_load_, kBlockIndex, true);
    free_block_ = value(block_free_, kBlockIndex, true);
    device_gain_value_ = value(device_gain_, kDeviceGainValue, true);
    simultaneous_ = value(pool_, kSimultaneousMax, false);
    managed_pool_ = value(pool_, kDeviceManagedPool, false);

    create_type_ = special(create_new_effect_, kEffectType, true);
    effect_type_ = special(set_effect_, kEffectType, true);
    direction_ = special(set_effect_, kDirection, false);
    // A device control field whose logical minimum isn't 1 is taken all the
    // same (the kernel's PERMISSIVE_CONTROL quirk).
    control_field_ = special(device_control_, kDeviceControl, true);
    if (!control_field_) {
        control_field_ = special(device_control_, kDeviceControl, false);
    }
    load_status_ = special(block_load_, kBlockLoadStatus, true);
    operation_ = special(effect_operation_, kOperation, true);
    if (!missing_.empty()) {
        return;
    }
    if (!create_type_ || !effect_type_ || !direction_ || !control_field_ || !load_status_ || !operation_) {
        missing_ = "no effect type, direction, device control, block load status or effect operation field";
        return;
    }

    create_constant_ = key(create_type_, kConstant, true);
    create_damper_ = key(create_type_, kDamper, true);
    set_constant_type_ = key(effect_type_, kConstant, true);
    set_damper_type_ = key(effect_type_, kDamper, true);
    create_friction_ = key(create_type_, kFriction, false);
    set_friction_type_ = key(effect_type_, kFriction, false);
    load_success_ = key(load_status_, kLoadSuccess, true);
    load_full_ = key(load_status_, kLoadFull, true);
    load_error_ = key(load_status_, kLoadError, true);
    operation_start_ = key(operation_, kStart, true);
    operation_stop_ = key(operation_, kStop, true);
    control_enable_ = key(control_field_, kEnableActuators, false);
    control_stop_all_ = key(control_field_, kStopAll, false);
    control_reset_ = key(control_field_, kReset, false);
}

PidDriver::Value PidDriver::value(const Report& report, std::uint16_t usage, bool required) {
    for (const HidField& field : descriptor_.fields()) {
        if (field.type == report.type && field.report_id == report.id) {
            const int slot = field.slot_of(kPage | usage);
            if (slot >= 0) {
                return {&field, slot};
            }
        }
    }
    if (required && missing_.empty()) {
        char text[64];
        std::snprintf(text, sizeof(text), "no field for usage 0x%02X", usage);
        missing_ = text;
    }
    return {};
}

const HidField* PidDriver::special(const Report& report, std::uint16_t usage, bool enforce_min) {
    for (const HidField& field : descriptor_.fields()) {
        if (field.type == report.type && field.report_id == report.id && field.logical == (kPage | usage) &&
            field.count > 0 && (!enforce_min || field.logical_min == 1)) {
            return &field;
        }
    }
    return nullptr;
}

int PidDriver::key(const HidField* field, std::uint16_t usage, bool required) {
    const int found = field->key_of(kPage | usage);
    if (found == 0 && required && missing_.empty()) {
        char text[64];
        std::snprintf(text, sizeof(text), "no usage 0x%02X to choose", usage);
        missing_ = text;
    }
    return found;
}

Bytes PidDriver::blank(const Report& report) const {
    Bytes data(report.length, 0x00);
    if (report.id != 0 && !data.empty()) {
        data[0] = report.id;
    }
    return data;
}

bool PidDriver::send(const Report& report, const Bytes& data) {
    return transport_.send(report.type, report.id, data);
}

void PidDriver::put(Bytes& data, const Value& value, std::int64_t number) const {
    if (value.field) {
        put_bits(data, value.field->bit_offset + value.slot * value.field->size, value.field->size, number);
    }
}

std::optional<std::int64_t> PidDriver::read(const Bytes& data, const Value& value) const {
    if (!value.field) {
        return std::nullopt;
    }
    return get_bits(data, value.field->bit_offset + value.slot * value.field->size, value.field->size,
                    value.field->logical_min < 0);
}

// pidff_set_device_control: one usage chosen in an array, or one bit set in a bitmask.
bool PidDriver::control(int key) {
    if (key == 0) {
        return true;
    }
    Bytes data = blank(device_control_);
    if (control_field_->variable) {
        put_bits(data, control_field_->bit_offset + (key - 1) * control_field_->size, control_field_->size, 1);
    } else {
        put_bits(data, control_field_->bit_offset, control_field_->size, key);
    }
    return send(device_control_, data);
}

// pidff_request_effect_upload: Create New Effect, then Block Load read until
// it says how it went. Its effect block index is the effect's handle.
std::optional<std::int64_t> PidDriver::create(int type_key) {
    Bytes data = blank(create_new_effect_);
    put_bits(data, create_type_->bit_offset, create_type_->size, type_key);
    if (!send(create_new_effect_, data)) {
        wheel_log("PID: Create New Effect failed");
        return std::nullopt;
    }
    for (int i = 0; i < kBlockLoadTries; ++i) {
        const std::optional<Bytes> load = transport_.read_feature(block_load_.id, block_load_.length);
        if (!load) {
            continue;
        }
        const std::int64_t status = get_bits(*load, load_status_->bit_offset, load_status_->size, false);
        if (status == load_success_) {
            return read(*load, load_block_);
        }
        if (status == load_full_) {
            wheel_log("PID: the wheel has no room for another effect");
            return std::nullopt;
        }
        if (status == load_error_) {
            wheel_log("PID: the wheel failed to create an effect");
            return std::nullopt;
        }
    }
    wheel_log("PID: Block Load never said the effect was made");
    return std::nullopt;
}

// pidff_set_effect_report: endless, full gain, no trigger, direction 90 degrees.
bool PidDriver::set_effect(std::int64_t block, int type_key) {
    Bytes data = blank(set_effect_);
    put(data, effect_block_, block);
    put_bits(data, effect_type_->bit_offset, effect_type_->size, type_key);
    put(data, duration_, endless(*duration_.field));
    put(data, trigger_button_, 0);
    put(data, trigger_repeat_, no_time(*trigger_repeat_.field));
    put(data, gain_, gain_.field->logical_max);
    put(data, direction_enable_, 1);
    put_bits(data, direction_->bit_offset, direction_->size, rescale(kWheelDirection, 0xFFFF, *direction_));
    if (start_delay_.field) {
        put(data, start_delay_, no_time(*start_delay_.field));
    }
    return send(set_effect_, data);
}

bool PidDriver::start(std::int64_t block) {
    Bytes data = blank(effect_operation_);
    put(data, operation_block_, block);
    put_bits(data, operation_->bit_offset, operation_->size, operation_start_);
    put(data, loop_count_, std::clamp<std::int64_t>(1, loop_count_.field->logical_min, loop_count_.field->logical_max));
    return send(effect_operation_, data);
}

bool PidDriver::send_constant(std::int64_t magnitude) {
    Bytes data = blank(set_constant_);
    put(data, constant_block_, constant_);
    put(data, magnitude_, magnitude);
    return send(set_constant_, data);
}

// pidff_set_condition_report: one parameter block per axis (one if the
// device has no block offset); the wheel's axis first, the other at nothing.
bool PidDriver::send_condition(std::int64_t block, double share) {
    const int axes = block_offset_.field ? 2 : 1;
    for (int axis = 0; axis < axes; ++axis) {
        const double k = axis == 0 ? share : 0.0;
        Bytes data = blank(set_condition_);
        put(data, condition_block_, block);
        put(data, block_offset_, axis);
        put(data, centre_, signed_value(0.0, *centre_.field));
        put(data, positive_coefficient_, signed_value(k, *positive_coefficient_.field));
        put(data, positive_saturation_, rescale(axis == 0 ? 0xFFFF : 0, 0xFFFF, *positive_saturation_.field));
        if (negative_coefficient_.field) {
            put(data, negative_coefficient_, signed_value(k, *negative_coefficient_.field));
        }
        if (negative_saturation_.field) {
            put(data, negative_saturation_, rescale(axis == 0 ? 0xFFFF : 0, 0xFFFF, *negative_saturation_.field));
        }
        if (dead_band_.field) {
            put(data, dead_band_, rescale(0, 0xFFFF, *dead_band_.field));
        }
        if (!send(set_condition_, data)) {
            return false;
        }
    }
    return true;
}

bool PidDriver::open() {
    sent_force_ = sent_damper_ = sent_friction_ = 0;
    friction_ = 0;
    if (!missing_.empty()) {
        wheel_log("PID: " + missing_ + "; not opened");
        return false;
    }
    // pidff_fetch_pool: read again while it claims fewer than two effects at once.
    for (int i = 0; i < kPoolTries; ++i) {
        const std::optional<Bytes> pool = transport_.read_feature(pool_.id, pool_.length);
        if (!pool) {
            wheel_log("PID: the Pool report couldn't be read");
            break;
        }
        if (managed_pool_.field && read(*pool, managed_pool_).value_or(1) == 0) {
            wheel_log("PID: the wheel doesn't manage its own effect memory; not opened");
            return false;
        }
        if (!simultaneous_.field || read(*pool, simultaneous_).value_or(2) >= 2) {
            break;
        }
    }
    // pidff_reset, twice as the kernel does it, then the gain full.
    Bytes gain = blank(device_gain_);
    put(gain, device_gain_value_, rescale(0xFFFF, 0xFFFF, *device_gain_value_.field));
    if (!control(control_reset_) || !control(control_reset_) || !control(control_stop_all_) ||
        !control(control_enable_) || !send(device_gain_, gain)) {
        wheel_log("PID: the wheel refused its start reports");
        return false;
    }
    const std::optional<std::int64_t> constant = create(create_constant_);
    if (!constant) {
        return false;
    }
    constant_ = *constant;
    sent_force_ = signed_value(0.0, *magnitude_.field);
    if (!set_effect(constant_, set_constant_type_) || !send_constant(sent_force_) || !start(constant_)) {
        wheel_log("PID: the constant force couldn't be started");
        return false;
    }
    sent_damper_ = sent_friction_ = signed_value(0.0, *positive_coefficient_.field);
    damper_ = start_condition(create_damper_, set_damper_type_, "the damper");
    if (damper_ == 0) {
        return false;
    }
    // Friction where the device has it.
    friction_ = create_friction_ != 0 && set_friction_type_ != 0
                    ? start_condition(create_friction_, set_friction_type_, "friction")
                    : 0;
    return true;
}

std::int64_t PidDriver::start_condition(int create_key, int set_key, const char* name) {
    const std::optional<std::int64_t> block = create(create_key);
    if (!block) {
        return 0;
    }
    if (!set_effect(*block, set_key) || !send_condition(*block, 0.0) || !start(*block)) {
        wheel_log(std::string("PID: ") + name + " couldn't be started");
        return 0;
    }
    return *block;
}

bool PidDriver::set_force(double force) {
    const std::int64_t magnitude = signed_value(force, *magnitude_.field);
    if (magnitude == sent_force_) {
        return true;
    }
    sent_force_ = magnitude;
    return send_constant(magnitude);
}

bool PidDriver::set_damper(double damper) {
    damper = std::clamp(damper, 0.0, 1.0);
    const std::int64_t coefficient = signed_value(damper, *positive_coefficient_.field);
    if (coefficient == sent_damper_) {
        return true;
    }
    sent_damper_ = coefficient;
    return send_condition(damper_, damper);
}

bool PidDriver::set_friction(double friction) {
    if (friction_ == 0) {
        return true;
    }
    friction = std::clamp(friction, 0.0, 1.0);
    const std::int64_t coefficient = signed_value(friction, *positive_coefficient_.field);
    if (coefficient == sent_friction_) {
        return true;
    }
    sent_friction_ = coefficient;
    return send_condition(friction_, friction);
}

void PidDriver::stop() {
    set_force(0.0);
    set_damper(0.0);
    set_friction(0.0);
}

// Each effect stopped and freed (pidff_erase_effect), then everything stopped.
void PidDriver::close() {
    if (!missing_.empty()) {
        return;
    }
    stop();
    for (const std::int64_t block : {constant_, damper_, friction_}) {
        if (block == 0) {
            continue;
        }
        Bytes operation = blank(effect_operation_);
        put(operation, operation_block_, block);
        put_bits(operation, operation_->bit_offset, operation_->size, operation_stop_);
        send(effect_operation_, operation);
        Bytes free = blank(block_free_);
        put(free, free_block_, block);
        send(block_free_, free);
    }
    control(control_stop_all_);
    constant_ = damper_ = friction_ = 0;
}
