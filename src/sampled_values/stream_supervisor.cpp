// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/stream_supervisor.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ar::iec61850::sampled_values {

SampledValuesStreamSupervisor::SampledValuesStreamSupervisor(
    StreamExpectation expectation)
    : expectation_{std::move(expectation)} {}

void SampledValuesStreamSupervisor::reset() noexcept {
    counter_tracker_.reset();
    statistics_ = {};
    last_configuration_revision_.reset();
}

StreamObservation SampledValuesStreamSupervisor::observe(
    const SampledValueAsdu& asdu,
    const bool restart_hint) {
    StreamObservation observation;

    if (expectation_.sv_id.has_value() && *expectation_.sv_id != asdu.sv_id) {
        observation.identity_matches = false;
        observation.diagnostics.push_back(
            "svID mismatch: expected '" + *expectation_.sv_id +
            "', observed '" + asdu.sv_id + "'.");
    }
    if (expectation_.data_set_reference.has_value() &&
        *expectation_.data_set_reference != asdu.data_set_reference) {
        observation.identity_matches = false;
        observation.diagnostics.push_back(
            "datSet mismatch: expected '" + *expectation_.data_set_reference +
            "', observed '" + asdu.data_set_reference + "'.");
    }
    if (expectation_.configuration_revision.has_value() &&
        *expectation_.configuration_revision != asdu.configuration_revision) {
        observation.identity_matches = false;
        observation.diagnostics.push_back(
            "confRev mismatch: expected " +
            std::to_string(*expectation_.configuration_revision) +
            ", observed " + std::to_string(asdu.configuration_revision) + ".");
    }

    observation.configuration_changed = last_configuration_revision_.has_value() &&
        *last_configuration_revision_ != asdu.configuration_revision;
    observation.counter = counter_tracker_.observe(
        asdu.sample_count,
        expectation_.sample_counter_wrap,
        restart_hint || observation.configuration_changed);
    last_configuration_revision_ = asdu.configuration_revision;

    ++statistics_.observations;
    if (!observation.identity_matches) {
        ++statistics_.identity_mismatches;
    }
    if (observation.configuration_changed) {
        ++statistics_.configuration_changes;
    }

    switch (observation.counter.kind) {
    case SampleCounterTransitionKind::continuous:
        ++statistics_.continuous;
        break;
    case SampleCounterTransitionKind::normal_wrap:
        ++statistics_.normal_wraps;
        break;
    case SampleCounterTransitionKind::gap:
        ++statistics_.gaps;
        statistics_.missing_samples += observation.counter.missing_samples;
        break;
    case SampleCounterTransitionKind::duplicate:
        ++statistics_.duplicates;
        break;
    case SampleCounterTransitionKind::out_of_order:
        ++statistics_.out_of_order;
        break;
    case SampleCounterTransitionKind::restart:
        ++statistics_.restarts;
        break;
    case SampleCounterTransitionKind::initial:
        break;
    }

    return observation;
}

std::vector<StreamObservation> SampledValuesStreamSupervisor::observe(
    const SampledValuesPdu& pdu,
    const bool restart_hint) {
    std::vector<StreamObservation> observations;
    observations.reserve(pdu.asdus.size());
    for (std::size_t index = 0U; index < pdu.asdus.size(); ++index) {
        observations.push_back(observe(pdu.asdus[index], restart_hint && index == 0U));
    }
    return observations;
}

} // namespace ar::iec61850::sampled_values
