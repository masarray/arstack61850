// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "ariec61850/simulation/ied_simulator_profile.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ar::iec61850::simulation {

struct IedSimulatorPointState final {
    std::string reference;
    std::string functional_constraint;
    SimulatorPointKind kind{SimulatorPointKind::status};
    std::string unit;
    std::string value;
    std::string quality{"good"};
    std::string origin{"Simulator"};
    std::uint64_t timestamp_unix_milliseconds{};
    std::string reason{"init"};

    friend bool operator==(const IedSimulatorPointState&, const IedSimulatorPointState&) = default;
};

struct IedSimulatorEvent final {
    std::uint64_t timestamp_unix_milliseconds{};
    std::string reference;
    std::string functional_constraint;
    std::string previous_value;
    std::string new_value;
    std::string previous_quality;
    std::string new_quality;
    std::string reason;

    friend bool operator==(const IedSimulatorEvent&, const IedSimulatorEvent&) = default;
};

struct IedSimulatorSnapshot final {
    std::uint64_t generated_at_unix_milliseconds{};
    std::string profile_name;
    std::size_t logical_device_count{};
    std::size_t logical_node_count{};
    std::size_t point_count{};
    std::size_t data_set_count{};
    std::size_t report_control_block_count{};
    std::vector<IedSimulatorPointState> points;
};

class IedSimulatorEngine final {
public:
    explicit IedSimulatorEngine(IedSimulatorProfile profile);

    [[nodiscard]] const IedSimulatorProfile& profile() const noexcept { return profile_; }
    [[nodiscard]] bool running() const noexcept { return running_; }
    [[nodiscard]] std::uint64_t step_index() const noexcept { return step_index_; }
    [[nodiscard]] const std::vector<IedSimulatorPointState>& point_states() const noexcept {
        return states_;
    }

    void start() noexcept { running_ = true; }
    void stop() noexcept { running_ = false; }
    void reset(std::uint64_t timestamp_unix_milliseconds = 0U);

    [[nodiscard]] const IedSimulatorPointState* find_point_state(
        const std::string& reference) const noexcept;

    [[nodiscard]] std::optional<IedSimulatorEvent> set_point_value(
        const std::string& reference,
        std::string value,
        std::string quality,
        std::string origin,
        std::uint64_t timestamp_unix_milliseconds);

    [[nodiscard]] std::vector<IedSimulatorEvent> step(
        std::uint64_t now_unix_milliseconds);

    [[nodiscard]] IedSimulatorSnapshot snapshot(
        std::uint64_t now_unix_milliseconds) const;

private:
    struct CaseInsensitiveHash final {
        [[nodiscard]] std::size_t operator()(const std::string& value) const noexcept;
    };

    struct CaseInsensitiveEqual final {
        [[nodiscard]] bool operator()(
            const std::string& left,
            const std::string& right) const noexcept;
    };

    [[nodiscard]] static bool should_simulate(const IedSimulatorPoint& point) noexcept;
    [[nodiscard]] std::string compute_value(
        const IedSimulatorPoint& point,
        double angle) const;

    void rebuild_index();

    IedSimulatorProfile profile_;
    std::vector<IedSimulatorPoint> points_;
    std::vector<IedSimulatorPointState> states_;
    std::unordered_map<std::string, std::size_t, CaseInsensitiveHash, CaseInsensitiveEqual> index_;
    std::uint64_t step_index_{};
    bool running_{};
};

} // namespace ar::iec61850::simulation
