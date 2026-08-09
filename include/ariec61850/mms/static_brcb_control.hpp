// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/static_brcb_runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace ar::iec61850::mms {

enum class MmsStaticBrcbControlStatus : std::uint8_t {
    ok,
    invalid_runtime,
    invalid_identity,
    object_access_denied,
    temporarily_unavailable,
    entry_not_found,
    backend_failure,
};

// Association identity is deliberately separate from the stable Owner identity.
// `association_id` is unique for one live MMS association. `owner` is a stable,
// caller-supplied binary identity that may be reused by a reconnecting client.
// No IP/socket dependency is embedded in the portable core.
struct MmsStaticBrcbClientIdentity final {
    static constexpr std::size_t maximum_owner_bytes = 16U;

    std::uint64_t association_id{};
    std::array<std::uint8_t, maximum_owner_bytes> owner{};
    std::size_t owner_size{};

    [[nodiscard]] constexpr bool valid() const noexcept {
        return association_id != 0U && owner_size > 0U && owner_size <= owner.size();
    }

    [[nodiscard]] constexpr std::span<const std::uint8_t> owner_view() const noexcept {
        return {owner.data(), owner_size <= owner.size() ? owner_size : 0U};
    }
};

struct MmsStaticBrcbControlStateView final {
    bool reserved{};
    bool owner_connected{};
    bool report_enabled{};
    std::uint32_t resv_tms_seconds{};
    std::uint64_t association_id{};
    std::uint64_t reservation_expires_at_ms{};
    std::span<const std::uint8_t> owner{};
};

// Portable owner/reservation gate for one BRCB runtime. The runtime itself owns
// buffered report data; this class owns association-sensitive control state.
// It is allocation-free, exception-free and does not depend on TCP/IP types.
class MmsStaticBrcbControl final {
public:
    explicit constexpr MmsStaticBrcbControl(MmsStaticBrcbRuntime& reports) noexcept
        : reports_{&reports} {}

    [[nodiscard]] constexpr bool valid() const noexcept {
        return reports_ != nullptr && reports_->valid();
    }

    [[nodiscard]] MmsStaticBrcbControlStatus reserve(
        const MmsStaticBrcbClientIdentity& client,
        std::uint32_t resv_tms_seconds,
        std::uint64_t now_ms) noexcept;

    [[nodiscard]] MmsStaticBrcbControlStatus release(
        const MmsStaticBrcbClientIdentity& client,
        std::uint64_t now_ms) noexcept;

    [[nodiscard]] MmsStaticBrcbControlStatus set_report_enabled(
        const MmsStaticBrcbClientIdentity& client,
        bool enabled,
        std::uint64_t now_ms) noexcept;

    [[nodiscard]] MmsStaticBrcbControlStatus replay_from(
        const MmsStaticBrcbClientIdentity& client,
        std::span<const std::uint8_t> entry_id,
        std::uint64_t now_ms) noexcept;

    [[nodiscard]] MmsStaticBrcbControlStatus resume_after(
        const MmsStaticBrcbClientIdentity& client,
        std::span<const std::uint8_t> entry_id,
        std::uint64_t now_ms) noexcept;

    [[nodiscard]] MmsStaticBrcbControlStatus rewind_to_oldest(
        const MmsStaticBrcbClientIdentity& client,
        std::uint64_t now_ms) noexcept;

    [[nodiscard]] MmsStaticBrcbControlStatus purge_buffer(
        const MmsStaticBrcbClientIdentity& client,
        std::uint64_t now_ms) noexcept;

    // Called when a live MMS association is lost/closed. RptEna is always
    // disabled. ResvTms==0 releases immediately; non-zero retains the stable
    // Owner identity until expiry and permits that Owner to reconnect early.
    void on_association_closed(
        std::uint64_t association_id,
        std::uint64_t now_ms) noexcept;

    // Expires a disconnected retained reservation once ResvTms elapses.
    void tick(std::uint64_t now_ms) noexcept;

    [[nodiscard]] MmsStaticBrcbControlStateView state(
        std::uint64_t now_ms) noexcept;

private:
    [[nodiscard]] bool same_owner(
        const MmsStaticBrcbClientIdentity& client) const noexcept;
    [[nodiscard]] bool owns_live_association(
        const MmsStaticBrcbClientIdentity& client) const noexcept;
    [[nodiscard]] MmsStaticBrcbControlStatus require_control_access(
        const MmsStaticBrcbClientIdentity& client,
        std::uint64_t now_ms) noexcept;
    [[nodiscard]] MmsStaticBrcbControlStatus map_runtime_status(
        MmsStaticBrcbStatus status) const noexcept;
    void clear_reservation() noexcept;

    MmsStaticBrcbRuntime* reports_{};
    std::array<std::uint8_t, MmsStaticBrcbClientIdentity::maximum_owner_bytes> owner_{};
    std::size_t owner_size_{};
    std::uint64_t association_id_{};
    std::uint64_t reservation_expires_at_ms_{};
    std::uint32_t resv_tms_seconds_{};
    bool reserved_{};
    bool owner_connected_{};
};

} // namespace ar::iec61850::mms
