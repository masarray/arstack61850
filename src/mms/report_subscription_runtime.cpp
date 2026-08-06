// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/report_subscription_runtime.hpp"

#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <array>
#include <exception>
#include <utility>

namespace ar::iec61850::mms {
namespace {

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
        throw MmsReportSubscriptionError(
            "RCB attribute write failed: " + attribute + ".");
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
    cleanup_required_ = false;
    monitor_.clear();
    received_reports_ = 0U;
    decode_failures_ = 0U;

    try {
        set_state(MmsReportSubscriptionState::probing,
                  "Probing RCB state before any write.");
        const auto current = probe(stop_token);
        const auto enabled_elsewhere = current.report_enabled.value_or(false) &&
            current.availability != MmsRcbAvailability::used_by_caller;
        if (enabled_elsewhere) {
            throw MmsReportSubscriptionError(
                "RCB is already enabled and is treated as owned by another session.");
        }
        if (current.availability == MmsRcbAvailability::in_use) {
            throw MmsReportSubscriptionError(
                "RCB is reserved or enabled by another session.");
        }
        if (current.report_enabled.value_or(false) &&
            !options_.allow_reenable_caller_owned) {
            throw MmsReportSubscriptionError(
                "RCB is already enabled; explicit caller-owned re-enable permission is required.");
        }

        if (!candidate_.buffered && options_.reserve_unbuffered_rcb &&
            contains_attribute(candidate_, "Resv")) {
            set_state(MmsReportSubscriptionState::reserving,
                      "Reserving unbuffered RCB.");
            write_attribute("Resv", MmsDataValue::boolean(true), stop_token);
            reservation_touched_ = true;
            add_event(MmsReportSubscriptionEventKind::reservation_acquired,
                      "URCB reservation acquired.");
        }

        set_state(MmsReportSubscriptionState::configuring,
                  "Applying requested RCB configuration.");
        if (options_.write_data_set_reference) {
            if (options_.data_set_reference.empty()) {
                throw MmsReportSubscriptionError(
                    "DataSet write was requested without a DataSet reference.");
            }
            write_attribute(
                "DatSet",
                MmsDataValue::visible_string(options_.data_set_reference),
                stop_token);
        }
        if (options_.write_trigger_options) {
            write_attribute(
                "TrgOps",
                MmsDataValue::bit_string(0U, options_.trigger_options),
                stop_token);
        }
        if (options_.write_optional_fields) {
            write_attribute(
                "OptFlds",
                MmsDataValue::bit_string(0U, options_.optional_fields),
                stop_token);
        }

        set_state(MmsReportSubscriptionState::enabling,
                  "Enabling RCB with RptEna=true.");
        write_attribute("RptEna", MmsDataValue::boolean(true), stop_token);
        enabled_by_runtime_ = true;
        add_event(MmsReportSubscriptionEventKind::enabled,
                  "RCB enabled by this runtime.");

        if (options_.trigger_general_interrogation &&
            contains_attribute(candidate_, "GI")) {
            try {
                write_attribute("GI", MmsDataValue::boolean(true), stop_token);
                add_event(MmsReportSubscriptionEventKind::general_interrogation_sent,
                          "General interrogation requested after RCB enable.");
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
            if (enabled_by_runtime_) {
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
        cleanup_required_ = enabled_by_runtime_ || reservation_touched_;
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
        cleanup_required_ = enabled_by_runtime_ || reservation_touched_;
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

    cleanup_required_ = !cleanup_ok || enabled_by_runtime_ || reservation_touched_;
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
