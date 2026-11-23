#include "../hat_reader.h"

#include "../drivers/hat_switch.h"
#include "hid_device.h"

namespace {

class MacHatReader : public HatReader {
public:
    MacHatReader(std::unique_ptr<HidManager> manager, std::unique_ptr<HidDevice> device, HatSwitch hat)
        : manager_(std::move(manager)), device_(std::move(device)), hat_(hat) {}

    std::optional<int> hat() override {
        const std::optional<Bytes> report = device_->latest_input(hat_.numbered ? hat_.report_id : 0);
        if (report) {
            const std::optional<int> read = read_hat(hat_, *report);
            if (read) {
                last_ = read;
            }
        }
        return last_;
    }

private:
    std::unique_ptr<HidManager> manager_;
    std::unique_ptr<HidDevice> device_;
    HatSwitch hat_;
    std::optional<int> last_;
};

}  // namespace

std::unique_ptr<HatReader> HatReader::open(std::uint16_t vendor_id, std::uint16_t product_id, std::uintptr_t) {
    auto manager = std::make_unique<HidManager>(true);
    for (const FoundDevice& found : manager->devices()) {
        if (found.facts.vendor != vendor_id || found.facts.product != product_id) {
            continue;
        }
        const std::optional<HatSwitch> hat = hat_switch(ReportDescriptor::parse(found.descriptor));
        if (!hat) {
            continue;  // another of its interfaces
        }
        auto device = std::make_unique<HidDevice>(found.service);
        if (device->open(true, found.max_input_report)) {
            return std::make_unique<MacHatReader>(std::move(manager), std::move(device), *hat);
        }
    }
    return nullptr;
}
