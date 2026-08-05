// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>

namespace ar::iec61850::sampled_values {

enum class SampleCounterMode : std::uint8_t {
    free_run,
    second_aligned
};

class SampleCounterPolicy final {
public:
    [[nodiscard]] static std::uint16_t initial_sample_count(
        std::chrono::system_clock::time_point timestamp,
        double sample_rate_hz,
        std::optional<std::uint16_t> wrap,
        SampleCounterMode mode);

    [[nodiscard]] static std::uint16_t increment(
        std::uint16_t current,
        std::optional<std::uint16_t> wrap,
        std::uint32_t step = 1U) noexcept;
};

enum class SampleCounterTransitionKind : std::uint8_t {
    initial,
    continuous,
    normal_wrap,
    gap,
    duplicate,
    out_of_order,
    restart
};

struct SampleCounterTransition final {
    SampleCounterTransitionKind kind{SampleCounterTransitionKind::initial};
    std::uint16_t actual{};
    std::optional<std::uint16_t> previous;
    std::optional<std::uint16_t> expected;
    std::optional<std::uint16_t> wrap;
    std::uint32_t missing_samples{};
    std::string detail;

    [[nodiscard]] bool is_anomaly() const noexcept;
};

class SampleCounterTracker final {
public:
    [[nodiscard]] std::optional<std::uint16_t> last() const noexcept { return last_; }
    [[nodiscard]] std::optional<std::uint16_t> expected() const noexcept { return expected_; }

    void reset() noexcept;

    [[nodiscard]] SampleCounterTransition observe(
        std::uint16_t actual,
        std::optional<std::uint16_t> wrap,
        bool restart_hint = false);

private:
    [[nodiscard]] static std::uint16_t next(
        std::uint16_t value, std::optional<std::uint16_t> wrap) noexcept;
    [[nodiscard]] static std::uint32_t resolve_modulus(
        std::optional<std::uint16_t> wrap) noexcept;
    [[nodiscard]] static std::uint32_t distance_forward(
        std::uint16_t from, std::uint16_t to, std::uint32_t modulus) noexcept;

    std::optional<std::uint16_t> last_;
    std::optional<std::uint16_t> expected_;
};

} // namespace ar::iec61850::sampled_values
