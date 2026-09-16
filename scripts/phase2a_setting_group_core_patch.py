from pathlib import Path


def replace(path: str, old: str, new: str, count: int = 1) -> None:
    p = Path(path)
    text = p.read_text(encoding="utf-8")
    actual = text.count(old)
    if actual != count:
        raise SystemExit(f"{path}: expected {count} occurrence(s), found {actual}")
    p.write_text(text.replace(old, new, count), encoding="utf-8")


def write(path: str, content: str) -> None:
    p = Path(path)
    if p.exists():
        raise SystemExit(f"{path}: already exists")
    p.parent.mkdir(parents=True, exist_ok=True)
    p.write_text(content, encoding="utf-8")


# Canonical SCL model: preserve SettingControl as first-class source semantics.
replace(
    "include/ariec61850/scl/model.hpp",
    '''struct SclConflict final {\n''',
    '''struct SclSettingControl final {\n    std::string ied_name;\n    std::string ld_inst;\n    std::string logical_node_path;\n    std::string control_block_reference;\n    std::optional<std::uint32_t> number_of_setting_groups;\n    std::optional<std::uint32_t> active_setting_group;\n\n    [[nodiscard]] bool valid() const noexcept {\n        return number_of_setting_groups.has_value() &&\n            active_setting_group.has_value() &&\n            *number_of_setting_groups != 0U &&\n            *active_setting_group != 0U &&\n            *active_setting_group <= *number_of_setting_groups;\n    }\n\n    friend bool operator==(const SclSettingControl&, const SclSettingControl&) = default;\n};\n\nstruct SclConflict final {\n''')
replace(
    "include/ariec61850/scl/model.hpp",
    '''    std::vector<SclReportControl> report_controls;\n    std::vector<std::string> warnings;\n''',
    '''    std::vector<SclReportControl> report_controls;\n    std::vector<SclSettingControl> setting_controls;\n    std::vector<std::string> warnings;\n''')

# Preserve missing/malformed integer state instead of collapsing it to zero.
replace(
    "src/scl/parser_part_02.inc",
    '''std::uint32_t uint_attribute(const XmlNode& node, const std::string_view name) {\n''',
    '''std::optional<std::uint32_t> optional_uint_attribute(\n    const XmlNode& node,\n    const std::string_view name) {\n    const auto text = trim_copy(attribute(&node, name));\n    if (text.empty()) return std::nullopt;\n    std::uint32_t value{};\n    const auto result = std::from_chars(\n        text.data(), text.data() + text.size(), value, 10);\n    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) {\n        return std::nullopt;\n    }\n    return value;\n}\n\nstd::uint32_t uint_attribute(const XmlNode& node, const std::string_view name) {\n''')

replace(
    "src/scl/parser_part_05_03.inc",
    '''                    document.report_controls.push_back(std::move(report));\n                }\n            }\n''',
    '''                    document.report_controls.push_back(std::move(report));\n                }\n\n                if (is(logical_node, "LN0")) {\n                    for (const auto* control : direct_children(logical_node, "SettingControl")) {\n                        SclSettingControl setting;\n                        setting.ied_name = ied_name;\n                        setting.ld_inst = ld_inst;\n                        setting.logical_node_path = logical_node_path;\n                        setting.control_block_reference =\n                            ied_name + ld_inst + "/" + logical_node_path + "$SP$SGCB";\n                        setting.number_of_setting_groups =\n                            optional_uint_attribute(*control, "numOfSGs");\n                        setting.active_setting_group =\n                            optional_uint_attribute(*control, "actSG");\n                        if (!setting.valid()) {\n                            document.warnings.push_back(\n                                "SettingControl '" + setting.control_block_reference +\n                                "' has invalid or missing numOfSGs/actSG and will not be "\n                                "operational in simulator service projection.");\n                        }\n                        document.setting_controls.push_back(std::move(setting));\n                    }\n                }\n            }\n''')

# Reusable allocation-free SGCB MMS object facade.
write(
    "include/ariec61850/mms/static_setting_group.hpp",
    r'''// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_object_table.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

using MmsStaticSettingGroupUtcCallback = bool (*)(
    const void* context,
    std::span<std::uint8_t, 8U> destination) noexcept;

struct MmsStaticSettingGroupSharedState final {
    explicit MmsStaticSettingGroupSharedState(
        const std::uint32_t number_of_groups,
        const std::uint32_t active_group) noexcept
        : number_of_setting_groups{number_of_groups},
          active_setting_group{active_group} {}

    [[nodiscard]] bool valid() const noexcept {
        const auto active = active_setting_group.load(std::memory_order_acquire);
        return number_of_setting_groups != 0U &&
            active != 0U && active <= number_of_setting_groups;
    }

    std::uint32_t number_of_setting_groups{};
    std::atomic<std::uint32_t> active_setting_group{};
    // Eight encoded IEC 61850 UTC-time octets packed big-endian. Zero means
    // the simulator has no source evidence for a previous activation time.
    std::atomic<std::uint64_t> last_activation_time_be{};
    // ActSG changes are rare configuration operations. Contention is rejected
    // immediately instead of blocking a protocol worker.
    std::atomic_flag activation_in_progress = ATOMIC_FLAG_INIT;
};

struct MmsStaticSettingGroupDefinition final {
    std::string_view domain;
    std::string_view item; // canonical SGCB root, e.g. LLN0$SP$SGCB
};

enum class MmsStaticSettingGroupAttribute : std::uint8_t {
    active_setting_group,
    confirm_edit,
    edit_setting_group,
    last_activation_time,
    number_of_setting_groups,
};

struct MmsStaticSettingGroupObjectContext final {
    MmsStaticSettingGroupSharedState* state{};
    MmsStaticSettingGroupAttribute attribute{
        MmsStaticSettingGroupAttribute::active_setting_group};
    MmsStaticSettingGroupUtcCallback now_utc{};
    const void* now_context{};
};

// Exact five-attribute SGCB facade used by engineering clients:
// ActSG,CnfEdit,EditSG,LActTm,NumOfSG. Full SE/EditSG/CnfEdit transactions are
// intentionally outside this runtime; only guarded ActSG activation is writable.
class MmsStaticSettingGroupObjectBank final {
public:
    static constexpr std::size_t attributes_per_control_block = 5U;

    MmsStaticSettingGroupObjectBank(
        const MmsStaticSettingGroupDefinition& definition,
        MmsStaticSettingGroupSharedState& state,
        std::span<const MmsStaticObjectEntry> base_objects,
        std::span<MmsStaticObjectEntry> object_storage,
        std::span<MmsStaticSettingGroupObjectContext> context_storage,
        std::span<char> name_storage,
        MmsStaticSettingGroupUtcCallback now_utc = nullptr,
        const void* now_context = nullptr) noexcept
        : definition_{&definition},
          state_{&state},
          base_objects_{base_objects},
          object_storage_{object_storage},
          context_storage_{context_storage},
          name_storage_{name_storage},
          now_utc_{now_utc},
          now_context_{now_context} {}

    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] std::size_t required_object_capacity() const noexcept;
    [[nodiscard]] constexpr std::size_t required_context_capacity() const noexcept {
        return attributes_per_control_block;
    }
    [[nodiscard]] std::size_t required_name_bytes() const noexcept;

    [[nodiscard]] constexpr bool valid() const noexcept { return initialized_; }
    [[nodiscard]] constexpr const MmsStaticObjectTable& table() const noexcept {
        return table_;
    }

private:
    const MmsStaticSettingGroupDefinition* definition_{};
    MmsStaticSettingGroupSharedState* state_{};
    std::span<const MmsStaticObjectEntry> base_objects_{};
    std::span<MmsStaticObjectEntry> object_storage_{};
    std::span<MmsStaticSettingGroupObjectContext> context_storage_{};
    std::span<char> name_storage_{};
    MmsStaticSettingGroupUtcCallback now_utc_{};
    const void* now_context_{};
    MmsStaticObjectTable table_{std::span<const MmsStaticObjectEntry>{}};
    bool initialized_{};
};

} // namespace ar::iec61850::mms
''')

write(
    "src/mms/static_setting_group.cpp",
    r'''// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_setting_group.hpp"

#include "ariec61850/asn1/ber_span_reader.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

constexpr std::array<std::uint8_t, 3U> kUnsigned8Type{0x86U, 0x01U, 0x08U};
constexpr std::array<std::uint8_t, 2U> kBooleanType{0x83U, 0x00U};
constexpr std::array<std::uint8_t, 2U> kUtcTimeType{0x91U, 0x00U};
constexpr std::uint32_t kTemporarilyUnavailable = 2U;
constexpr std::uint32_t kTypeInconsistent = 7U;
constexpr std::uint32_t kObjectValueInvalid = 11U;

constexpr std::array<MmsStaticSettingGroupAttribute,
                     MmsStaticSettingGroupObjectBank::attributes_per_control_block>
    kAttributes{
        MmsStaticSettingGroupAttribute::active_setting_group,
        MmsStaticSettingGroupAttribute::confirm_edit,
        MmsStaticSettingGroupAttribute::edit_setting_group,
        MmsStaticSettingGroupAttribute::last_activation_time,
        MmsStaticSettingGroupAttribute::number_of_setting_groups};

constexpr std::array<std::string_view,
                     MmsStaticSettingGroupObjectBank::attributes_per_control_block>
    kSuffixes{"ActSG", "CnfEdit", "EditSG", "LActTm", "NumOfSG"};

[[nodiscard]] std::span<const std::uint8_t> type_for(
    const MmsStaticSettingGroupAttribute attribute) noexcept {
    switch (attribute) {
    case MmsStaticSettingGroupAttribute::active_setting_group:
    case MmsStaticSettingGroupAttribute::edit_setting_group:
    case MmsStaticSettingGroupAttribute::number_of_setting_groups:
        return kUnsigned8Type;
    case MmsStaticSettingGroupAttribute::confirm_edit:
        return kBooleanType;
    case MmsStaticSettingGroupAttribute::last_activation_time:
        return kUtcTimeType;
    }
    return {};
}

[[nodiscard]] wire::EncodeResult encode_unsigned8(
    const std::uint32_t value,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (value > 0xFFU) {
        return {wire::EncodeStatus::value_out_of_range, 0U, required};
    }
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x86U;
    destination[1] = 0x01U;
    destination[2] = static_cast<std::uint8_t>(value);
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult encode_boolean(
    const bool value,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = value ? 0xFFU : 0x00U;
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] std::uint64_t pack_utc(
    const std::span<const std::uint8_t, 8U> bytes) noexcept {
    std::uint64_t packed{};
    for (const auto byte : bytes) {
        packed = (packed << 8U) | static_cast<std::uint64_t>(byte);
    }
    return packed;
}

void unpack_utc(
    std::uint64_t packed,
    const std::span<std::uint8_t, 8U> destination) noexcept {
    for (std::size_t index = destination.size(); index-- > 0U;) {
        destination[index] = static_cast<std::uint8_t>(packed & 0xFFU);
        packed >>= 8U;
    }
}

[[nodiscard]] wire::EncodeResult encode_utc(
    const std::uint64_t packed,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 10U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x91U;
    destination[1] = 0x08U;
    std::array<std::uint8_t, 8U> bytes{};
    unpack_utc(packed, bytes);
    std::copy(bytes.begin(), bytes.end(), destination.begin() + 2);
    return {wire::EncodeStatus::ok, required, required};
}

[[nodiscard]] wire::EncodeResult read_attribute(
    const void* raw_context,
    const std::span<std::uint8_t> destination) noexcept {
    const auto* context = static_cast<const MmsStaticSettingGroupObjectContext*>(raw_context);
    if (context == nullptr || context->state == nullptr) {
        return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
    }
    switch (context->attribute) {
    case MmsStaticSettingGroupAttribute::active_setting_group:
        return encode_unsigned8(
            context->state->active_setting_group.load(std::memory_order_acquire),
            destination);
    case MmsStaticSettingGroupAttribute::confirm_edit:
        return encode_boolean(false, destination);
    case MmsStaticSettingGroupAttribute::edit_setting_group:
        return encode_unsigned8(0U, destination);
    case MmsStaticSettingGroupAttribute::last_activation_time:
        return encode_utc(
            context->state->last_activation_time_be.load(std::memory_order_acquire),
            destination);
    case MmsStaticSettingGroupAttribute::number_of_setting_groups:
        return encode_unsigned8(context->state->number_of_setting_groups, destination);
    }
    return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
}

[[nodiscard]] bool decode_unsigned8(
    const std::span<const std::uint8_t> encoded,
    std::uint32_t& value) noexcept {
    asn1::BerTlvView tlv;
    if (!asn1::BerSpanReader::try_read_exact(encoded, tlv) ||
        tlv.tag_class != asn1::BerClass::context_specific ||
        tlv.tag_number != 6 || tlv.constructed || tlv.value.empty() ||
        tlv.value.size() > sizeof(std::uint32_t)) {
        return false;
    }
    const auto parsed = asn1::BerSpanReader::read_unsigned_integer(tlv);
    if (!parsed || *parsed > 0xFFU) return false;
    value = static_cast<std::uint32_t>(*parsed);
    return true;
}

struct ActivationGuard final {
    std::atomic_flag* flag{};
    ~ActivationGuard() {
        if (flag != nullptr) flag->clear(std::memory_order_release);
    }
};

[[nodiscard]] MmsStaticWriteResult write_active_setting_group(
    void* raw_context,
    const std::span<const std::uint8_t> encoded_data) noexcept {
    auto* context = static_cast<MmsStaticSettingGroupObjectContext*>(raw_context);
    if (context == nullptr || context->state == nullptr) {
        return {false, kObjectValueInvalid};
    }
    std::uint32_t requested{};
    if (!decode_unsigned8(encoded_data, requested)) {
        return {false, kTypeInconsistent};
    }
    if (requested == 0U || requested > context->state->number_of_setting_groups) {
        return {false, kObjectValueInvalid};
    }
    if (context->state->activation_in_progress.test_and_set(std::memory_order_acquire)) {
        return {false, kTemporarilyUnavailable};
    }
    ActivationGuard guard{&context->state->activation_in_progress};

    std::array<std::uint8_t, 8U> utc{};
    const bool have_time = context->now_utc != nullptr &&
        context->now_utc(context->now_context, utc);
    context->state->active_setting_group.store(requested, std::memory_order_release);
    if (have_time) {
        context->state->last_activation_time_be.store(
            pack_utc(utc), std::memory_order_release);
    }
    return {true, 0U};
}

} // namespace

std::size_t MmsStaticSettingGroupObjectBank::required_object_capacity() const noexcept {
    if (base_objects_.size() >
        std::numeric_limits<std::size_t>::max() - attributes_per_control_block) {
        return std::numeric_limits<std::size_t>::max();
    }
    return base_objects_.size() + attributes_per_control_block;
}

std::size_t MmsStaticSettingGroupObjectBank::required_name_bytes() const noexcept {
    if (definition_ == nullptr) return std::numeric_limits<std::size_t>::max();
    std::size_t total{};
    for (const auto suffix : kSuffixes) {
        if (definition_->item.size() >
            MmsServiceSpanCodec::maximum_identifier_bytes - 1U - suffix.size()) {
            return std::numeric_limits<std::size_t>::max();
        }
        const auto required = definition_->item.size() + 1U + suffix.size();
        if (required > std::numeric_limits<std::size_t>::max() - total) {
            return std::numeric_limits<std::size_t>::max();
        }
        total += required;
    }
    return total;
}

bool MmsStaticSettingGroupObjectBank::initialize() noexcept {
    initialized_ = false;
    table_ = MmsStaticObjectTable{std::span<const MmsStaticObjectEntry>{}};
    if (definition_ == nullptr || state_ == nullptr || !state_->valid() ||
        state_->number_of_setting_groups > 0xFFU) {
        return false;
    }

    const auto required_objects = required_object_capacity();
    const auto required_names = required_name_bytes();
    if (required_objects == std::numeric_limits<std::size_t>::max() ||
        required_names == std::numeric_limits<std::size_t>::max() ||
        required_objects > object_storage_.size() ||
        required_objects > MmsStaticObjectTable::maximum_objects ||
        attributes_per_control_block > context_storage_.size() ||
        required_names > name_storage_.size()) {
        return false;
    }

    std::copy(base_objects_.begin(), base_objects_.end(), object_storage_.begin());
    std::size_t object_offset = base_objects_.size();
    std::size_t name_offset{};
    for (std::size_t index = 0U; index < attributes_per_control_block; ++index) {
        const auto suffix = kSuffixes[index];
        const auto name_size = definition_->item.size() + 1U + suffix.size();
        auto* name = name_storage_.data() + name_offset;
        std::copy(definition_->item.begin(), definition_->item.end(), name);
        name[definition_->item.size()] = '$';
        std::copy(suffix.begin(), suffix.end(), name + definition_->item.size() + 1U);

        auto& context = context_storage_[index];
        context = MmsStaticSettingGroupObjectContext{
            state_, kAttributes[index], now_utc_, now_context_};
        const bool writable =
            kAttributes[index] == MmsStaticSettingGroupAttribute::active_setting_group;
        object_storage_[object_offset++] = MmsStaticObjectEntry{
            definition_->domain,
            std::string_view{name, name_size},
            type_for(kAttributes[index]),
            read_attribute,
            &context,
            false,
            writable ? write_active_setting_group : nullptr,
            writable ? &context : nullptr};
        name_offset += name_size;
    }

    std::stable_sort(
        object_storage_.begin(),
        object_storage_.begin() + static_cast<std::ptrdiff_t>(object_offset),
        [](const MmsStaticObjectEntry& left, const MmsStaticObjectEntry& right) noexcept {
            if (left.domain != right.domain) return left.domain < right.domain;
            return left.item < right.item;
        });
    table_ = MmsStaticObjectTable{
        std::span<const MmsStaticObjectEntry>{object_storage_.data(), object_offset}};
    if (!table_.valid()) {
        table_ = MmsStaticObjectTable{std::span<const MmsStaticObjectEntry>{}};
        return false;
    }
    initialized_ = true;
    return true;
}

} // namespace ar::iec61850::mms
''')

# Compile the shared SGCB core in both portable and desktop compatibility server cores.
replace(
    "CMakeLists.txt",
    '''    src/mms/static_direct_control.cpp\n    src/mms/static_dispatcher.cpp\n''',
    '''    src/mms/static_direct_control.cpp\n    src/mms/static_setting_group.cpp\n    src/mms/static_dispatcher.cpp\n''')
replace(
    "apps/ied_simulator/CMakeLists.txt",
    '''    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_direct_control.cpp\n    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_dispatcher.cpp\n''',
    '''    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_direct_control.cpp\n    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_setting_group.cpp\n    ${CMAKE_CURRENT_LIST_DIR}/../../src/mms/static_dispatcher.cpp\n''')

# Deterministic core test: exact types/names, positive activation, invalid values,
# no blocking under contention, immutable non-ActSG attributes.
write(
    "tests/test_mms_static_setting_group.cpp",
    r'''// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_setting_group.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace {
using namespace ar::iec61850;

#define CHECK(condition) do { \
    if (!(condition)) throw std::runtime_error(std::string{"CHECK failed: "} + #condition); \
} while (false)

[[nodiscard]] bool fixed_utc(
    const void*,
    const std::span<std::uint8_t, 8U> destination) noexcept {
    constexpr std::array<std::uint8_t, 8U> value{
        0x65U, 0x53U, 0xF1U, 0x00U, 0x20U, 0x00U, 0x00U, 0x00U};
    std::copy(value.begin(), value.end(), destination.begin());
    return true;
}

[[nodiscard]] mms::MmsObjectNameView object_name(
    const std::string_view domain,
    const std::string_view item) noexcept {
    return {
        mms::MmsObjectNameViewKind::domain_specific,
        {reinterpret_cast<const std::uint8_t*>(domain.data()), domain.size()},
        {reinterpret_cast<const std::uint8_t*>(item.data()), item.size()}};
}

void run() {
    constexpr std::array<std::uint8_t, 2U> root_type{0x83U, 0x00U};
    const std::array<mms::MmsStaticObjectEntry, 1U> base{
        mms::MmsStaticObjectEntry{"LD0", "LLN0", root_type, nullptr, nullptr}};
    const mms::MmsStaticSettingGroupDefinition definition{"LD0", "LLN0$SP$SGCB"};
    mms::MmsStaticSettingGroupSharedState state{3U, 2U};
    std::array<mms::MmsStaticObjectEntry, 6U> objects{};
    std::array<mms::MmsStaticSettingGroupObjectContext, 5U> contexts{};
    std::array<char, 256U> names{};
    mms::MmsStaticSettingGroupObjectBank bank{
        definition, state, base, objects, contexts, names, fixed_utc, nullptr};
    CHECK(bank.initialize());
    CHECK(bank.valid());
    CHECK(bank.table().valid());
    CHECK(bank.required_object_capacity() == 6U);

    const auto* act = bank.table().find(object_name("LD0", "LLN0$SP$SGCB$ActSG"));
    const auto* cnf = bank.table().find(object_name("LD0", "LLN0$SP$SGCB$CnfEdit"));
    const auto* edit = bank.table().find(object_name("LD0", "LLN0$SP$SGCB$EditSG"));
    const auto* time = bank.table().find(object_name("LD0", "LLN0$SP$SGCB$LActTm"));
    const auto* count = bank.table().find(object_name("LD0", "LLN0$SP$SGCB$NumOfSG"));
    CHECK(act != nullptr && act->writable());
    CHECK(cnf != nullptr && !cnf->writable());
    CHECK(edit != nullptr && !edit->writable());
    CHECK(time != nullptr && !time->writable());
    CHECK(count != nullptr && !count->writable());

    std::array<std::uint8_t, 16U> encoded{};
    auto read = act->read(act->context, encoded);
    CHECK(read.success() && read.bytes_written == 3U);
    CHECK(encoded[0] == 0x86U && encoded[1] == 0x01U && encoded[2] == 0x02U);
    read = count->read(count->context, encoded);
    CHECK(read.success() && encoded[2] == 0x03U);
    read = cnf->read(cnf->context, encoded);
    CHECK(read.success() && encoded[0] == 0x83U && encoded[2] == 0x00U);
    read = edit->read(edit->context, encoded);
    CHECK(read.success() && encoded[0] == 0x86U && encoded[2] == 0x00U);
    read = time->read(time->context, encoded);
    CHECK(read.success() && read.bytes_written == 10U);
    CHECK(encoded[0] == 0x91U && encoded[1] == 0x08U);
    for (std::size_t index = 2U; index < 10U; ++index) CHECK(encoded[index] == 0U);

    constexpr std::array<std::uint8_t, 3U> activate3{0x86U, 0x01U, 0x03U};
    auto written = act->write(act->write_context, activate3);
    CHECK(written.success);
    CHECK(state.active_setting_group.load() == 3U);
    read = time->read(time->context, encoded);
    CHECK(read.success());
    constexpr std::array<std::uint8_t, 8U> expected_time{
        0x65U, 0x53U, 0xF1U, 0x00U, 0x20U, 0x00U, 0x00U, 0x00U};
    CHECK(std::equal(expected_time.begin(), expected_time.end(), encoded.begin() + 2));

    constexpr std::array<std::uint8_t, 3U> activate0{0x86U, 0x01U, 0x00U};
    written = act->write(act->write_context, activate0);
    CHECK(!written.success && written.failure_code == 11U);
    CHECK(state.active_setting_group.load() == 3U);
    constexpr std::array<std::uint8_t, 3U> activate4{0x86U, 0x01U, 0x04U};
    written = act->write(act->write_context, activate4);
    CHECK(!written.success && written.failure_code == 11U);
    CHECK(state.active_setting_group.load() == 3U);
    constexpr std::array<std::uint8_t, 3U> wrong_type{0x83U, 0x01U, 0xFFU};
    written = act->write(act->write_context, wrong_type);
    CHECK(!written.success && written.failure_code == 7U);

    CHECK(!state.activation_in_progress.test_and_set());
    written = act->write(act->write_context, activate3);
    CHECK(!written.success && written.failure_code == 2U);
    state.activation_in_progress.clear();

    mms::MmsStaticSettingGroupSharedState invalid{0U, 0U};
    mms::MmsStaticSettingGroupObjectBank invalid_bank{
        definition, invalid, base, objects, contexts, names};
    CHECK(!invalid_bank.initialize());
}
} // namespace

int main() {
    try {
        run();
        return 0;
    } catch (const std::exception&) {
        return 1;
    }
}
''')

replace(
    "CMakeLists.txt",
    '''    add_executable(ariec61850_control_block_read_tests tests/test_control_block_read.cpp)\n''',
    '''    add_executable(ariec61850_mms_static_setting_group_tests tests/test_mms_static_setting_group.cpp)\n    target_link_libraries(ariec61850_mms_static_setting_group_tests PRIVATE ARIEC61850::mms_server_core)\n    target_compile_features(ariec61850_mms_static_setting_group_tests PRIVATE cxx_std_20)\n    ariec61850_apply_warnings(ariec61850_mms_static_setting_group_tests)\n    ariec61850_apply_sanitizers(ariec61850_mms_static_setting_group_tests)\n    add_test(NAME ariec61850_mms_static_setting_group_tests COMMAND ariec61850_mms_static_setting_group_tests)\n\n    add_executable(ariec61850_control_block_read_tests tests/test_control_block_read.cpp)\n''')

# Parser regression is inline so it explicitly covers valid + malformed-present
# SettingControl without coupling unrelated legacy fixtures to new counts.
replace(
    "tests/test_scl.cpp",
    '''void parser_detects_duplicate_ieds_and_missing_dataset_references() {\n''',
    r'''void parser_preserves_setting_control_and_invalid_state() {
    using namespace ar::iec61850::scl;

    constexpr std::string_view valid_xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <IED name="IED1"><AccessPoint><Server><LDevice inst="LD0">
    <LN0 lnClass="LLN0"><SettingControl numOfSGs="3" actSG="2"/></LN0>
  </LDevice></Server></AccessPoint></IED>
</SCL>)xml";
    const auto valid = SclParser{}.parse(valid_xml, "setting-valid.scd");
    CHECK(valid.setting_controls.size() == 1U);
    const auto& control = valid.setting_controls.front();
    CHECK(control.ied_name == "IED1");
    CHECK(control.ld_inst == "LD0");
    CHECK(control.logical_node_path == "LLN0");
    CHECK(control.control_block_reference == "IED1LD0/LLN0$SP$SGCB");
    CHECK(control.number_of_setting_groups == std::optional<std::uint32_t>{3U});
    CHECK(control.active_setting_group == std::optional<std::uint32_t>{2U});
    CHECK(control.valid());
    CHECK(std::none_of(valid.warnings.begin(), valid.warnings.end(), [](const std::string& warning) {
        return warning.find("SettingControl") != std::string::npos;
    }));

    constexpr std::string_view invalid_xml = R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <IED name="IED1"><AccessPoint><Server><LDevice inst="LD0">
    <LN0 lnClass="LLN0"><SettingControl numOfSGs="0" actSG="bogus"/></LN0>
  </LDevice></Server></AccessPoint></IED>
</SCL>)xml";
    const auto invalid = SclParser{}.parse(invalid_xml, "setting-invalid.scd");
    CHECK(invalid.setting_controls.size() == 1U);
    CHECK(!invalid.setting_controls.front().valid());
    CHECK(invalid.setting_controls.front().number_of_setting_groups ==
          std::optional<std::uint32_t>{0U});
    CHECK(!invalid.setting_controls.front().active_setting_group.has_value());
    CHECK(std::any_of(invalid.warnings.begin(), invalid.warnings.end(), [](const std::string& warning) {
        return warning.find("SettingControl 'IED1LD0/LLN0$SP$SGCB'") != std::string::npos &&
               warning.find("invalid or missing") != std::string::npos;
    }));
}

void parser_detects_duplicate_ieds_and_missing_dataset_references() {
''')
replace(
    "tests/test_scl.cpp",
    '''        {"SCL conflicts and warnings", parser_detects_duplicate_ieds_and_missing_dataset_references},\n''',
    '''        {"SCL SettingControl", parser_preserves_setting_control_and_invalid_state},\n        {"SCL conflicts and warnings", parser_detects_duplicate_ieds_and_missing_dataset_references},\n''')

print("Phase2A canonical SettingControl + shared SGCB core patch applied")
