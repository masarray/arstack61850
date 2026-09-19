// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/report_subscription_runtime.hpp"

#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace ar::iec61850::mms {
namespace {

constexpr std::uint8_t kTriggerOptionsUnusedBits = 2U;
constexpr std::uint8_t kOptionalFieldsUnusedBits = 6U;
constexpr std::size_t kTriggerOptionsBytes = 1U;
constexpr std::size_t kOptionalFieldsBytes = 2U;

[[nodiscard]] bool contains_attribute(
    const MmsReportControlCandidate& candidate,
    const std::string& attribute) {
    return std::any_of(
        candidate.attributes.begin(), candidate.attributes.end(),
        [&attribute](const std::string& item) {
            return item == attribute;
        });
}

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const MmsConfirmedExchangeResult& exchange) {
    if (!exchange.presentation_payload.empty()) {
        return exchange.presentation_payload;
    }
    return exchange.envelope.mms_payload;
}

[[nodiscard]] bool valid_bit_payload(
    const std::span<const std::uint8_t> payload,
    const std::size_t expected_bytes,
    const std::uint8_t unused_bits) noexcept {
    if (payload.size() != expected_bytes || payload.empty() || unused_bits > 7U) {
        return false;
    }
    if (unused_bits == 0U) {
        return true;
    }
    const auto mask = static_cast<std::uint8_t>((1U << unused_bits) - 1U);
    return (payload.back() & mask) == 0U;
}

[[nodiscard]] bool bit_field_matches(
    const MmsReportBitField& live,
    const std::span<const std::uint8_t> desired,
    const std::uint8_t unused_bits) noexcept {
    if (live.raw.size() != desired.size() + 1U || live.raw.empty() ||
        live.raw.front() != unused_bits) {
        return false;
    }
    return std::equal(desired.begin(), desired.end(), live.raw.begin() + 1);
}

} // namespace

MmsReportSubscriptionRuntime::MmsReportSubscriptionRuntime(
    MmsAssociationRuntime& association,
    MmsReportControlCandidate candidate,
    MmsDataSetDirectoryResponse directory,
    MmsReportSubscriptionOptions options)
    : association_{association},
      candidate_{std::move(candidate)},
      directory_{std::move(directory)},
      options_{std::move(options)},
      monitor_{options_.monitor_options} {
    if (candidate_.domain.empty() || candidate_.logical_node.empty() ||
        candidate_.name.empty()) {
        throw std::invalid_argument(
            "Report subscription requires a complete RCB candidate identity.");
    }
    if (options_.maximum_events == 0U) {
        throw std::invalid_argument(
            "Report subscription event limit must be positive.");
    }
    if (options_.write_trigger_options &&
        !valid_bit_payload(
            options_.trigger_options,
            kTriggerOptionsBytes,
            kTriggerOptionsUnusedBits)) {
        throw std::invalid_argument(
            "TrgOps must contain exactly six IEC 61850 bits in one octet; "
            "the two unused low bits must be zero.");
    }
    if (options_.write_optional_fields &&
        !valid_bit_payload(
            options_.optional_fields,
            kOptionalFieldsBytes,
            kOptionalFieldsUnusedBits)) {
        throw std::invalid_argument(
            "OptFlds must contain exactly ten IEC 61850 bits in two octets; "
            "the six unused low bits must be zero.");
    }
}

std::vector<std::string> MmsReportSubscriptionRuntime::probe_attributes() const {
    static const std::array<const char*, 15> preferred{
        "RptID", "RptEna", "DatSet", "ConfRev", "OptFlds",
        "BufTm", "SqNum", "TrgOps", "IntgPd", "GI",
        "Resv", "ResvTms", "Owner", "EntryID", "TimeOfEntry"};
    std::vector<std::string> attributes;
    for (const auto* name : preferred) {
        if (contains_attribute(candidate_, name)) {
            attributes.emplace_back(name);
        }
    }
    if (attributes.empty()) {
        throw MmsReportSubscriptionError(
            "RCB candidate has no readable attributes for a live-state probe.");
    }
    return attributes;
}

MmsReportControlState MmsReportSubscriptionRuntime::probe(
    const std::stop_token stop_token) {
    const auto attributes = probe_attributes();
    const auto invoke_id = association_.next_invoke_id();
    const auto request = MmsReportControlStateMapper::build_read_request(
        invoke_id, candidate_, attributes);
    const auto encoded = MmsServiceCodec::encode_read_request_p_data(
        request, association_.negotiated().presentation_context_id);
    const auto exchange = association_.exchange_confirmed(
        encoded, invoke_id, stop_token);
    if (exchange.envelope.kind == MmsPduKind::confirmed_error) {
        throw MmsReportSubscriptionError(
            "RCB live-state probe returned a confirmed MMS error.");
    }
    const auto response = MmsServiceCodec::decode_read_response(
        response_payload(exchange), invoke_id);
    auto mapped = MmsReportControlStateMapper::map_read_response(
        candidate_, attributes, response, &directory_, false);
    last_rcb_state_ = mapped;
    add_event(MmsReportSubscriptionEventKind::probe_completed,
              "RCB live-state probe completed: " + mapped.availability_reason);
    return mapped;
}

void MmsReportSubscriptionRuntime::write_attribute(
    const std::string& attribute,
    MmsDataValue value,
    const std::stop_token stop_token) {
    if (!contains_attribute(candidate_, attribute)) {
        throw MmsReportSubscriptionError(
            "RCB does not expose writable attribute " + attribute + ".");
    }
    const auto invoke_id = association_.next_invoke_id();
    MmsWriteRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(candidate_.attribute_object_name(attribute));
    request.values.push_back(std::move(value));
    const auto encoded = MmsServiceCodec::encode_write_request_p_data(
        request, association_.negotiated().presentation_context_id);
    const auto exchange = association_.exchange_confirmed(
        encoded, invoke_id, stop_token);
    if (exchange.envelope.kind == MmsPduKind::confirmed_error) {
        throw MmsReportSubscriptionError(
            "RCB attribute write returned a confirmed MMS error: " + attribute + ".");
    }
    const auto response = MmsServiceCodec::decode_write_response(
        response_payload(exchange), invoke_id);
    if (response.results.size() != 1U || !response.results.front().success) {
        const auto failure = response.results.size() == 1U
            ? response.results.front().failure_code
            : std::nullopt;
        throw MmsReportSubscriptionError(
            "RCB attribute write failed: " + attribute +
            (failure ? " (DataAccessError=" + std::to_string(*failure) + ")."
                     : "."));
    }
    add_event(MmsReportSubscriptionEventKind::attribute_written,
              "RCB attribute written: " + attribute + ".");
}

bool MmsReportSubscriptionRuntime::try_write_attribute_noexcept(
    const std::string& attribute,
    MmsDataValue value,
    const std::stop_token stop_token) noexcept {
    try {
        write_attribute(attribute, std::move(value), stop_token);
        return true;
    } catch (const std::exception& exception) {
        add_event(MmsReportSubscriptionEventKind::cleanup_deferred,
                  "RCB cleanup write failed for " + attribute + ": " +
                      exception.what());
        return false;
    } catch (...) {
        add_event(MmsReportSubscriptionEventKind::cleanup_deferred,
                  "RCB cleanup write failed unexpectedly for " + attribute + ".");
        return false;
    }
}

void MmsReportSubscriptionRuntime::start(
    const std::stop_token stop_token) {
    if (!association_.associated()) {
        throw MmsReportSubscriptionError(
            "Cannot start report subscription without an active MMS association.");
    }
    if (active()) {
        throw MmsReportSubscriptionError("Report subscription is already active.");
    }

    enabled_by_runtime_ = false;
    reservation_touched_ = false;
    restore_enabled_required_ = false;
    cleanup_required_ = false;
    monitor_.clear();
    received_reports_ = 0U;
    decode_failures_ = 0U;

    try {
        set_state(MmsReportSubscriptionState::probing,
                  "Probing RCB state before any write.");
        const auto current = probe(stop_token);
        const auto was_enabled = current.report_enabled.value_or(false);
        const auto caller_owned_enabled =
            was_enabled && options_.allow_reenable_caller_owned;

        if (was_enabled && !caller_owned_enabled) {
            throw MmsReportSubscriptionError(
                "RCB is already enabled; explicit caller-owned re-enable permission is required.");
        }
        if (current.availability == MmsRcbAvailability::in_use &&
            !caller_owned_enabled) {
            throw MmsReportSubscriptionError(
                "RCB is reserved or enabled by another session.");
        }

        std::string desired_data_set;
        if (options_.write_data_set_reference) {
            if (options_.data_set_reference.empty()) {
                throw MmsReportSubscriptionError(
                    "DataSet write was requested without a DataSet reference.");
            }
            desired_data_set = MmsDataSetDirectoryCodec::to_report_attribute_value(
                options_.data_set_reference);
        }

        const auto data_set_changed = options_.write_data_set_reference &&
            current.data_set_reference != desired_data_set;
        const auto trigger_options_changed = options_.write_trigger_options &&
            !bit_field_matches(
                current.trigger_options,
                options_.trigger_options,
                kTriggerOptionsUnusedBits);
        const auto optional_fields_changed = options_.write_optional_fields &&
            !bit_field_matches(
                current.optional_fields,
                options_.optional_fields,
                kOptionalFieldsUnusedBits);
        const auto configuration_changed =
            data_set_changed || trigger_options_changed || optional_fields_changed;

        if (was_enabled && configuration_changed) {
            set_state(MmsReportSubscriptionState::configuring,
                      "Disabling caller-owned RCB before configuration delta write.");
            write_attribute("RptEna", MmsDataValue::boolean(false), stop_token);
            restore_enabled_required_ = true;
            add_event(MmsReportSubscriptionEventKind::disabled,
                      "Caller-owned RCB disabled for verified reconfiguration.");
        }

        if (!was_enabled && !candidate_.buffered &&
            options_.reserve_unbuffered_rcb &&
            contains_attribute(candidate_, "Resv")) {
            set_state(MmsReportSubscriptionState::reserving,
                      "Reserving unbuffered RCB.");
            write_attribute("Resv", MmsDataValue::boolean(true), stop_token);
            reservation_touched_ = true;
            add_event(MmsReportSubscriptionEventKind::reservation_acquired,
                      "URCB reservation acquired.");
        }

        set_state(MmsReportSubscriptionState::configuring,
                  configuration_changed
                      ? "Applying only changed RCB configuration attributes."
                      : "Live RCB configuration already matches requested values; no configuration write required.");
        if (data_set_changed) {
            write_attribute(
                "DatSet",
                MmsDataValue::visible_string(desired_data_set),
                stop_token);
        }
        if (trigger_options_changed) {
            write_attribute(
                "TrgOps",
                MmsDataValue::bit_string(
                    kTriggerOptionsUnusedBits,
                    options_.trigger_options),
                stop_token);
        }
        if (optional_fields_changed) {
            write_attribute(
                "OptFlds",
                MmsDataValue::bit_string(
                    kOptionalFieldsUnusedBits,
                    options_.optional_fields),
                stop_token);
        }

        if (configuration_changed || reservation_touched_) {
            set_state(MmsReportSubscriptionState::probing,
                      "Verifying RCB state after configuration/reservation writes.");
            const auto verified = probe(stop_token);
            if (data_set_changed && verified.data_set_reference != desired_data_set) {
                throw MmsReportSubscriptionError(
                    "RCB DataSet read-back verification failed.");
            }
            if (trigger_options_changed &&
                !bit_field_matches(
                    verified.trigger_options,
                    options_.trigger_options,
                    kTriggerOptionsUnusedBits)) {
                throw MmsReportSubscriptionError(
                    "RCB TrgOps read-back verification failed.");
            }
            if (optional_fields_changed &&
                !bit_field_matches(
                    verified.optional_fields,
                    options_.optional_fields,
                    kOptionalFieldsUnusedBits)) {
                throw MmsReportSubscriptionError(
                    "RCB OptFlds read-back verification failed.");
            }
            if (reservation_touched_ &&
                contains_attribute(candidate_, "Resv") &&
                !verified.reserved.value_or(false)) {
                throw MmsReportSubscriptionError(
                    "URCB reservation read-back verification failed.");
            }
        }

        if (!was_enabled || configuration_changed) {
            set_state(MmsReportSubscriptionState::enabling,
                      "Enabling RCB only after configuration read-back verification.");
            write_attribute("RptEna", MmsDataValue::boolean(true), stop_token);
            if (was_enabled) {
                restore_enabled_required_ = true;
            } else {
                enabled_by_runtime_ = true;
            }

            set_state(MmsReportSubscriptionState::probing,
                      "Verifying RptEna after enable write.");
            const auto enabled_state = probe(stop_token);
            if (!enabled_state.report_enabled.value_or(false)) {
                throw MmsReportSubscriptionError(
                    "RCB enable read-back verification failed.");
            }
            if (was_enabled) {
                restore_enabled_required_ = false;
            }
            add_event(MmsReportSubscriptionEventKind::enabled,
                      was_enabled
                          ? "Caller-owned RCB re-enabled after verified reconfiguration."
                          : "RCB enabled by this runtime after verified configuration.");
        } else {
            add_event(MmsReportSubscriptionEventKind::enabled,
                      "Caller-owned RCB was already enabled; redundant RptEna write skipped.");
        }

        if (options_.trigger_general_interrogation &&
            contains_attribute(candidate_, "GI")) {
            try {
                write_attribute("GI", MmsDataValue::boolean(true), stop_token);
                add_event(MmsReportSubscriptionEventKind::general_interrogation_sent,
                          "General interrogation requested after verified RCB enable.");
            } catch (const std::exception& exception) {
                add_event(MmsReportSubscriptionEventKind::cleanup_deferred,
                          "RCB remains enabled, but GI write failed: " +
                              std::string{exception.what()});
            }
        }

        set_state(MmsReportSubscriptionState::active,
                  "Persistent report subscription is active.");
    } catch (const std::exception& exception) {
        if (association_.associated()) {
            if (restore_enabled_required_) {
                const auto restored = try_write_attribute_noexcept(
                    "RptEna", MmsDataValue::boolean(true), stop_token);
                if (restored) {
                    restore_enabled_required_ = false;
                    add_event(MmsReportSubscriptionEventKind::enabled,
                              "Caller-owned RCB enable state restored after failed reconfiguration.");
                }
            } else if (enabled_by_runtime_) {
                const auto disabled = try_write_attribute_noexcept(
                    "RptEna", MmsDataValue::boolean(false), stop_token);
                enabled_by_runtime_ = !disabled;
            }
            if (reservation_touched_) {
                const auto released = try_write_attribute_noexcept(
                    "Resv", MmsDataValue::boolean(false), stop_token);
                reservation_touched_ = !released;
            }
        }
        cleanup_required_ =
            enabled_by_runtime_ || reservation_touched_ || restore_enabled_required_;
        fail(exception.what());
        throw;
    }
}

void MmsReportSubscriptionRuntime::stop(
    const std::stop_token stop_token) noexcept {
    if (state_ == MmsReportSubscriptionState::idle ||
        state_ == MmsReportSubscriptionState::stopped) {
        return;
    }
    set_state(MmsReportSubscriptionState::stopping,
              "Stopping persistent report subscription.");

    if (!association_.associated()) {
        cleanup_required_ =
            enabled_by_runtime_ || reservation_touched_ || restore_enabled_required_;
        if (cleanup_required_) {
            state_ = MmsReportSubscriptionState::cleanup_required;
            add_event(MmsReportSubscriptionEventKind::cleanup_deferred,
                      "Association is unavailable; RCB cleanup must be retried after reconnect.");
        } else {
            set_state(MmsReportSubscriptionState::stopped,
                      "Report subscription stopped without pending cleanup.");
        }
        return;
    }

    bool cleanup_ok = true;
    if (restore_enabled_required_) {
        if (try_write_attribute_noexcept(
                "RptEna", MmsDataValue::boolean(true), stop_token)) {
            restore_enabled_required_ = false;
            add_event(MmsReportSubscriptionEventKind::enabled,
                      "Caller-owned RCB enable state restored.");
        } else {
            cleanup_ok = false;
        }
    }
    if (enabled_by_runtime_) {
        if (try_write_attribute_noexcept(
                "RptEna", MmsDataValue::boolean(false), stop_token)) {
            enabled_by_runtime_ = false;
            add_event(MmsReportSubscriptionEventKind::disabled,
                      "RCB disabled by this runtime.");
        } else {
            cleanup_ok = false;
        }
    }
    if (reservation_touched_) {
        if (try_write_attribute_noexcept(
                "Resv", MmsDataValue::boolean(false), stop_token)) {
            reservation_touched_ = false;
            add_event(MmsReportSubscriptionEventKind::reservation_released,
                      "URCB reservation released.");
        } else {
            cleanup_ok = false;
        }
    }

    cleanup_required_ = !cleanup_ok || enabled_by_runtime_ ||
        reservation_touched_ || restore_enabled_required_;
    if (cleanup_required_) {
        state_ = MmsReportSubscriptionState::cleanup_required;
        add_event(MmsReportSubscriptionEventKind::cleanup_deferred,
                  "One or more RCB cleanup writes remain pending.");
    } else {
        set_state(MmsReportSubscriptionState::stopped,
                  "Report subscription stopped and RCB cleanup completed.");
    }
}

bool MmsReportSubscriptionRuntime::poll_once(
    const std::stop_token stop_token) {
    if (!active()) {
        throw MmsReportSubscriptionError(
            "Report polling requires an active subscription.");
    }
    const auto envelope = association_.poll_once(stop_token);
    const auto ingested = drain_queued_reports();
    return envelope.information_report || ingested != 0U;
}

void MmsReportSubscriptionRuntime::run(const std::stop_token stop_token) {
    if (!active()) {
        throw MmsReportSubscriptionError(
            "Persistent report loop requires an active subscription.");
    }
    while (!stop_token.stop_requested() && active()) {
        static_cast<void>(poll_once(stop_token));
    }
}

bool MmsReportSubscriptionRuntime::retry_cleanup(
    const std::stop_token stop_token) noexcept {
    if (!cleanup_required_) {
        return true;
    }
    if (!association_.associated()) {
        return false;
    }
    stop(stop_token);
    return !cleanup_required_;
}

std::size_t MmsReportSubscriptionRuntime::drain_queued_reports() {
    std::size_t count = 0U;
    std::vector<std::uint8_t> payload;
    while (association_.try_pop_information_report(payload)) {
        ingest_report(payload);
        ++count;
    }
    return count;
}

void MmsReportSubscriptionRuntime::ingest_report(
    const std::span<const std::uint8_t> payload) {
    try {
        const auto report = MmsInformationReportCodec::decode(payload);
        const auto frame = MmsReportFrameMapper::map(report, directory_.members);
        static_cast<void>(monitor_.ingest(frame));
        ++received_reports_;
        add_event(MmsReportSubscriptionEventKind::report_received,
                  "InformationReport mapped to " + frame.routing_key() + ".");
    } catch (const std::exception& exception) {
        ++decode_failures_;
        add_event(MmsReportSubscriptionEventKind::report_decode_failed,
                  "InformationReport decode failed: " +
                      std::string{exception.what()});
    }
}

void MmsReportSubscriptionRuntime::set_state(
    const MmsReportSubscriptionState state,
    std::string message) {
    state_ = state;
    add_event(MmsReportSubscriptionEventKind::state_changed, std::move(message));
}

void MmsReportSubscriptionRuntime::add_event(
    const MmsReportSubscriptionEventKind kind,
    std::string message) {
    if (events_.size() >= options_.maximum_events) {
        events_.erase(events_.begin());
    }
    events_.push_back(MmsReportSubscriptionEvent{kind, state_, std::move(message)});
}

void MmsReportSubscriptionRuntime::fail(std::string message) {
    state_ = MmsReportSubscriptionState::faulted;
    add_event(MmsReportSubscriptionEventKind::faulted, std::move(message));
}

MmsReportSubscriptionSnapshot MmsReportSubscriptionRuntime::snapshot() const {
    MmsReportSubscriptionSnapshot result;
    result.state = state_;
    result.enabled_by_runtime = enabled_by_runtime_;
    result.reservation_touched = reservation_touched_;
    result.cleanup_required = cleanup_required_;
    result.received_reports = received_reports_;
    result.decode_failures = decode_failures_;
    result.last_rcb_state = last_rcb_state_;
    result.streams = monitor_.snapshots();
    result.events = events_;
    return result;
}

} // namespace ar::iec61850::mms
