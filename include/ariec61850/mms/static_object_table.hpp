// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/services_span.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

using MmsStaticReadCallback = wire::EncodeResult (*)(
    const void* context,
    std::span<std::uint8_t> destination) noexcept;

struct MmsStaticWriteResult final {
    bool success{};
    std::uint32_t failure_code{};
};

using MmsStaticWriteCallback = MmsStaticWriteResult (*)(
    void* context,
    std::span<const std::uint8_t> encoded_data) noexcept;

// Per-request identity propagated by a connection/runtime adapter. The core
// deliberately treats Owner as opaque binary identity and does not bind it to
// an IP address, socket, TLS implementation, or operating-system handle.
struct MmsStaticRequestAccessContext final {
    std::uint64_t association_id{};
    std::span<const std::uint8_t> owner{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return association_id != 0U && !owner.empty();
    }
};

using MmsStaticContextualWriteCallback = MmsStaticWriteResult (*)(
    void* context,
    std::span<const std::uint8_t> encoded_data,
    const MmsStaticRequestAccessContext& access) noexcept;

struct MmsStaticObjectEntry final {
    std::string_view domain;
    std::string_view item;
    std::span<const std::uint8_t> type_specification;
    MmsStaticReadCallback read{};
    const void* context{};
    bool mms_deletable{};
    MmsStaticWriteCallback write{};
    void* write_context{};

    // Appended for source compatibility with existing aggregate initializers.
    // A contextual callback takes precedence over the legacy write callback.
    MmsStaticContextualWriteCallback contextual_write{};

    [[nodiscard]] constexpr bool writable() const noexcept {
        return write != nullptr || contextual_write != nullptr;
    }
};

class MmsStaticObjectTable final {
public:
    static constexpr std::size_t maximum_objects = 128U;

    explicit constexpr MmsStaticObjectTable(
        const std::span<const MmsStaticObjectEntry> objects) noexcept
        : objects_{objects} {}

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] const MmsStaticObjectEntry* find(
        const MmsObjectNameView& name) const noexcept;

    [[nodiscard]] bool try_resolve_read_request(
        const MmsReadRequestView& request,
        std::span<const MmsStaticObjectEntry*> resolved,
        std::size_t& resolved_count) const noexcept;

    [[nodiscard]] bool try_resolve_write_request(
        const MmsWriteRequestView& request,
        std::span<const MmsStaticObjectEntry*> resolved,
        std::size_t& resolved_count) const noexcept;

    [[nodiscard]] constexpr std::span<const MmsStaticObjectEntry> objects() const noexcept {
        return objects_;
    }

private:
    std::span<const MmsStaticObjectEntry> objects_;
};

} // namespace ar::iec61850::mms
