// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/sampled_values/asdu.hpp"
#include "ariec61850/sampled_values/sample_counter.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ar::iec61850::sampled_values {

struct StreamExpectation final {
    std::optional<std::string> sv_id;
    std::optional<std::string> data_set_reference;
    std::optional<std::uint32_t> configuration_revision;
    std::optional<std::uint16_t> sample_counter_wrap;
};

struct StreamObservation final {
    SampleCounterTransition counter;
    bool identity_matches{true};
    bool configuration_changed{};
    std::vector<std::string> diagnostics;
};

struct StreamStatistics final {
    std::uint64_t observations{};
    std::uint64_t continuous{};
    std::uint64_t normal_wraps{};
    std::uint64_t gaps{};
    std::uint64_t duplicates{};
    std::uint64_t out_of_order{};
    std::uint64_t restarts{};
    std::uint64_t missing_samples{};
    std::uint64_t identity_mismatches{};
    std::uint64_t configuration_changes{};
};

class SampledValuesStreamSupervisor final {
public:
    explicit SampledValuesStreamSupervisor(StreamExpectation expectation = {});

    [[nodiscard]] const StreamExpectation& expectation() const noexcept { return expectation_; }
    [[nodiscard]] const StreamStatistics& statistics() const noexcept { return statistics_; }
    [[nodiscard]] std::optional<std::uint32_t> last_configuration_revision() const noexcept {
        return last_configuration_revision_;
    }

    void reset() noexcept;

    [[nodiscard]] StreamObservation observe(
        const SampledValueAsdu& asdu,
        bool restart_hint = false);

    [[nodiscard]] std::vector<StreamObservation> observe(
        const SampledValuesPdu& pdu,
        bool restart_hint = false);

private:
    StreamExpectation expectation_;
    SampleCounterTracker counter_tracker_;
    StreamStatistics statistics_;
    std::optional<std::uint32_t> last_configuration_revision_;
};

} // namespace ar::iec61850::sampled_values
