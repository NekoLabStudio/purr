#include "report_descriptor.h"

#include <algorithm>
#include <map>
#include <utility>

namespace {

enum MainTag { kInput = 0x8, kOutput = 0x9, kCollection = 0xA, kFeature = 0xB, kEndCollection = 0xC };
enum GlobalTag {
    kUsagePage = 0x0,
    kLogicalMin = 0x1,
    kLogicalMax = 0x2,
    kUnitExponent = 0x5,
    kReportSize = 0x7,
    kReportId = 0x8,
    kReportCount = 0x9,
    kPush = 0xA,
    kPop = 0xB,
};
enum LocalTag { kUsage = 0x0, kUsageMin = 0x1, kUsageMax = 0x2 };
constexpr std::uint32_t kLogicalCollection = 0x02;
// A usage range longer than this is taken as malformed.
constexpr std::uint32_t kLongestRange = 0x10000;

struct Globals {
    std::uint32_t usage_page = 0;
    std::int64_t logical_min = 0;
    std::uint32_t logical_max_raw = 0;
    int logical_max_size = 0;
    std::int32_t unit_exponent = 0;
    std::uint32_t report_size = 0;
    std::uint32_t report_count = 0;
    std::uint8_t report_id = 0;
};

// A usage as written: 4 bytes carry their own page, shorter ones take the
// page current at the main item.
struct LocalUsage {
    std::uint32_t value;
    bool extended;
};

struct Collection {
    std::uint32_t type;
    std::uint32_t usage;
};

std::int64_t as_signed(std::uint32_t raw, int size) {
    switch (size) {
    case 1:
        return static_cast<std::int8_t>(raw);
    case 2:
        return static_cast<std::int16_t>(raw);
    case 4:
        return static_cast<std::int32_t>(raw);
    default:
        return 0;
    }
}

std::uint32_t full_usage(const LocalUsage& usage, std::uint32_t page) {
    return usage.extended ? usage.value : (page << 16) | (usage.value & 0xFFFF);
}

}  // namespace

int HidField::slot_of(std::uint32_t usage) const {
    if (usages.size() != count) {
        return -1;
    }
    const auto found = std::find(usages.begin(), usages.end(), usage);
    return found == usages.end() ? -1 : static_cast<int>(found - usages.begin());
}

int HidField::key_of(std::uint32_t usage) const {
    const auto found = std::find(usages.begin(), usages.end(), usage);
    return found == usages.end() ? 0 : static_cast<int>(found - usages.begin()) + 1;
}

ReportDescriptor ReportDescriptor::parse(const Bytes& data) {
    ReportDescriptor parsed;
    Globals globals;
    std::vector<Globals> pushed;
    std::vector<LocalUsage> usages;
    std::optional<LocalUsage> usage_min;
    std::vector<Collection> collections;
    std::map<std::pair<ReportType, std::uint8_t>, std::uint32_t> bits;

    std::size_t at = 0;
    while (at < data.size()) {
        const std::uint8_t prefix = data[at];
        if (prefix == 0xFE) {  // a long item: nothing the spec defines uses one
            if (at + 1 >= data.size()) {
                return {};
            }
            at += 3 + data[at + 1];
            continue;
        }
        const int size = (prefix & 0x3) == 3 ? 4 : (prefix & 0x3);
        const int type = (prefix >> 2) & 0x3;
        const int tag = prefix >> 4;
        if (at + 1 + size > data.size()) {
            return {};
        }
        std::uint32_t raw = 0;
        for (int i = 0; i < size; ++i) {
            raw |= static_cast<std::uint32_t>(data[at + 1 + i]) << (8 * i);
        }
        at += 1 + size;

        if (type == 1) {  // global
            switch (tag) {
            case kUsagePage:
                globals.usage_page = raw & 0xFFFF;
                break;
            case kLogicalMin:
                globals.logical_min = as_signed(raw, size);
                break;
            case kLogicalMax:
                globals.logical_max_raw = raw;
                globals.logical_max_size = size;
                break;
            case kUnitExponent:
                // Four-bit two's complement, as the spec has it; the kernel
                // also takes a wider signed value as it is.
                globals.unit_exponent =
                    (raw & ~0xFu) == 0 ? (raw & 0x8 ? static_cast<std::int32_t>(raw) - 16 : static_cast<std::int32_t>(raw))
                                       : static_cast<std::int32_t>(as_signed(raw, size));
                break;
            case kReportSize:
                globals.report_size = raw;
                break;
            case kReportId:
                globals.report_id = static_cast<std::uint8_t>(raw);
                parsed.numbered_ = true;
                break;
            case kReportCount:
                globals.report_count = raw;
                break;
            case kPush:
                pushed.push_back(globals);
                break;
            case kPop:
                if (pushed.empty()) {
                    return {};
                }
                globals = pushed.back();
                pushed.pop_back();
                break;
            default:
                break;
            }
            continue;
        }
        if (type == 2) {  // local
            switch (tag) {
            case kUsage:
                usages.push_back({raw, size == 4});
                break;
            case kUsageMin:
                usage_min = LocalUsage{raw, size == 4};
                break;
            case kUsageMax:
                if (usage_min) {
                    const std::uint32_t page = size == 4 ? raw >> 16 : 0;
                    const std::uint32_t low = usage_min->value & (usage_min->extended ? 0xFFFF : 0xFFFFFFFF);
                    const std::uint32_t high = size == 4 ? raw & 0xFFFF : raw;
                    if (high < low || high - low > kLongestRange) {
                        return {};
                    }
                    for (std::uint32_t usage = low; usage <= high; ++usage) {
                        if (usage_min->extended || size == 4) {
                            const std::uint32_t usage_page = usage_min->extended ? usage_min->value >> 16 : page;
                            usages.push_back({(usage_page << 16) | usage, true});
                        } else {
                            usages.push_back({usage, false});
                        }
                    }
                    usage_min.reset();
                }
                break;
            default:
                break;
            }
            continue;
        }
        if (type != 0) {
            continue;  // reserved
        }

        std::vector<std::uint32_t> resolved;
        for (const LocalUsage& usage : usages) {
            resolved.push_back(full_usage(usage, globals.usage_page));
        }
        usages.clear();
        usage_min.reset();

        switch (tag) {
        case kCollection: {
            const std::uint32_t usage = resolved.empty() ? 0 : resolved.front();
            collections.push_back({raw, usage});
            parsed.collection_usages_.push_back(usage);
            break;
        }
        case kEndCollection:
            if (collections.empty()) {
                return {};
            }
            collections.pop_back();
            break;
        case kInput:
        case kOutput:
        case kFeature: {
            HidField field;
            field.type = tag == kInput ? ReportType::input : tag == kOutput ? ReportType::output : ReportType::feature;
            field.report_id = globals.report_id;
            field.size = globals.report_size;
            field.count = globals.report_count;
            field.logical_min = static_cast<std::int32_t>(globals.logical_min);
            const std::int64_t logical_max = globals.logical_min < 0
                                                 ? as_signed(globals.logical_max_raw, globals.logical_max_size)
                                                 : static_cast<std::int64_t>(globals.logical_max_raw);
            field.logical_max = static_cast<std::int32_t>(std::min<std::int64_t>(logical_max, INT32_MAX));
            field.unit_exponent = globals.unit_exponent;
            field.constant = raw & 0x1;
            field.variable = raw & 0x2;
            if (field.size == 0 || field.size > 32 || field.count == 0) {
                break;
            }
            std::uint32_t& offset = bits[{field.type, field.report_id}];
            field.bit_offset = offset;
            offset += field.size * field.count;
            if (field.variable && !resolved.empty()) {
                // One usage a slot, the last repeated over any slots left.
                for (std::uint32_t i = 0; i < field.count; ++i) {
                    field.usages.push_back(resolved[std::min<std::size_t>(i, resolved.size() - 1)]);
                }
            } else {
                field.usages = resolved;
            }
            for (auto it = collections.rbegin(); it != collections.rend(); ++it) {
                if (it->type == kLogicalCollection) {
                    field.logical = it->usage;
                    break;
                }
            }
            if (collections.size() >= 2 && collections[collections.size() - 2].type == kLogicalCollection) {
                field.outer_logical = collections[collections.size() - 2].usage;
            }
            parsed.fields_.push_back(std::move(field));
            break;
        }
        default:
            break;
        }
    }
    if (!collections.empty()) {
        return {};
    }
    if (parsed.numbered_) {
        for (HidField& field : parsed.fields_) {
            field.bit_offset += 8;
        }
    }
    return parsed;
}

std::size_t ReportDescriptor::length(ReportType type, std::uint8_t id) const {
    std::uint32_t end = 0;
    bool found = false;
    for (const HidField& field : fields_) {
        if (field.type == type && field.report_id == id) {
            end = std::max(end, field.bit_offset + field.size * field.count);
            found = true;
        }
    }
    if (!found) {
        return 0;
    }
    return (end + 7) / 8;  // the offsets already count the ID byte
}

std::vector<std::uint8_t> ReportDescriptor::report_ids(ReportType type) const {
    std::vector<std::uint8_t> ids;
    for (const HidField& field : fields_) {
        if (field.type == type && std::find(ids.begin(), ids.end(), field.report_id) == ids.end()) {
            ids.push_back(field.report_id);
        }
    }
    return ids;
}

bool ReportDescriptor::uses_page(std::uint16_t page) const {
    const auto on_page = [page](std::uint32_t usage) { return (usage >> 16) == page; };
    if (std::any_of(collection_usages_.begin(), collection_usages_.end(), on_page)) {
        return true;
    }
    return std::any_of(fields_.begin(), fields_.end(), [&](const HidField& field) {
        return std::any_of(field.usages.begin(), field.usages.end(), on_page);
    });
}

void put_bits(Bytes& report, std::uint32_t bit_offset, std::uint32_t size, std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (std::uint32_t i = 0; i < size; ++i) {
        const std::uint32_t bit = bit_offset + i;
        if (bit / 8 >= report.size()) {
            return;
        }
        const auto mask = static_cast<std::uint8_t>(1u << (bit % 8));
        if ((bits >> i) & 1u) {
            report[bit / 8] |= mask;
        } else {
            report[bit / 8] &= static_cast<std::uint8_t>(~mask);
        }
    }
}

std::int64_t get_bits(const Bytes& report, std::uint32_t bit_offset, std::uint32_t size, bool is_signed) {
    std::uint64_t value = 0;
    for (std::uint32_t i = 0; i < size; ++i) {
        const std::uint32_t bit = bit_offset + i;
        if (bit / 8 < report.size() && ((report[bit / 8] >> (bit % 8)) & 1u)) {
            value |= std::uint64_t{1} << i;
        }
    }
    if (is_signed && size > 0 && size < 64 && ((value >> (size - 1)) & 1u)) {
        value |= ~std::uint64_t{0} << size;
    }
    return static_cast<std::int64_t>(value);
}
