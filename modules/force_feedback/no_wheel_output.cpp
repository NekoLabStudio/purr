// Platforms with no wheel driver here (Linux, iOS, tvOS, Android): no wheel is ever found, so
// SteeringWheel and SteeringWheelHats exist everywhere and simply report that there's no wheel.

#include "hat_reader.h"
#include "wheel_output.h"

namespace {

class NoWheelOutput : public WheelOutput {
public:
    std::optional<FoundWheel> find(std::uint16_t, std::uint16_t) override { return std::nullopt; }
    bool open(std::uintptr_t, std::uint16_t, std::uint16_t) override { return false; }
    void close() override {}
    std::optional<FoundWheel> wheel() const override { return std::nullopt; }
    bool set_force(double) override { return false; }
    bool set_damper(double) override { return false; }
    bool set_friction(double) override { return false; }
    void stop() override {}
};

}  // namespace

std::unique_ptr<WheelOutput> WheelOutput::create() {
    return std::make_unique<NoWheelOutput>();
}

void WheelOutput::watch() {}

std::unique_ptr<HatReader> HatReader::open(std::uint16_t, std::uint16_t, std::uintptr_t) {
    return nullptr;
}
