// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/sample_counter.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>

namespace ar::iec61850::sampled_values {

std::uint16_t SampleCounterPolicy::initial_sample_count(
    const std::chrono::system_clock::time_point timestamp,
    const double sample_rate_hz,
    const std::optional<std::uint16_t> wrap,
    const SampleCounterMode mode) {
    if (mode == SampleCounterMode::free_run ||
        sample_rate_hz <= 0.0 ||
        !std::isfinite(sample_rate_hz)) {
        return 0U;
    }

    const double samples_per_second = wrap.has_value() && *wrap > 1U
        ? static_cast<double>(*wrap)
        : sample_rate_hz;
    if (samples_per_second <= 0.0 || !std::isfinite(samples_per_second)) {
        return 0U;
    }

    const auto epoch_seconds = std::chrono::duration<double>(
        timestamp.time_since_epoch()).count();
    const auto fraction = epoch_seconds - std::floor(epoch_seconds);
    auto sample = static_cast<std::int64_t>(std::floor(fraction * samples_per_second));
    const auto modulo = wrap.has_value() && *wrap > 1U
        ? static_cast<std::int64_t>(*wrap)
        : 65'536LL;
    sample %= modulo;
    if (sample < 0) {
        sample += modulo;
    }
    return static_cast<std::uint16_t>(sample);
}

std::uint16_t SampleCounterPolicy::increment(
    const std::uint16_t current,
    const std::optional<std::uint16_t> wrap,
    const std::uint32_t step) noexcept {
    if (step == 0U) {
        return current;
    }

    const auto modulo = wrap.has_value() && *wrap > 1U
        ? static_cast<std::uint64_t>(*wrap)
        : 65'536ULL;
    return static_cast<std::uint16_t>(
        (static_cast<std::uint64_t>(current) + step) % modulo);
}

bool SampleCounterTransition::is_anomaly() const noexcept {
    return kind == SampleCounterTransitionKind::gap ||
           kind == SampleCounterTransitionKind::duplicate ||
           kind == SampleCounterTransitionKind::out_of_order;
}

void SampleCounterTracker::reset() noexcept {
    last_.reset();
    expected_.reset();
}

SampleCounterTransition SampleCounterTracker::observe(
    const std::uint16_t actual,
    const std::optional<std::uint16_t> wrap,
    const bool restart_hint) {
    if (restart_hint && last_.has_value()) {
        const auto previous = last_;
        last_ = actual;
        expected_ = next(actual, wrap);
        return {
            SampleCounterTransitionKind::restart,
            actual,
            previous,
            std::nullopt,
            wrap,
            0U,
            "The counter state was reset by trusted restart/configuration evidence."};
    }

    if (!last_.has_value()) {
        last_ = actual;
        expected_ = next(actual, wrap);
        return {
            SampleCounterTransitionKind::initial,
            actual,
            std::nullopt,
            std::nullopt,
            wrap,
            0U,
            "Initial sample counter observation."};
    }

    const auto previous = *last_;
    if (actual == previous) {
        return {
            SampleCounterTransitionKind::duplicate,
            actual,
            previous,
            expected_,
            wrap,
            0U,
            "Duplicate smpCnt " + std::to_string(actual) + "."};
    }

    const auto expected = expected_.value_or(next(previous, wrap));
    if (actual == expected) {
        const bool wrapped = actual < previous;
        last_ = actual;
        expected_ = next(actual, wrap);
        return {
            wrapped ? SampleCounterTransitionKind::normal_wrap
                    : SampleCounterTransitionKind::continuous,
            actual,
            previous,
            expected,
            wrap,
            0U,
            wrapped ? "Normal smpCnt wrap." : "Continuous smpCnt transition."};
    }

    const auto modulus = resolve_modulus(wrap);
    const auto forward = distance_forward(expected, actual, modulus);
    const auto backward = distance_forward(actual, expected, modulus);
    const bool is_forward_gap = forward > 0U && forward < backward;

    last_ = actual;
    expected_ = next(actual, wrap);

    if (is_forward_gap) {
        return {
            SampleCounterTransitionKind::gap,
            actual,
            previous,
            expected,
            wrap,
            forward,
            "smpCnt advanced from expected " + std::to_string(expected) +
                " to " + std::to_string(actual) + "; " +
                std::to_string(forward) + " sample(s) were not observed."};
    }

    return {
        SampleCounterTransitionKind::out_of_order,
        actual,
        previous,
        expected,
        wrap,
        0U,
        "smpCnt " + std::to_string(actual) +
            " is behind expected " + std::to_string(expected) + "."};
}

std::uint16_t SampleCounterTracker::next(
    const std::uint16_t value,
    const std::optional<std::uint16_t> wrap) noexcept {
    return SampleCounterPolicy::increment(value, wrap);
}

std::uint32_t SampleCounterTracker::resolve_modulus(
    const std::optional<std::uint16_t> wrap) noexcept {
    return wrap.has_value() && *wrap > 1U
        ? static_cast<std::uint32_t>(*wrap)
        : 65'536U;
}

std::uint32_t SampleCounterTracker::distance_forward(
    const std::uint16_t from,
    const std::uint16_t to,
    const std::uint32_t modulus) noexcept {
    const auto from_value = static_cast<std::int64_t>(from);
    const auto to_value = static_cast<std::int64_t>(to);
    const auto modulus_value = static_cast<std::int64_t>(modulus);
    auto distance = (to_value - from_value) % modulus_value;
    if (distance < 0) {
        distance += modulus_value;
    }
    return static_cast<std::uint32_t>(distance);
}

} // namespace ar::iec61850::sampled_values
