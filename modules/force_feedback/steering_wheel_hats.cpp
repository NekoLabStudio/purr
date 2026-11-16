#include "steering_wheel_hats.h"

#include "core/object/class_db.h"
#include "servers/display/display_server.h"

void SteeringWheelHats::_bind_methods() {
    ClassDB::bind_method(D_METHOD("watch", "vendor_id", "product_id"), &SteeringWheelHats::watch);
    ClassDB::bind_method(D_METHOD("get_hat"), &SteeringWheelHats::get_hat);
    ClassDB::bind_method(D_METHOD("reads_buttons"), &SteeringWheelHats::reads_buttons);
    ClassDB::bind_method(D_METHOD("get_buttons"), &SteeringWheelHats::get_buttons);
    ClassDB::bind_method(D_METHOD("close"), &SteeringWheelHats::close);
}

bool SteeringWheelHats::watch(int64_t vendor_id, int64_t product_id) {
    // The game's own window, which Windows asks for.
    DisplayServer* display = DisplayServer::get_singleton();
    const int64_t window = display ? display->window_get_native_handle(DisplayServerEnums::WINDOW_HANDLE) : 0;
    reader_ = HatReader::open(static_cast<std::uint16_t>(vendor_id), static_cast<std::uint16_t>(product_id),
                              static_cast<std::uintptr_t>(window));
    return reader_ != nullptr;
}

int64_t SteeringWheelHats::get_hat() {
    if (!reader_) {
        return -1;
    }
    const std::optional<int> hat = reader_->hat();
    return hat ? *hat : -1;
}

bool SteeringWheelHats::reads_buttons() {
    return reader_ && reader_->reads_buttons();
}

PackedInt32Array SteeringWheelHats::get_buttons() {
    PackedInt32Array held;
    if (reader_) {
        if (const std::optional<std::vector<int>> buttons = reader_->buttons()) {
            for (int button : *buttons) {
                held.push_back(button);
            }
        }
    }
    return held;
}

void SteeringWheelHats::close() {
    reader_.reset();
}
