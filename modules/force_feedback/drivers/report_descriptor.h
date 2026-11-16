#pragma once

// A HID report descriptor, parsed into its fields: which report each sits
// in, where, how wide, its logical range and unit exponent, its usages, and
// the usages of the logical collections round it (the HID 1.11
// specification, section 6.2.2). The registry reads it to tell what a device
// is; HID PID reads it to build its reports, as the kernel's hid-pidff does.

#include "wheel_driver.h"

#include <cstdint>
#include <vector>

struct HidField {
    ReportType type = ReportType::input;
    std::uint8_t report_id = 0;
    // Where it starts in the report, counting the report ID byte when there is one.
    std::uint32_t bit_offset = 0;
    std::uint32_t size = 0;
    std::uint32_t count = 0;
    std::int32_t logical_min = 0;
    std::int32_t logical_max = 0;
    std::int32_t unit_exponent = 0;
    bool variable = false;
    bool constant = false;
    // Full usages (page << 16 | id). A variable field has one per slot; an
    // array field lists the usages a slot can hold.
    std::vector<std::uint32_t> usages;
    // The usage of the innermost logical collection round it (0 if none),
    // and of the collection round that one when it's logical too.
    std::uint32_t logical = 0;
    std::uint32_t outer_logical = 0;

    // The slot holding `usage`, if it has one slot per usage; -1 otherwise.
    int slot_of(std::uint32_t usage) const;
    // `usage`'s place among an array field's usages plus one, which is the
    // value that selects it; 0 if it isn't there.
    int key_of(std::uint32_t usage) const;
};

class ReportDescriptor {
public:
    // A malformed descriptor parses as empty.
    static ReportDescriptor parse(const Bytes& descriptor);

    const std::vector<HidField>& fields() const { return fields_; }
    // Whether its reports carry an ID byte.
    bool numbered() const { return numbered_; }
    // A report's length in bytes, its ID byte included; 0 if there's no such report.
    std::size_t length(ReportType type, std::uint8_t id) const;
    // The IDs of a kind of report, in the order they first appear.
    std::vector<std::uint8_t> report_ids(ReportType type) const;
    // Whether any collection or field uses a usage page (0x0F is PID's).
    bool uses_page(std::uint16_t page) const;

private:
    std::vector<HidField> fields_;
    std::vector<std::uint32_t> collection_usages_;
    bool numbered_ = false;
};

// Reading and writing a field's slot in a report buffer, little-endian bits.
void put_bits(Bytes& report, std::uint32_t bit_offset, std::uint32_t size, std::int64_t value);
std::int64_t get_bits(const Bytes& report, std::uint32_t bit_offset, std::uint32_t size, bool is_signed);
