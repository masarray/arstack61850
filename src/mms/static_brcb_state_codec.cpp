// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_state_codec.hpp"

#include "ariec61850/mms/static_brcb_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

constexpr std::array<std::uint8_t, 8U> kMagicV1{
    'A','R','B','R','C','B','Q','1'};
constexpr std::array<std::uint8_t, 8U> kMagicV2{
    'A','R','B','R','C','B','Q','2'};
constexpr std::size_t kHeaderBytesV1 = 48U;
constexpr std::size_t kHeaderBytesV2 = 56U;
constexpr std::size_t kEntryHeaderBytes = 16U;
constexpr std::uint8_t kReplayGapFlag = 0x01U;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ULL;
constexpr std::uint64_t kFnvPrime = 1099511628211ULL;

void write_u16(
    const std::span<std::uint8_t> destination,
    const std::size_t offset,
    const std::uint16_t value) noexcept {
    destination[offset] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[offset + 1U] = static_cast<std::uint8_t>(value & 0xFFU);
}

void write_u32(
    const std::span<std::uint8_t> destination,
    const std::size_t offset,
    const std::uint32_t value) noexcept {
    for (std::size_t index = 0U; index < 4U; ++index) {
        const auto shift = static_cast<unsigned>((3U - index) * 8U);
        destination[offset + index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

void write_u64(
    const std::span<std::uint8_t> destination,
    const std::size_t offset,
    const std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 8U);
        destination[offset + index] = static_cast<std::uint8_t>((value >> shift) & 0xFFU);
    }
}

[[nodiscard]] std::uint16_t read_u16(
    const std::span<const std::uint8_t> source,
    const std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(source[offset]) << 8U) |
        static_cast<std::uint16_t>(source[offset + 1U]));
}

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::uint8_t> source,
    const std::size_t offset) noexcept {
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < 4U; ++index) {
        value = static_cast<std::uint32_t>((value << 8U) | source[offset + index]);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64(
    const std::span<const std::uint8_t> source,
    const std::size_t offset) noexcept {
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < 8U; ++index) {
        value = (value << 8U) | source[offset + index];
    }
    return value;
}

[[nodiscard]] std::uint64_t next_entry_number(const std::uint64_t value) noexcept {
    return value == std::numeric_limits<std::uint64_t>::max() ? 1U : value + 1U;
}

void hash_byte(std::uint64_t& hash, const std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u64(std::uint64_t& hash, const std::uint64_t value) noexcept {
    for (std::size_t index = 0U; index < 8U; ++index) {
        const auto shift = static_cast<unsigned>((7U - index) * 8U);
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
}

void hash_text(std::uint64_t& hash, const std::string_view text) noexcept {
    hash_u64(hash, static_cast<std::uint64_t>(text.size()));
    for (const auto character : text) {
        hash_byte(hash, static_cast<std::uint8_t>(character));
    }
}

[[nodiscard]] std::uint64_t definition_fingerprint(
    const MmsStaticBrcbDefinition& definition) noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_text(hash, definition.domain);
    hash_text(hash, definition.item);
    hash_text(hash, definition.report_id);
    hash_text(hash, definition.data_set_domain);
    hash_text(hash, definition.data_set_item);
    hash_u64(hash, definition.conf_revision);
    for (const auto field : definition.optional_fields) {
        hash_byte(hash, field);
    }
    hash_u64(hash, definition.buffer_time_ms);
    hash_byte(hash, definition.trigger_options);
    return hash;
}

[[nodiscard]] bool add_required(
    std::size_t& required,
    const std::size_t extra) noexcept {
    if (extra > std::numeric_limits<std::size_t>::max() - required) {
        return false;
    }
    required += extra;
    return true;
}

[[nodiscard]] std::uint64_t decode_entry_id(
    const std::span<const std::uint8_t> entry_id) noexcept {
    std::uint64_t value = 0U;
    for (const auto byte : entry_id) {
        value = (value << 8U) | byte;
    }
    return value;
}

void clear_slots(const std::span<MmsStaticBrcbSlot> slots) noexcept {
    for (auto& slot : slots) {
        const auto storage = slot.storage;
        slot = {};
        slot.storage = storage;
    }
}

[[nodiscard]] bool magic_matches(
    const std::span<const std::uint8_t> source,
    const std::span<const std::uint8_t> magic) noexcept {
    return source.size() >= magic.size() &&
        std::equal(magic.begin(), magic.end(), source.begin());
}

} // namespace

MmsStaticBrcbStateResult MmsStaticBrcbStateCodec::encode(
    const MmsStaticBrcbRuntime& runtime,
    const std::span<std::uint8_t> destination) noexcept {
    MmsStaticBrcbStateResult result;
    if (!runtime.initialized_ || runtime.definition_ == nullptr ||
        runtime.pending_ == nullptr || runtime.slots_.empty() ||
        runtime.head_ >= runtime.slots_.size() ||
        runtime.count_ > runtime.slots_.size() ||
        runtime.delivery_offset_ > runtime.count_ ||
        runtime.count_ > std::numeric_limits<std::uint32_t>::max() ||
        runtime.delivery_offset_ > std::numeric_limits<std::uint32_t>::max()) {
        result.status = MmsStaticBrcbStateStatus::invalid_runtime;
        return result;
    }

    std::size_t required = kHeaderBytesV2;
    for (std::size_t logical = 0U; logical < runtime.count_; ++logical) {
        const auto physical = (runtime.head_ + logical) % runtime.slots_.size();
        const auto& slot = runtime.slots_[physical];
        if (!slot.occupied || slot.bytes == 0U || slot.bytes > slot.storage.size() ||
            slot.bytes > std::numeric_limits<std::uint32_t>::max() ||
            !add_required(required, kEntryHeaderBytes) ||
            !add_required(required, slot.bytes)) {
            result.status = MmsStaticBrcbStateStatus::invalid_runtime;
            return result;
        }
    }
    result.required_bytes = required;
    if (destination.size() < required) {
        result.status = MmsStaticBrcbStateStatus::buffer_too_small;
        return result;
    }

    auto output = destination.first(required);
    std::fill(output.begin(), output.end(), std::uint8_t{0U});
    std::copy(kMagicV2.begin(), kMagicV2.end(), output.begin());
    write_u16(output, 8U, format_version);
    write_u16(output, 10U, static_cast<std::uint16_t>(kHeaderBytesV2));
    write_u64(output, 12U, definition_fingerprint(*runtime.definition_));
    write_u32(output, 20U, static_cast<std::uint32_t>(runtime.count_));
    write_u64(output, 24U, runtime.next_entry_number_);
    write_u64(output, 32U, runtime.dropped_reports_);
    output[40U] = runtime.sequence_number_;
    output[41U] = runtime.replay_gap_ ? kReplayGapFlag : 0U;
    write_u32(output, 44U, static_cast<std::uint32_t>(runtime.delivery_offset_));

    std::size_t offset = kHeaderBytesV2;
    for (std::size_t logical = 0U; logical < runtime.count_; ++logical) {
        const auto physical = (runtime.head_ + logical) % runtime.slots_.size();
        const auto& slot = runtime.slots_[physical];
        write_u32(output, offset, static_cast<std::uint32_t>(slot.bytes));
        std::copy(
            slot.entry_id.begin(),
            slot.entry_id.end(),
            output.subspan(offset + 4U).begin());
        output[offset + 12U] = slot.sequence_number;
        output[offset + 13U] = slot.buffer_overflow ? 1U : 0U;
        offset += kEntryHeaderBytes;
        std::copy_n(
            slot.storage.begin(),
            slot.bytes,
            output.subspan(offset).begin());
        offset += slot.bytes;
    }

    result.status = MmsStaticBrcbStateStatus::ok;
    result.bytes_written = required;
    return result;
}

MmsStaticBrcbStateResult MmsStaticBrcbStateCodec::restore(
    MmsStaticBrcbRuntime& runtime,
    const std::span<const std::uint8_t> source) noexcept {
    MmsStaticBrcbStateResult result;
    result.required_bytes = source.size();
    if (!runtime.initialized_ || runtime.definition_ == nullptr ||
        runtime.pending_ == nullptr || runtime.slots_.empty()) {
        result.status = MmsStaticBrcbStateStatus::invalid_runtime;
        return result;
    }

    const bool is_v2 = source.size() >= kHeaderBytesV2 &&
        magic_matches(source, kMagicV2) &&
        read_u16(source, 8U) == format_version &&
        read_u16(source, 10U) == static_cast<std::uint16_t>(kHeaderBytesV2);
    const bool is_v1 = source.size() >= kHeaderBytesV1 &&
        magic_matches(source, kMagicV1) &&
        read_u16(source, 8U) == legacy_format_version &&
        read_u16(source, 10U) == static_cast<std::uint16_t>(kHeaderBytesV1);
    if (!is_v2 && !is_v1) {
        result.status = MmsStaticBrcbStateStatus::invalid_state;
        return result;
    }

    const auto header_bytes = is_v2 ? kHeaderBytesV2 : kHeaderBytesV1;
    std::size_t delivery_offset = 0U;
    bool replay_gap = false;
    if (is_v2) {
        if ((source[41U] & static_cast<std::uint8_t>(~kReplayGapFlag)) != 0U ||
            source[42U] != 0U || source[43U] != 0U) {
            result.status = MmsStaticBrcbStateStatus::invalid_state;
            return result;
        }
        for (std::size_t index = 48U; index < kHeaderBytesV2; ++index) {
            if (source[index] != 0U) {
                result.status = MmsStaticBrcbStateStatus::invalid_state;
                return result;
            }
        }
        replay_gap = (source[41U] & kReplayGapFlag) != 0U;
        delivery_offset = static_cast<std::size_t>(read_u32(source, 44U));
    } else {
        for (std::size_t index = 41U; index < kHeaderBytesV1; ++index) {
            if (source[index] != 0U) {
                result.status = MmsStaticBrcbStateStatus::invalid_state;
                return result;
            }
        }
    }

    if (read_u64(source, 12U) != definition_fingerprint(*runtime.definition_)) {
        result.status = MmsStaticBrcbStateStatus::definition_mismatch;
        return result;
    }

    const auto count = static_cast<std::size_t>(read_u32(source, 20U));
    const auto restored_next_entry = read_u64(source, 24U);
    const auto restored_dropped = read_u64(source, 32U);
    const auto restored_sequence = source[40U];
    if (count > runtime.slots_.size()) {
        result.status = MmsStaticBrcbStateStatus::capacity_mismatch;
        return result;
    }
    if (delivery_offset > count || restored_next_entry == 0U) {
        result.status = MmsStaticBrcbStateStatus::invalid_state;
        return result;
    }

    std::size_t offset = header_bytes;
    std::uint64_t previous_entry_number = 0U;
    std::uint8_t last_sequence = 0U;
    for (std::size_t logical = 0U; logical < count; ++logical) {
        if (offset > source.size() || source.size() - offset < kEntryHeaderBytes) {
            result.status = MmsStaticBrcbStateStatus::invalid_state;
            return result;
        }
        const auto pdu_bytes = static_cast<std::size_t>(read_u32(source, offset));
        const auto entry_id = source.subspan(offset + 4U, 8U);
        const auto sequence = source[offset + 12U];
        const auto overflow = source[offset + 13U];
        if (pdu_bytes == 0U || overflow > 1U || source[offset + 14U] != 0U ||
            source[offset + 15U] != 0U ||
            pdu_bytes > runtime.slots_[logical].storage.size()) {
            result.status = pdu_bytes > runtime.slots_[logical].storage.size()
                ? MmsStaticBrcbStateStatus::capacity_mismatch
                : MmsStaticBrcbStateStatus::invalid_state;
            return result;
        }
        offset += kEntryHeaderBytes;
        if (offset > source.size() || pdu_bytes > source.size() - offset) {
            result.status = MmsStaticBrcbStateStatus::invalid_state;
            return result;
        }

        const auto entry_number = decode_entry_id(entry_id);
        if (entry_number == 0U ||
            (logical != 0U && entry_number != next_entry_number(previous_entry_number))) {
            result.status = MmsStaticBrcbStateStatus::invalid_state;
            return result;
        }
        previous_entry_number = entry_number;
        last_sequence = sequence;
        offset += pdu_bytes;
    }
    if (offset != source.size() ||
        (count != 0U && restored_next_entry != next_entry_number(previous_entry_number)) ||
        (count != 0U && restored_sequence != last_sequence)) {
        result.status = MmsStaticBrcbStateStatus::invalid_state;
        return result;
    }

    clear_slots(runtime.slots_);
    offset = header_bytes;
    for (std::size_t logical = 0U; logical < count; ++logical) {
        auto& slot = runtime.slots_[logical];
        const auto pdu_bytes = static_cast<std::size_t>(read_u32(source, offset));
        std::copy_n(
            source.subspan(offset + 4U).begin(),
            slot.entry_id.size(),
            slot.entry_id.begin());
        slot.sequence_number = source[offset + 12U];
        slot.buffer_overflow = source[offset + 13U] != 0U;
        slot.bytes = pdu_bytes;
        slot.occupied = true;
        offset += kEntryHeaderBytes;
        std::copy_n(
            source.subspan(offset).begin(),
            pdu_bytes,
            slot.storage.begin());
        offset += pdu_bytes;
    }

    runtime.head_ = 0U;
    runtime.count_ = count;
    runtime.delivery_offset_ = delivery_offset;
    runtime.next_entry_number_ = restored_next_entry;
    runtime.dropped_reports_ = restored_dropped;
    runtime.sequence_number_ = restored_sequence;
    runtime.replay_gap_ = replay_gap;
    runtime.queue_revision_ = runtime.queue_revision_ ==
            std::numeric_limits<std::uint32_t>::max()
        ? 1U
        : runtime.queue_revision_ + 1U;
    *runtime.pending_ = {};
    runtime.pending_->revision = 1U;
    runtime.enabled_ = false;

    result.status = MmsStaticBrcbStateStatus::ok;
    result.bytes_written = source.size();
    return result;
}

} // namespace ar::iec61850::mms
