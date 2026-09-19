// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_setting_group.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

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

[[nodiscard]] wire::EncodeResult read_base_object(
    const void*,
    const std::span<std::uint8_t> destination) noexcept {
    constexpr std::size_t required = 3U;
    if (destination.size() < required) {
        return {wire::EncodeStatus::buffer_too_small, 0U, required};
    }
    destination[0] = 0x83U;
    destination[1] = 0x01U;
    destination[2] = 0x00U;
    return {wire::EncodeStatus::ok, required, required};
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
        mms::MmsStaticObjectEntry{"LD0", "LLN0", root_type, read_base_object, nullptr}};
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
