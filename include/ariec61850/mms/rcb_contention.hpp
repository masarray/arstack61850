// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ar::iec61850::mms {

struct MmsRcbContentionProbeObservation final {
    std::size_t probe_number{};
    std::chrono::system_clock::time_point captured_at{};
    std::string rcb_reference;
    std::string rpt_ena;
    std::string resv;
    std::string resv_tms;
    std::string data_set_reference;
    std::string conf_rev;
    std::string message;
};

struct MmsRcbContentionProbeResult final {
    std::string rcb_reference;
    bool is_contended{};
    bool is_busy_at_probe{};
    bool is_flapping{};
    int cooldown_seconds{};
    std::string decision;
    std::string reason;
    std::string recommended_action;
    std::vector<MmsRcbContentionProbeObservation> observations;
};

struct MmsRcbContentionProbeOptions final {
    std::size_t probe_count{1U};
    std::chrono::milliseconds probe_delay{1'000};
    int cooldown_seconds{60};
};

class MmsRcbContentionProbeError final : public MmsAssociationRuntimeError {
public:
    using MmsAssociationRuntimeError::MmsAssociationRuntimeError;
};

class MmsRcbContentionProbeEvaluator final {
public:
    [[nodiscard]] static MmsRcbContentionProbeResult evaluate(
        std::string rcb_reference,
        std::span<const MmsRcbContentionProbeObservation> observations,
        const int cooldown_seconds = 60) {
        if (cooldown_seconds < 0) {
            throw std::invalid_argument(
                "RCB contention cooldown must be greater than or equal to zero.");
        }

        const auto rpt_ena_states = distinct_normalized(
            observations, [](const auto& observation) {
                return observation.rpt_ena;
            });
        const auto reservation_states = distinct_normalized(
            observations, [](const auto& observation) {
                // C# chooses ResvTms using IsNullOrWhiteSpace before
                // NormalizeProbeValue converts a literal "-" to empty.
                return trim(observation.resv_tms).empty()
                    ? observation.resv
                    : observation.resv_tms;
            });
        const auto data_sets = distinct_normalized(
            observations, [](const auto& observation) {
                return observation.data_set_reference;
            });
        const auto conf_revs = distinct_normalized(
            observations, [](const auto& observation) {
                return observation.conf_rev;
            });

        const bool is_busy = std::any_of(
            observations.begin(), observations.end(), [](const auto& observation) {
                return is_true_like(observation.rpt_ena) ||
                       is_true_like(observation.resv) ||
                       is_positive_integer(observation.resv_tms);
            });
        const bool is_flapping = rpt_ena_states.size() > 1U ||
                                 reservation_states.size() > 1U ||
                                 data_sets.size() > 1U ||
                                 conf_revs.size() > 1U;
        const bool is_contended = is_busy || is_flapping;

        MmsRcbContentionProbeResult result;
        result.rcb_reference = std::move(rcb_reference);
        result.is_contended = is_contended;
        result.is_busy_at_probe = is_busy;
        result.is_flapping = is_flapping;
        result.cooldown_seconds = is_contended ? cooldown_seconds : 0;
        result.decision = is_contended ? "CooldownSkip" : "StableProceed";
        result.reason = is_flapping
            ? "RCB state changed across pre-claim probes. Treat as contended/flapping to avoid fighting another client."
            : is_busy
                ? "RCB became busy/reserved during pre-claim probes. Treat as owned by another client and skip."
                : "RCB remained stable and free across pre-claim probes.";
        result.recommended_action = is_contended
            ? "Do not write RptEna/DatSet on this RCB in the current command; try the next candidate or polling fallback."
            : "Safe to continue with guarded claim attempt.";
        result.observations.assign(observations.begin(), observations.end());
        return result;
    }

private:
    [[nodiscard]] static std::string trim(std::string value) {
        const auto not_space = [](const unsigned char character) {
            return std::isspace(character) == 0;
        };
        const auto first = std::find_if(
            value.begin(), value.end(), [&](const char character) {
                return not_space(static_cast<unsigned char>(character));
            });
        const auto last = std::find_if(
            value.rbegin(), value.rend(), [&](const char character) {
                return not_space(static_cast<unsigned char>(character));
            }).base();
        if (first >= last) {
            return {};
        }
        return std::string{first, last};
    }

    [[nodiscard]] static std::string lower_ascii(std::string value) {
        std::transform(
            value.begin(), value.end(), value.begin(), [](const char character) {
                return static_cast<char>(
                    std::tolower(static_cast<unsigned char>(character)));
            });
        return value;
    }

    [[nodiscard]] static std::string normalize_probe_value(
        const std::string_view value) {
        auto text = trim(std::string{value});
        return text == "-" ? std::string{} : text;
    }

    [[nodiscard]] static bool is_true_like(const std::string_view value) {
        const auto text = lower_ascii(trim(std::string{value}));
        return text == "true" || text == "1" || text == "yes" || text == "on";
    }

    [[nodiscard]] static bool is_positive_integer(const std::string_view value) {
        const auto text = trim(std::string{value});
        if (text.empty()) {
            return false;
        }
        std::uint64_t number{};
        const auto* begin = text.data();
        const auto* end = text.data() + text.size();
        const auto result = std::from_chars(begin, end, number, 10);
        return result.ec == std::errc{} && result.ptr == end && number > 0U;
    }

    template <typename Projection>
    [[nodiscard]] static std::vector<std::string> distinct_normalized(
        std::span<const MmsRcbContentionProbeObservation> observations,
        Projection projection) {
        std::vector<std::string> result;
        for (const auto& observation : observations) {
            auto normalized = normalize_probe_value(projection(observation));
            if (normalized.empty()) {
                continue;
            }
            const auto normalized_lower = lower_ascii(normalized);
            const auto duplicate = std::any_of(
                result.begin(), result.end(), [&](const auto& existing) {
                    return lower_ascii(existing) == normalized_lower;
                });
            if (!duplicate) {
                result.push_back(std::move(normalized));
            }
        }
        return result;
    }
};

// Read-only runtime counterpart of ARIEC61850 ProbeSelectedRcbContentionAsync.
// It performs repeated MMS Read requests only. It never writes DatSet/RptEna,
// reserves an RCB, sends GI, or mutates a DataSet.
class MmsRcbContentionProbeClient final {
public:
    explicit MmsRcbContentionProbeClient(MmsAssociationRuntime& association)
        : association_{association} {}

    [[nodiscard]] MmsRcbContentionProbeResult probe(
        const MmsReportControlCandidate& candidate,
        const MmsRcbContentionProbeOptions& options = {},
        const std::stop_token stop_token = {}) {
        validate_options(options);
        if (!association_.associated()) {
            throw MmsRcbContentionProbeError(
                "RCB contention probe requires an active MMS association.");
        }

        const auto attributes = select_attributes(candidate);
        if (attributes.empty()) {
            throw MmsRcbContentionProbeError(
                "RCB contention probe found none of DatSet/ConfRev/RptEna/Resv/ResvTms on " +
                candidate.reference + '.');
        }

        std::vector<MmsRcbContentionProbeObservation> observations;
        observations.reserve(options.probe_count);
        for (std::size_t index = 1U; index <= options.probe_count; ++index) {
            if (stop_token.stop_requested()) {
                throw MmsRcbContentionProbeError("RCB contention probe cancelled.");
            }

            const auto state = read_state(candidate, attributes, stop_token);
            MmsRcbContentionProbeObservation observation;
            observation.probe_number = index;
            observation.captured_at = std::chrono::system_clock::now();
            observation.rcb_reference = candidate.reference;
            observation.rpt_ena = bool_text(state.report_enabled);
            observation.resv = bool_text(state.reserved);
            observation.resv_tms = unsigned_text(state.reservation_time_seconds);
            observation.data_set_reference = state.data_set_reference;
            observation.conf_rev = unsigned_text(state.configuration_revision);
            if (!state.diagnostics.empty()) {
                observation.message = state.diagnostics.back();
            }
            observations.push_back(std::move(observation));

            if (index < options.probe_count && options.probe_delay.count() > 0) {
                cancellable_sleep(options.probe_delay, stop_token);
            }
        }

        return MmsRcbContentionProbeEvaluator::evaluate(
            candidate.reference, observations, options.cooldown_seconds);
    }

private:
    static void validate_options(const MmsRcbContentionProbeOptions& options) {
        if (options.probe_count == 0U) {
            throw std::invalid_argument("RCB contention probe count must be at least one.");
        }
        if (options.probe_delay.count() < 0) {
            throw std::invalid_argument(
                "RCB contention probe delay must be greater than or equal to zero.");
        }
        if (options.cooldown_seconds < 0) {
            throw std::invalid_argument(
                "RCB contention cooldown must be greater than or equal to zero.");
        }
    }

    [[nodiscard]] static std::vector<std::string> select_attributes(
        const MmsReportControlCandidate& candidate) {
        static constexpr std::string_view wanted[]{
            "DatSet", "ConfRev", "RptEna", "Resv", "ResvTms"};
        std::vector<std::string> result;
        for (const auto name : wanted) {
            const auto found = std::find(
                candidate.attributes.begin(), candidate.attributes.end(), name);
            if (found != candidate.attributes.end()) {
                result.emplace_back(name);
            }
        }
        return result;
    }

    [[nodiscard]] MmsReportControlState read_state(
        const MmsReportControlCandidate& candidate,
        const std::vector<std::string>& attributes,
        const std::stop_token stop_token) {
        const auto invoke_id = association_.next_invoke_id();
        const auto request = MmsReportControlStateMapper::build_read_request(
            invoke_id, candidate, attributes);
        const auto encoded = MmsServiceCodec::encode_read_request_p_data(
            request, association_.negotiated().presentation_context_id);
        const auto exchange = association_.exchange_confirmed(
            encoded, invoke_id, stop_token);
        if (exchange.envelope.kind != MmsPduKind::confirmed_response) {
            throw MmsRcbContentionProbeError(
                "Read RCB contention attributes returned a non-confirmed-response MMS PDU.");
        }

        std::span<const std::uint8_t> payload;
        if (!exchange.presentation_payload.empty()) {
            payload = exchange.presentation_payload;
        } else if (!exchange.envelope.mms_payload.empty()) {
            payload = exchange.envelope.mms_payload;
        } else {
            throw MmsRcbContentionProbeError(
                "Read RCB contention attributes returned no decodable MMS payload.");
        }

        const auto response = MmsServiceCodec::decode_read_response(payload, invoke_id);
        return MmsReportControlStateMapper::map_read_response(
            candidate, attributes, response);
    }

    static void cancellable_sleep(
        const std::chrono::milliseconds duration,
        const std::stop_token stop_token) {
        using namespace std::chrono_literals;
        auto remaining = duration;
        while (remaining.count() > 0) {
            if (stop_token.stop_requested()) {
                throw MmsRcbContentionProbeError("RCB contention probe cancelled.");
            }
            const auto slice = std::min(remaining, 10ms);
            std::this_thread::sleep_for(slice);
            remaining -= slice;
        }
    }

    [[nodiscard]] static std::string bool_text(const std::optional<bool> value) {
        if (!value) {
            return {};
        }
        return *value ? "true" : "false";
    }

    [[nodiscard]] static std::string unsigned_text(
        const std::optional<std::uint64_t> value) {
        return value ? std::to_string(*value) : std::string{};
    }

    MmsAssociationRuntime& association_;
};

} // namespace ar::iec61850::mms
