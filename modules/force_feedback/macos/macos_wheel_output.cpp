// Force feedback on macOS: each wheel family's own protocol written straight
// to the wheel over IOKit HID (src/drivers), chosen by the registry
// (wheel_registry.h). macOS's ForceFeedback framework drives none of them.
// A wheel found in the mode it starts in is switched to its own
// (mode_switches.h) and taken on the game's next try, once it has come back.

#include "../drivers/steering_axis.h"
#include "../wheel_log.h"
#include "../wheel_output.h"
#include "hid_device.h"
#include "mode_switches.h"
#include "wheel_registry.h"

#include <chrono>
#include <map>
#include <string>

namespace {

using Clock = std::chrono::steady_clock;
using wheel_registry::Choice;

// A device whose driver wouldn't start isn't tried again sooner than this:
// starting one can wait on the wheel, and the game tries every few seconds.
constexpr auto kTryAgain = std::chrono::seconds(30);

FoundWheel found_wheel(const Choice& choice, const wheel_registry::Device& facts) {
    return FoundWheel{choice.name, choice.verified, facts.vendor, facts.product,
                      wheel_registry::family_id(choice.family)};
}

class MacWheelOutput : public WheelOutput {
public:
    ~MacWheelOutput() override { close(); }

    std::optional<FoundWheel> find(std::uint16_t vendor, std::uint16_t product) override {
        std::vector<FoundDevice> found = devices().devices();
        for (const std::size_t i : order(found, vendor, product)) {
            const Choice choice = wheel_registry::choose(found[i].facts);
            if (choice.kind != Choice::Kind::none) {
                return found_wheel(choice, found[i].facts);
            }
        }
        return std::nullopt;
    }

    bool open(std::uintptr_t, std::uint16_t vendor, std::uint16_t product) override {
        close();
        std::vector<FoundDevice> found = devices().devices();
        for (const std::size_t i : order(found, vendor, product)) {
            FoundDevice& device = found[i];
            const Choice choice = wheel_registry::choose(device.facts);
            switch (choice.kind) {
            case Choice::Kind::none:
                continue;
            case Choice::Kind::switch_logitech:
            case Choice::Kind::switch_thrustmaster:
                ModeSwitches::shared().take(device);
                return false;
            case Choice::Kind::drive: {
                const auto failed = failed_.find(device.service);
                if (failed != failed_.end() && Clock::now() - failed->second < kTryAgain) {
                    continue;
                }
                if (drive(device, choice, found)) {
                    failed_.erase(device.service);
                    return true;
                }
                failed_[device.service] = Clock::now();
                continue;
            }
            }
        }
        return false;
    }

    void close() override {
        if (driver_) {
            driver_->close();
        }
        driver_.reset();
        siblings_.clear();
        device_.reset();
        wheel_.reset();
        steering_.reset();
    }

    std::optional<FoundWheel> wheel() const override { return wheel_; }

    bool set_force(double force) override { return driver_ && driver_->set_force(force); }
    bool set_damper(double damper) override { return driver_ && driver_->set_damper(damper); }
    bool set_friction(double friction) override { return driver_ && driver_->set_friction(friction); }
    bool play(double force, double damper, double friction) override {
        return driver_ && driver_->play(force, damper, friction);
    }
    int set_rotation(int degrees) override { return driver_ ? driver_->set_rotation(degrees) : 0; }
    int rotation() const override { return driver_ ? driver_->rotation() : 0; }
    int update_hz() const override { return driver_ ? driver_->max_update_hz() : WheelOutput::update_hz(); }
    bool set_leds(std::uint32_t leds) override { return driver_ && driver_->set_leds(leds); }
    std::optional<double> position() override {
        if (!device_ || !steering_) {
            return std::nullopt;
        }
        const std::optional<Bytes> report = device_->latest_input(steering_->numbered ? steering_->report_id : 0);
        return report ? read_steering(*steering_, *report) : std::nullopt;
    }
    OutputCounts counts() const override {
        OutputCounts counts;
        if (device_) {
            counts.reports = device_->sent();
        }
        if (driver_) {
            counts.errors = static_cast<std::uint64_t>(driver_->errors());
        }
        return counts;
    }
    void stop() override {
        if (driver_) {
            driver_->stop();
        }
    }
    void prepare() override { ModeSwitches::shared().prepare(devices()); }

private:
    HidManager& devices() {
        if (!manager_) {
            manager_ = std::make_unique<HidManager>();
        }
        return *manager_;
    }

    static std::vector<std::size_t> order(const std::vector<FoundDevice>& found, std::uint16_t vendor,
                                          std::uint16_t product) {
        std::vector<wheel_registry::Device> facts;
        for (const FoundDevice& device : found) {
            facts.push_back(device.facts);
        }
        return wheel_registry::preferred_order(facts, vendor, product);
    }

    // A wheel's other interfaces its driver needs, opened to listen.
    wheel_registry::Siblings open_siblings(const FoundDevice& wheel, const std::vector<FoundDevice>& found) {
        wheel_registry::Siblings siblings;
        for (const FoundDevice& other : found) {
            if (other.service == wheel.service || other.facts.vendor != wheel.facts.vendor ||
                other.facts.product != wheel.facts.product || other.facts.location != wheel.facts.location) {
                continue;
            }
            const wheel_registry::Sibling role = wheel_registry::sibling(other.facts);
            Transport** slot = role == wheel_registry::Sibling::hidpp   ? &siblings.hidpp
                               : role == wheel_registry::Sibling::input ? &siblings.input
                                                                        : nullptr;
            if (!slot || *slot) {
                continue;
            }
            auto device = std::make_unique<HidDevice>(other.service);
            if (device->open(true, other.max_input_report)) {
                *slot = device.get();
                siblings_.push_back(std::move(device));
            }
        }
        return siblings;
    }

    bool drive(FoundDevice& found, const Choice& choice, const std::vector<FoundDevice>& all) {
        const wheel_registry::Device& facts = found.facts;
        const std::string id = usb_id(facts.vendor, facts.product);
        const char* family = wheel_registry::family_name(choice.family);
        auto device = std::make_unique<HidDevice>(found.service);
        // Listening for its reports too: HID++ talks back, and every wheel's own report
        // carries the steering, read for the damping at the thread's rate.
        if (!device->open(true, found.max_input_report)) {
            wheel_log(choice.name + " (" + id + ") couldn't be opened");
            return false;
        }
        // A PID wheel no one has confirmed: its descriptor, once, so a tester's
        // log can become a test fixture.
        if (choice.family == wheel_registry::Family::pid && !choice.verified) {
            wheel_log(choice.name + " (" + id + ") report descriptor: " + hex_bytes(found.descriptor));
        }
        const wheel_registry::Siblings siblings =
            wheel_registry::needs_siblings(choice.family) ? open_siblings(found, all) : wheel_registry::Siblings{};
        std::unique_ptr<WheelDriver> driver = wheel_registry::make_driver(choice, facts, *device, siblings);
        if (!driver || !driver->open()) {
            wheel_log(choice.name + " (" + id + ", " + family + ") didn't start");
            siblings_.clear();
            return false;
        }
        wheel_log(choice.name + ": " + family + " driver, " + id + ", release " + hex4(facts.version) +
                  ", reports of " + std::to_string(found.max_input_report) + " in, " +
                  std::to_string(facts.max_output_report) + " out, " + std::to_string(found.max_feature_report) +
                  " feature bytes" + (choice.verified ? "" : " (new)"));
        device_ = std::move(device);
        driver_ = std::move(driver);
        wheel_ = found_wheel(choice, facts);
        steering_ = steering_axis(ReportDescriptor::parse(found.descriptor));
        if (!steering_) {
            wheel_log(choice.name + ": its reports don't show the steering, so the game damps it");
        }
        return true;
    }

    std::unique_ptr<HidManager> manager_;
    std::unique_ptr<HidDevice> device_;
    std::vector<std::unique_ptr<HidDevice>> siblings_;
    std::unique_ptr<WheelDriver> driver_;
    std::optional<FoundWheel> wheel_;
    std::optional<SteeringAxis> steering_;
    std::map<io_service_t, Clock::time_point> failed_;
};

}  // namespace

std::unique_ptr<WheelOutput> WheelOutput::create() {
    return std::make_unique<MacWheelOutput>();
}

void WheelOutput::watch() {
    ModeSwitches::shared().watch();
}
