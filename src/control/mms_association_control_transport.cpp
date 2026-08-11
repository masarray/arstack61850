// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"

#include "ariec61850/mms/pdu.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace ar::iec61850::control {
namespace {

std::atomic<std::uint64_t> g_next_control_association_id{1U};

[[nodiscard]] std::span<const std::uint8_t> response_payload(
    const mms::MmsConfirmedExchangeResult& exchange) noexcept {
    return exchange.presentation_payload.empty()
        ? std::span<const std::uint8_t>{exchange.envelope.mms_payload}
        : std::span<const std::uint8_t>{exchange.presentation_payload};
}

[[nodiscard]] std::uint64_t allocate_association_id() noexcept {
    auto value = g_next_control_association_id.fetch_add(1U, std::memory_order_relaxed);
    if (value == 0U) {
        value = g_next_control_association_id.fetch_add(1U, std::memory_order_relaxed);
    }
    return value;
}

} // namespace

MmsAssociationControlTransport::MmsAssociationControlTransport(
    mms::MmsAssociationRuntime& association) noexcept
    : association_{association}, association_id_{allocate_association_id()} {}

bool MmsAssociationControlTransport::associated() const noexcept {
    return association_.associated();
}

std::uint64_t MmsAssociationControlTransport::association_id() const noexcept {
    return association_id_;
}

std::optional<mms::MmsDataValue> MmsAssociationControlTransport::read(
    const mms::MmsObjectName& object,
    const std::stop_token stop_token) {
    if (!association_.associated()) {
        return std::nullopt;
    }
    const auto invoke_id = association_.next_invoke_id();
    mms::MmsReadRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(object);
    const auto encoded = mms::MmsServiceCodec::encode_read_request_p_data(
        request, association_.negotiated().presentation_context_id);
    const auto exchange = association_.exchange_confirmed(encoded, invoke_id, stop_token);
    if (exchange.envelope.kind == mms::MmsPduKind::confirmed_error) {
        return std::nullopt;
    }
    const auto response = mms::MmsServiceCodec::decode_read_response(
        response_payload(exchange), invoke_id);
    if (response.results.size() != 1U || !response.results.front().success()) {
        return std::nullopt;
    }
    return response.results.front().value;
}

std::optional<mms::MmsTypeSpecification>
MmsAssociationControlTransport::variable_specification(
    const mms::MmsObjectName& object,
    const std::stop_token stop_token) {
    if (!association_.associated()) {
        return std::nullopt;
    }
    const auto invoke_id = association_.next_invoke_id();
    mms::MmsVariableAccessAttributesRequest request;
    request.invoke_id = invoke_id;
    request.name = object;
    const auto encoded =
        mms::MmsServiceCodec::encode_variable_access_attributes_request_p_data(
            request, association_.negotiated().presentation_context_id);
    const auto exchange = association_.exchange_confirmed(encoded, invoke_id, stop_token);
    if (exchange.envelope.kind == mms::MmsPduKind::confirmed_error) {
        return std::nullopt;
    }
    const auto response =
        mms::MmsServiceCodec::decode_variable_access_attributes_response(
            response_payload(exchange), invoke_id);
    return response.type;
}

std::vector<std::string> MmsAssociationControlTransport::domain_variable_names(
    const std::string& domain,
    const std::stop_token stop_token) {
    if (!association_.associated() || domain.empty()) {
        return {};
    }

    std::vector<std::string> names;
    std::string continue_after;
    constexpr std::size_t maximum_pages = 4'096U;
    for (std::size_t page = 0U; page < maximum_pages; ++page) {
        const auto invoke_id = association_.next_invoke_id();
        mms::MmsGetNameListRequest request;
        request.invoke_id = invoke_id;
        request.object_class = mms::MmsGetNameListObjectClass::named_variable;
        request.scope = mms::MmsObjectScopeKind::domain_specific;
        request.domain_id = domain;
        request.continue_after = continue_after;
        const auto encoded = mms::MmsServiceCodec::encode_get_name_list_request_p_data(
            request, association_.negotiated().presentation_context_id);
        const auto exchange = association_.exchange_confirmed(encoded, invoke_id, stop_token);
        if (exchange.envelope.kind == mms::MmsPduKind::confirmed_error) {
            throw std::runtime_error("GetNameList returned a confirmed MMS error during control discovery.");
        }
        const auto response = mms::MmsServiceCodec::decode_get_name_list_response(
            response_payload(exchange), invoke_id);
        names.insert(names.end(), response.names.begin(), response.names.end());
        if (!response.more_follows) {
            break;
        }
        if (response.names.empty()) {
            throw std::runtime_error(
                "GetNameList asserted moreFollows without returning a continuation name.");
        }
        const auto next = response.names.back();
        if (next.empty() || next == continue_after) {
            throw std::runtime_error("GetNameList continuation did not advance.");
        }
        continue_after = next;
        if (page + 1U == maximum_pages) {
            throw std::runtime_error("GetNameList exceeded the bounded control-discovery page limit.");
        }
    }

    std::sort(names.begin(), names.end());
    names.erase(std::unique(names.begin(), names.end()), names.end());
    return names;
}

ControlTransportWriteResult MmsAssociationControlTransport::write(
    const mms::MmsObjectName& object,
    mms::MmsDataValue value,
    const std::stop_token stop_token) {
    if (!association_.associated()) {
        return {};
    }
    const auto invoke_id = association_.next_invoke_id();
    mms::MmsWriteRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(object);
    request.values.push_back(std::move(value));
    const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
        request, association_.negotiated().presentation_context_id);
    const auto exchange = association_.exchange_confirmed(encoded, invoke_id, stop_token);
    if (exchange.envelope.kind == mms::MmsPduKind::confirmed_error) {
        return {};
    }
    const auto response = mms::MmsServiceCodec::decode_write_response(
        response_payload(exchange), invoke_id);
    if (response.results.size() != 1U) {
        return {};
    }
    const auto& result = response.results.front();
    return {result.success, result.failure_code};
}

void MmsAssociationControlTransport::clear_information_reports() {
    std::vector<std::uint8_t> ignored;
    while (association_.try_pop_information_report(ignored)) {
    }
}

bool MmsAssociationControlTransport::wait_information_report(
    const std::chrono::milliseconds timeout,
    mms::MmsInformationReport& report,
    const std::stop_token stop_token) {
    report = {};
    if (!association_.associated() || timeout <= std::chrono::milliseconds::zero()) {
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (association_.associated()) {
        std::vector<std::uint8_t> queued;
        if (association_.try_pop_information_report(queued)) {
            report = mms::MmsInformationReportCodec::decode(queued);
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (remaining <= std::chrono::milliseconds::zero()) {
            remaining = std::chrono::milliseconds{1};
        }

        mms::MmsPduEnvelope envelope;
        if (!association_.try_poll_once_for(remaining, envelope, stop_token)) {
            return false;
        }
        // route_received_payload() queues InformationReports. Any other PDU is
        // intentionally ignored here while preserving its normal router path.
    }
    return false;
}

} // namespace ar::iec61850::control
