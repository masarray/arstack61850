// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_brcb_control.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] constexpr std::uint64_t reservation_deadline(
    const std::uint64_t now_ms,
    const std::uint32_t seconds) noexcept {
    constexpr auto kMax = std::numeric_limits<std::uint64_t>::max();
    const auto seconds64 = static_cast<std::uint64_t>(seconds);
    if (seconds64 > kMax / 1'000U) {
        return kMax;
    }
    const auto duration = seconds64 * 1'000U;
    return duration > kMax - now_ms ? kMax : now_ms + duration;
}

} // namespace

bool MmsStaticBrcbControl::same_owner(
    const MmsStaticBrcbClientIdentity& client) const noexcept {
    return client.valid() && reserved_ && client.owner_size == owner_size_ &&
        std::equal(
            client.owner.begin(),
            client.owner.begin() + static_cast<std::ptrdiff_t>(client.owner_size),
            owner_.begin());
}

bool MmsStaticBrcbControl::owns_live_association(
    const MmsStaticBrcbClientIdentity& client) const noexcept {
    return same_owner(client) && owner_connected_ &&
        client.association_id == association_id_;
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::map_runtime_status(
    const MmsStaticBrcbStatus status) const noexcept {
    switch (status) {
    case MmsStaticBrcbStatus::ok:
        return MmsStaticBrcbControlStatus::ok;
    case MmsStaticBrcbStatus::entry_not_found:
        return MmsStaticBrcbControlStatus::entry_not_found;
    case MmsStaticBrcbStatus::temporarily_unavailable:
    case MmsStaticBrcbStatus::no_report_due:
    case MmsStaticBrcbStatus::stale_plan:
        return MmsStaticBrcbControlStatus::temporarily_unavailable;
    case MmsStaticBrcbStatus::invalid_runtime:
    case MmsStaticBrcbStatus::invalid_definition:
    case MmsStaticBrcbStatus::member_out_of_range:
    case MmsStaticBrcbStatus::trigger_not_selected:
    case MmsStaticBrcbStatus::data_set_not_found:
    case MmsStaticBrcbStatus::object_not_found:
    case MmsStaticBrcbStatus::workspace_too_small:
    case MmsStaticBrcbStatus::response_buffer_too_small:
    case MmsStaticBrcbStatus::slot_too_small:
    case MmsStaticBrcbStatus::backend_failure:
    case MmsStaticBrcbStatus::report_encode_failed:
        return MmsStaticBrcbControlStatus::backend_failure;
    }
    return MmsStaticBrcbControlStatus::backend_failure;
}

void MmsStaticBrcbControl::clear_reservation() noexcept {
    owner_.fill(0U);
    owner_size_ = 0U;
    association_id_ = 0U;
    reservation_expires_at_ms_ = 0U;
    resv_tms_seconds_ = 0U;
    reserved_ = false;
    owner_connected_ = false;
}

void MmsStaticBrcbControl::tick(const std::uint64_t now_ms) noexcept {
    if (!reserved_ || owner_connected_ || resv_tms_seconds_ == 0U) {
        return;
    }
    if (now_ms < reservation_expires_at_ms_) {
        return;
    }
    if (reports_ != nullptr && reports_->valid() && reports_->enabled()) {
        static_cast<void>(reports_->set_enabled(false));
    }
    clear_reservation();
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::reserve(
    const MmsStaticBrcbClientIdentity& client,
    const std::uint32_t resv_tms_seconds,
    const std::uint64_t now_ms) noexcept {
    if (!valid()) {
        return MmsStaticBrcbControlStatus::invalid_runtime;
    }
    if (!client.valid()) {
        return MmsStaticBrcbControlStatus::invalid_identity;
    }

    tick(now_ms);
    if (!reserved_) {
        std::copy_n(client.owner.begin(), client.owner_size, owner_.begin());
        owner_size_ = client.owner_size;
        association_id_ = client.association_id;
        resv_tms_seconds_ = resv_tms_seconds;
        reservation_expires_at_ms_ = 0U;
        reserved_ = true;
        owner_connected_ = true;
        return MmsStaticBrcbControlStatus::ok;
    }

    if (!same_owner(client)) {
        return MmsStaticBrcbControlStatus::object_access_denied;
    }
    if (owner_connected_ && association_id_ != client.association_id) {
        return MmsStaticBrcbControlStatus::object_access_denied;
    }

    // A disconnected stable Owner may reclaim its reservation before timeout.
    association_id_ = client.association_id;
    owner_connected_ = true;
    resv_tms_seconds_ = resv_tms_seconds;
    reservation_expires_at_ms_ = 0U;
    return MmsStaticBrcbControlStatus::ok;
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::require_control_access(
    const MmsStaticBrcbClientIdentity& client,
    const std::uint64_t now_ms) noexcept {
    if (!valid()) {
        return MmsStaticBrcbControlStatus::invalid_runtime;
    }
    if (!client.valid()) {
        return MmsStaticBrcbControlStatus::invalid_identity;
    }
    tick(now_ms);
    return owns_live_association(client)
        ? MmsStaticBrcbControlStatus::ok
        : MmsStaticBrcbControlStatus::object_access_denied;
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::release(
    const MmsStaticBrcbClientIdentity& client,
    const std::uint64_t now_ms) noexcept {
    const auto access = require_control_access(client, now_ms);
    if (access != MmsStaticBrcbControlStatus::ok) {
        return access;
    }
    if (reports_->enabled()) {
        return MmsStaticBrcbControlStatus::temporarily_unavailable;
    }
    clear_reservation();
    return MmsStaticBrcbControlStatus::ok;
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::set_report_enabled(
    const MmsStaticBrcbClientIdentity& client,
    const bool enabled,
    const std::uint64_t now_ms) noexcept {
    const auto access = require_control_access(client, now_ms);
    if (access != MmsStaticBrcbControlStatus::ok) {
        return access;
    }
    return map_runtime_status(reports_->set_enabled(enabled));
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::replay_from(
    const MmsStaticBrcbClientIdentity& client,
    const std::span<const std::uint8_t> entry_id,
    const std::uint64_t now_ms) noexcept {
    const auto access = require_control_access(client, now_ms);
    if (access != MmsStaticBrcbControlStatus::ok) {
        return access;
    }
    if (reports_->enabled()) {
        return MmsStaticBrcbControlStatus::temporarily_unavailable;
    }
    return map_runtime_status(reports_->replay_from(entry_id));
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::resume_after(
    const MmsStaticBrcbClientIdentity& client,
    const std::span<const std::uint8_t> entry_id,
    const std::uint64_t now_ms) noexcept {
    const auto access = require_control_access(client, now_ms);
    if (access != MmsStaticBrcbControlStatus::ok) {
        return access;
    }
    if (reports_->enabled()) {
        return MmsStaticBrcbControlStatus::temporarily_unavailable;
    }
    return map_runtime_status(reports_->resume_after(entry_id));
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::rewind_to_oldest(
    const MmsStaticBrcbClientIdentity& client,
    const std::uint64_t now_ms) noexcept {
    const auto access = require_control_access(client, now_ms);
    if (access != MmsStaticBrcbControlStatus::ok) {
        return access;
    }
    if (reports_->enabled()) {
        return MmsStaticBrcbControlStatus::temporarily_unavailable;
    }
    return map_runtime_status(reports_->rewind_to_oldest());
}

MmsStaticBrcbControlStatus MmsStaticBrcbControl::purge_buffer(
    const MmsStaticBrcbClientIdentity& client,
    const std::uint64_t now_ms) noexcept {
    const auto access = require_control_access(client, now_ms);
    if (access != MmsStaticBrcbControlStatus::ok) {
        return access;
    }
    if (reports_->enabled()) {
        return MmsStaticBrcbControlStatus::temporarily_unavailable;
    }
    return map_runtime_status(reports_->purge_buffer());
}

void MmsStaticBrcbControl::on_association_closed(
    const std::uint64_t association_id,
    const std::uint64_t now_ms) noexcept {
    tick(now_ms);
    if (!reserved_ || !owner_connected_ || association_id == 0U ||
        association_id != association_id_) {
        return;
    }

    if (reports_ != nullptr && reports_->valid() && reports_->enabled()) {
        static_cast<void>(reports_->set_enabled(false));
    }
    association_id_ = 0U;
    owner_connected_ = false;
    if (resv_tms_seconds_ == 0U) {
        clear_reservation();
        return;
    }
    reservation_expires_at_ms_ = reservation_deadline(now_ms, resv_tms_seconds_);
}

MmsStaticBrcbControlStateView MmsStaticBrcbControl::state(
    const std::uint64_t now_ms) noexcept {
    tick(now_ms);
    return {
        reserved_,
        owner_connected_,
        reports_ != nullptr && reports_->valid() && reports_->enabled(),
        resv_tms_seconds_,
        association_id_,
        reservation_expires_at_ms_,
        {owner_.data(), owner_size_}};
}

} // namespace ar::iec61850::mms
