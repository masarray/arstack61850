// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_checkpoint_store.hpp"

#include "ariec61850/mms/static_brcb_runtime.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::mms {
namespace {

constexpr std::array<std::uint8_t, 8U> kHeaderMagic{
    'A','R','B','R','C','B','P','1'};
constexpr std::array<std::uint8_t, 8U> kFooterMagic{
    'A','R','C','O','M','M','I','T'};
constexpr std::size_t kCrcChunkBytes = 256U;

struct BankInfo final {
    bool valid{};
    bool io_error{};
    std::uint64_t generation{};
    std::uint32_t payload_bytes{};
    std::uint32_t payload_crc{};
};

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

[[nodiscard]] std::uint32_t crc32_update(
    std::uint32_t state,
    const std::span<const std::uint8_t> bytes) noexcept {
    for (const auto byte : bytes) {
        state ^= byte;
        for (std::uint8_t bit = 0U; bit < 8U; ++bit) {
            const auto mask = static_cast<std::uint32_t>(
                0U - static_cast<std::uint32_t>(state & 1U));
            state = (state >> 1U) ^ (0xEDB88320U & mask);
        }
    }
    return state;
}

[[nodiscard]] std::uint32_t crc32(
    const std::span<const std::uint8_t> bytes) noexcept {
    return ~crc32_update(0xFFFF'FFFFU, bytes);
}

[[nodiscard]] bool generation_newer(
    const std::uint64_t left,
    const std::uint64_t right) noexcept {
    if (left == right) {
        return false;
    }
    return static_cast<std::uint64_t>(left - right) < (std::uint64_t{1U} << 63U);
}

[[nodiscard]] std::uint64_t next_generation(const std::uint64_t current) noexcept {
    return current == 0U || current == std::numeric_limits<std::uint64_t>::max()
        ? 1U
        : current + 1U;
}

[[nodiscard]] std::size_t bank_offset(
    const std::size_t bank_index,
    const std::size_t bank_bytes) noexcept {
    return bank_index * bank_bytes;
}

[[nodiscard]] bool read_payload_crc(
    const MmsStaticBrcbStorageBackend& backend,
    const std::size_t payload_offset,
    const std::size_t payload_bytes,
    std::uint32_t& result) noexcept {
    std::array<std::uint8_t, kCrcChunkBytes> chunk{};
    std::size_t consumed = 0U;
    std::uint32_t state = 0xFFFF'FFFFU;
    while (consumed < payload_bytes) {
        const auto remaining = payload_bytes - consumed;
        const auto bytes = std::min(remaining, chunk.size());
        if (!backend.read(
                backend.context,
                payload_offset + consumed,
                std::span<std::uint8_t>{chunk}.first(bytes))) {
            return false;
        }
        state = crc32_update(
            state,
            std::span<const std::uint8_t>{chunk}.first(bytes));
        consumed += bytes;
    }
    result = ~state;
    return true;
}

[[nodiscard]] BankInfo inspect_bank(
    const MmsStaticBrcbStorageBackend& backend,
    const std::size_t bank_index,
    const std::size_t bank_bytes) noexcept {
    BankInfo info;
    std::array<std::uint8_t, MmsStaticBrcbCheckpointStore::record_header_bytes> header{};
    std::array<std::uint8_t, MmsStaticBrcbCheckpointStore::record_footer_bytes> footer{};
    const auto base = bank_offset(bank_index, bank_bytes);
    const auto footer_offset = base + bank_bytes - footer.size();
    if (!backend.read(backend.context, base, header) ||
        !backend.read(backend.context, footer_offset, footer)) {
        info.io_error = true;
        return info;
    }

    if (!std::equal(kHeaderMagic.begin(), kHeaderMagic.end(), header.begin()) ||
        read_u16(header, 8U) != MmsStaticBrcbCheckpointStore::format_version ||
        read_u16(header, 10U) != MmsStaticBrcbCheckpointStore::record_header_bytes ||
        crc32(std::span<const std::uint8_t>{header}.first(36U)) != read_u32(header, 36U) ||
        !std::equal(kFooterMagic.begin(), kFooterMagic.end(), footer.begin()) ||
        crc32(std::span<const std::uint8_t>{footer}.first(28U)) != read_u32(footer, 28U)) {
        return info;
    }
    for (std::size_t index = 28U; index < 36U; ++index) {
        if (header[index] != 0U) {
            return info;
        }
    }
    for (std::size_t index = 24U; index < 28U; ++index) {
        if (footer[index] != 0U) {
            return info;
        }
    }

    const auto generation = read_u64(header, 12U);
    const auto payload_bytes = read_u32(header, 20U);
    const auto payload_crc = read_u32(header, 24U);
    if (generation == 0U || payload_bytes == 0U ||
        generation != read_u64(footer, 8U) ||
        payload_bytes != read_u32(footer, 16U) ||
        payload_crc != read_u32(footer, 20U) ||
        static_cast<std::size_t>(payload_bytes) >
            bank_bytes - MmsStaticBrcbCheckpointStore::record_header_bytes -
                MmsStaticBrcbCheckpointStore::record_footer_bytes) {
        return info;
    }

    std::uint32_t measured_crc = 0U;
    if (!read_payload_crc(
            backend,
            base + MmsStaticBrcbCheckpointStore::record_header_bytes,
            payload_bytes,
            measured_crc)) {
        info.io_error = true;
        return info;
    }
    if (measured_crc != payload_crc) {
        return info;
    }

    info.valid = true;
    info.generation = generation;
    info.payload_bytes = payload_bytes;
    info.payload_crc = payload_crc;
    return info;
}

void build_header(
    const std::uint64_t generation,
    const std::uint32_t payload_bytes,
    const std::uint32_t payload_crc,
    std::span<std::uint8_t> header) noexcept {
    std::fill(header.begin(), header.end(), std::uint8_t{0U});
    std::copy(kHeaderMagic.begin(), kHeaderMagic.end(), header.begin());
    write_u16(header, 8U, MmsStaticBrcbCheckpointStore::format_version);
    write_u16(
        header,
        10U,
        static_cast<std::uint16_t>(MmsStaticBrcbCheckpointStore::record_header_bytes));
    write_u64(header, 12U, generation);
    write_u32(header, 20U, payload_bytes);
    write_u32(header, 24U, payload_crc);
    write_u32(header, 36U, crc32(std::span<const std::uint8_t>{header}.first(36U)));
}

void build_footer(
    const std::uint64_t generation,
    const std::uint32_t payload_bytes,
    const std::uint32_t payload_crc,
    std::span<std::uint8_t> footer) noexcept {
    std::fill(footer.begin(), footer.end(), std::uint8_t{0U});
    std::copy(kFooterMagic.begin(), kFooterMagic.end(), footer.begin());
    write_u64(footer, 8U, generation);
    write_u32(footer, 16U, payload_bytes);
    write_u32(footer, 20U, payload_crc);
    write_u32(footer, 28U, crc32(std::span<const std::uint8_t>{footer}.first(28U)));
}

} // namespace

bool MmsStaticBrcbCheckpointStore::valid() const noexcept {
    return backend_.valid() &&
        bank_bytes_ > record_header_bytes + record_footer_bytes &&
        bank_bytes_ <= backend_.capacity_bytes / bank_count;
}

MmsStaticBrcbCheckpointResult MmsStaticBrcbCheckpointStore::checkpoint(
    const MmsStaticBrcbRuntime& runtime,
    const std::span<std::uint8_t> state_buffer) const noexcept {
    MmsStaticBrcbCheckpointResult result;
    if (!valid()) {
        result.status = MmsStaticBrcbCheckpointStatus::invalid_backend;
        return result;
    }

    const auto state = MmsStaticBrcbStateCodec::encode(runtime, state_buffer);
    result.state_status = state.status;
    result.required_state_bytes = state.required_bytes;
    if (!state.success()) {
        result.status = state.status == MmsStaticBrcbStateStatus::buffer_too_small
            ? MmsStaticBrcbCheckpointStatus::state_buffer_too_small
            : MmsStaticBrcbCheckpointStatus::state_encode_failed;
        return result;
    }
    if (state.bytes_written > maximum_state_bytes() ||
        state.bytes_written > std::numeric_limits<std::uint32_t>::max()) {
        result.status = MmsStaticBrcbCheckpointStatus::invalid_layout;
        result.state_bytes = state.bytes_written;
        return result;
    }

    const auto first = inspect_bank(backend_, 0U, bank_bytes_);
    const auto second = inspect_bank(backend_, 1U, bank_bytes_);
    if (first.io_error || second.io_error) {
        result.status = MmsStaticBrcbCheckpointStatus::storage_failure;
        return result;
    }

    std::size_t target = 0U;
    std::uint64_t current_generation = 0U;
    if (first.valid && second.valid) {
        const auto newest = generation_newer(second.generation, first.generation) ? 1U : 0U;
        target = 1U - newest;
        current_generation = newest == 0U ? first.generation : second.generation;
    } else if (first.valid) {
        target = 1U;
        current_generation = first.generation;
    } else if (second.valid) {
        target = 0U;
        current_generation = second.generation;
    }
    const auto generation = next_generation(current_generation);
    const auto payload_bytes = static_cast<std::uint32_t>(state.bytes_written);
    const auto payload_crc = crc32(state_buffer.first(state.bytes_written));

    std::array<std::uint8_t, record_header_bytes> header{};
    std::array<std::uint8_t, record_footer_bytes> footer{};
    build_header(generation, payload_bytes, payload_crc, header);
    build_footer(generation, payload_bytes, payload_crc, footer);

    const auto base = bank_offset(target, bank_bytes_);
    const auto footer_offset = base + bank_bytes_ - footer.size();
    if (!backend_.erase(backend_.context, base, bank_bytes_) ||
        !backend_.sync(backend_.context) ||
        !backend_.write(backend_.context, base, header) ||
        !backend_.write(
            backend_.context,
            base + record_header_bytes,
            state_buffer.first(state.bytes_written)) ||
        !backend_.sync(backend_.context) ||
        !backend_.write(backend_.context, footer_offset, footer) ||
        !backend_.sync(backend_.context)) {
        result.status = MmsStaticBrcbCheckpointStatus::storage_failure;
        result.generation = generation;
        result.bank_index = target;
        result.state_bytes = state.bytes_written;
        return result;
    }

    result.status = MmsStaticBrcbCheckpointStatus::ok;
    result.generation = generation;
    result.bank_index = target;
    result.state_bytes = state.bytes_written;
    return result;
}

MmsStaticBrcbCheckpointResult MmsStaticBrcbCheckpointStore::restore(
    MmsStaticBrcbRuntime& runtime,
    const std::span<std::uint8_t> state_buffer) const noexcept {
    MmsStaticBrcbCheckpointResult result;
    if (!valid()) {
        result.status = MmsStaticBrcbCheckpointStatus::invalid_backend;
        return result;
    }

    const auto first = inspect_bank(backend_, 0U, bank_bytes_);
    const auto second = inspect_bank(backend_, 1U, bank_bytes_);
    if (first.io_error || second.io_error) {
        result.status = MmsStaticBrcbCheckpointStatus::storage_failure;
        return result;
    }
    if (!first.valid && !second.valid) {
        result.status = MmsStaticBrcbCheckpointStatus::no_checkpoint;
        return result;
    }

    std::size_t selected = 0U;
    BankInfo info = first;
    if (!first.valid || (second.valid && generation_newer(second.generation, first.generation))) {
        selected = 1U;
        info = second;
    }
    result.generation = info.generation;
    result.bank_index = selected;
    result.state_bytes = info.payload_bytes;
    result.required_state_bytes = info.payload_bytes;
    if (state_buffer.size() < info.payload_bytes) {
        result.status = MmsStaticBrcbCheckpointStatus::state_buffer_too_small;
        return result;
    }

    const auto base = bank_offset(selected, bank_bytes_);
    auto payload = state_buffer.first(info.payload_bytes);
    if (!backend_.read(
            backend_.context,
            base + record_header_bytes,
            payload) ||
        crc32(payload) != info.payload_crc) {
        result.status = MmsStaticBrcbCheckpointStatus::storage_failure;
        return result;
    }

    const auto restored = MmsStaticBrcbStateCodec::restore(runtime, payload);
    result.state_status = restored.status;
    if (!restored.success()) {
        result.status = MmsStaticBrcbCheckpointStatus::state_restore_failed;
        return result;
    }
    result.status = MmsStaticBrcbCheckpointStatus::ok;
    return result;
}

} // namespace ar::iec61850::mms
