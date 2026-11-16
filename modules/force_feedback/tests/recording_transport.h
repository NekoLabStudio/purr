#pragma once

// A Transport that records what a driver sends and plays back what a wheel
// would answer, in place of IOKit.

#include "drivers/wheel_driver.h"

#include <deque>
#include <functional>
#include <map>

struct Sent {
    ReportType type;
    std::uint8_t id;
    Bytes report;
};

class RecordingTransport : public Transport {
public:
    std::vector<Sent> sent;
    // Input reports the wheel sends after a report, by what it was sent.
    std::function<std::vector<Bytes>(const Bytes&)> answer;
    // Feature reports read back, by ID: each read takes the front one, the
    // last staying for any read after.
    std::map<std::uint8_t, std::deque<Bytes>> features;
    bool writes_fail = false;

    bool send(ReportType type, std::uint8_t id, const Bytes& report) override {
        sent.push_back({type, id, report});
        if (answer) {
            for (Bytes& reply : answer(report)) {
                inputs_.push_back(std::move(reply));
            }
        }
        return !writes_fail;
    }

    std::optional<Bytes> read_feature(std::uint8_t id, std::size_t length) override {
        auto found = features.find(id);
        if (found == features.end() || found->second.empty()) {
            return std::nullopt;
        }
        Bytes report = found->second.front();
        if (found->second.size() > 1) {
            found->second.pop_front();
        }
        report.resize(length, 0x00);
        return report;
    }

    std::optional<Bytes> next_input(std::chrono::milliseconds) override {
        if (inputs_.empty()) {
            return std::nullopt;
        }
        Bytes report = std::move(inputs_.front());
        inputs_.pop_front();
        return report;
    }

    void queue_input(Bytes report) { inputs_.push_back(std::move(report)); }

    // The reports sent from `from` on.
    std::vector<Bytes> reports(std::size_t from = 0) const {
        std::vector<Bytes> out;
        for (std::size_t i = from; i < sent.size(); ++i) {
            out.push_back(sent[i].report);
        }
        return out;
    }

private:
    std::deque<Bytes> inputs_;
};

// A report of `length` bytes starting with `start`, padded with zeros.
inline Bytes padded(Bytes start, std::size_t length) {
    start.resize(length, 0x00);
    return start;
}
