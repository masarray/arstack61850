// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/control/command_termination.hpp"
#include "ariec61850/control/guarded_control.hpp"
#include "ariec61850/control/mms_control_structure.hpp"
#include "ariec61850/mms/association_runtime.hpp"
#include "ariec61850/mms/reporting.hpp"
#include "ariec61850/mms/services.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace ar::iec61850::control {

struct ControlTransportWriteResult final {
    bool success{};
    std::optional<std::uint32_t> failure_code;
};

class ControlTransport {
public:
    virtual ~ControlTransport() = default;

    [[nodiscard]] virtual bool associated() const noexcept = 0;
    [[nodiscard]] virtual std::uint64_t association_id() const noexcept = 0;

    [[nodiscard]] virtual std::optional<mms::MmsDataValue> read(
        const mms::MmsObjectName& object,
        std::stop_token stop_token = {}) = 0;
    [[nodiscard]] virtual std::optional<mms::MmsTypeSpecification> variable_specification(
        const mms::MmsObjectName& object,
        std::stop_token stop_token = {}) = 0;
    [[nodiscard]] virtual std::vector<std::string> domain_variable_names(
        const std::string& domain,
        std::stop_token stop_token = {}) = 0;
    [[nodiscard]] virtual ControlTransportWriteResult write(
        const mms::MmsObjectName& object,
        mms::MmsDataValue value,
        std::stop_token stop_token = {}) = 0;

    // Establishes the completion-observation boundary used by C# smart control:
    // stale reports are discarded before SBOw/Oper, while reports arriving
    // during the confirmed Write exchange remain queued by the association.
    virtual void clear_information_reports() = 0;
    [[nodiscard]] virtual bool wait_information_report(
        std::chrono::milliseconds timeout,
        mms::MmsInformationReport& report,
        std::stop_token stop_token = {}) = 0;
};

// Production adapter over the already-live MmsAssociationRuntime. It performs
// exactly one confirmed Read/GVAA/Write exchange per requested operation and
// uses the non-fatal bounded poll API while waiting for asynchronous reports.
class MmsAssociationControlTransport final : public ControlTransport {
public:
    explicit MmsAssociationControlTransport(mms::MmsAssociationRuntime& association) noexcept;

    [[nodiscard]] bool associated() const noexcept override;
    [[nodiscard]] std::uint64_t association_id() const noexcept override;
    [[nodiscard]] std::optional<mms::MmsDataValue> read(
        const mms::MmsObjectName& object,
        std::stop_token stop_token = {}) override;
    [[nodiscard]] std::optional<mms::MmsTypeSpecification> variable_specification(
        const mms::MmsObjectName& object,
        std::stop_token stop_token = {}) override;
    [[nodiscard]] std::vector<std::string> domain_variable_names(
        const std::string& domain,
        std::stop_token stop_token = {}) override;
    [[nodiscard]] ControlTransportWriteResult write(
        const mms::MmsObjectName& object,
        mms::MmsDataValue value,
        std::stop_token stop_token = {}) override;
    void clear_information_reports() override;
    [[nodiscard]] bool wait_information_report(
        std::chrono::milliseconds timeout,
        mms::MmsInformationReport& report,
        std::stop_token stop_token = {}) override;

private:
    mms::MmsAssociationRuntime& association_;
    std::uint64_t association_id_{};
};

struct ControlObjectDescriptor final {
    ControlObjectReference object{};
    ControlModel model{ControlModel::unknown};
    mms::MmsTypeSpecification ctl_val_specification{};
    mms::MmsTypeSpecification operate_specification{};
    std::optional<mms::MmsTypeSpecification> select_with_value_specification;
    std::optional<mms::MmsTypeSpecification> cancel_specification;
    std::optional<mms::MmsObjectName> status_object;
    std::string status_functional_constraint;
    std::chrono::milliseconds sbo_timeout{10'000};
    std::chrono::milliseconds operate_timeout{10'000};
    bool supports_time_activated_operate{};
    bool supports_command_termination{};
    std::string cdc;
    std::vector<std::string> discovery_evidence;

    [[nodiscard]] bool requires_select() const noexcept {
        return model_requires_select(model);
    }
    [[nodiscard]] bool enhanced() const noexcept {
        return model_is_enhanced(model);
    }
    [[nodiscard]] bool operationally_ready() const noexcept;
};

struct ControlDiscoveryOptions final {
    std::chrono::milliseconds default_sbo_timeout{10'000};
    std::chrono::milliseconds default_operate_timeout{10'000};
};

class ControlDescriptorDiscovery final {
public:
    [[nodiscard]] static ControlObjectDescriptor discover(
        ControlTransport& transport,
        const std::string& object_reference,
        ControlDiscoveryOptions options = {},
        std::stop_token stop_token = {});
};

struct ControlRequest final {
    ControlValue control_value{ControlValue::boolean(false)};
    OriginCategory origin_category{OriginCategory::station_control};
    std::vector<std::uint8_t> origin_identifier;
    std::optional<std::uint8_t> control_number;
    std::optional<std::chrono::system_clock::time_point> operate_at;
    bool test{};
    bool interlock_check{true};
    bool synchro_check{true};
    bool auto_select{true};
    std::optional<std::chrono::milliseconds> command_termination_timeout;
};

enum class ControlCompletionState : std::uint8_t {
    accepted,
    positive_termination,
    negative_termination,
    rejected,
    unsupported,
    timed_out,
    association_lost,
    cancelled,
};

struct ControlActionResult final {
    ControlAction action{ControlAction::operate};
    ControlCompletionState completion{ControlCompletionState::rejected};
    bool request_accepted{};
    bool command_termination_received{};
    bool positive_termination{};
    std::uint8_t control_number{};
    ControlError control_error{ControlError::no_error};
    AddCause add_cause{AddCause::none};
    std::int64_t raw_control_error{};
    std::int64_t raw_add_cause{25};
    std::optional<std::uint32_t> mms_failure_code;
    std::string message;
    std::vector<std::string> diagnostics;

    [[nodiscard]] bool success() const noexcept {
        return completion == ControlCompletionState::accepted ||
            completion == ControlCompletionState::positive_termination;
    }
};

struct ControlSessionOptions final {
    std::chrono::milliseconds application_error_grace_period{400};
    bool require_exact_named_control_fields{true};
    GuardedControlPolicy guarded_policy{}; // authorize==nullptr => default deny.
};

class ControlObjectSession final {
public:
    ControlObjectSession(
        ControlTransport& transport,
        ControlObjectDescriptor descriptor,
        ControlSessionOptions options = {});
    ~ControlObjectSession();

    ControlObjectSession(const ControlObjectSession&) = delete;
    ControlObjectSession& operator=(const ControlObjectSession&) = delete;

    [[nodiscard]] const ControlObjectDescriptor& descriptor() const noexcept {
        return descriptor_;
    }
    [[nodiscard]] bool has_active_selection() const noexcept {
        return active_context_.has_value();
    }

    [[nodiscard]] ControlActionResult select(
        const ControlRequest& request,
        std::stop_token stop_token = {});
    [[nodiscard]] ControlActionResult select_with_value(
        const ControlRequest& request,
        std::stop_token stop_token = {});
    [[nodiscard]] ControlActionResult operate(
        const ControlRequest& request,
        std::stop_token stop_token = {});
    [[nodiscard]] ControlActionResult cancel(
        std::stop_token stop_token = {});

    void on_association_closed() noexcept;

private:
    struct ActiveContext final {
        ControlRequest request{};
        MmsControlSequenceContext mms{};
        std::vector<std::uint8_t> encoded_ctl_value;
        std::uint64_t timestamp_token{};
        std::uint64_t operate_at_token{};
        std::chrono::steady_clock::time_point created{};
    };

    [[nodiscard]] ActiveContext create_context(const ControlRequest& request);
    [[nodiscard]] ControlSequenceView sequence_view(
        const ControlRequest& request,
        const ActiveContext& context,
        const std::vector<std::uint8_t>& encoded_ctl_value) const noexcept;
    [[nodiscard]] ControlActionResult write_failure(
        ControlAction action,
        const ActiveContext& context,
        const ControlTransportWriteResult& write,
        const std::optional<CommandTermination>& app_error = std::nullopt) const;
    [[nodiscard]] std::optional<CommandTermination> wait_for_termination(
        std::chrono::milliseconds timeout,
        std::stop_token stop_token,
        const ActiveContext& context);
    [[nodiscard]] std::optional<CommandTermination> wait_for_application_error(
        std::chrono::milliseconds timeout,
        std::stop_token stop_token,
        const ActiveContext& context);
    [[nodiscard]] bool selection_expired(const ActiveContext& context) const noexcept;
    [[nodiscard]] std::chrono::milliseconds effective_operate_timeout(
        const ControlRequest& request) const;
    [[nodiscard]] bool positive_sbo_selection(const mms::MmsDataValue& value) const;
    void best_effort_cancel() noexcept;
    void clear_selection() noexcept;

    ControlTransport& transport_;
    ControlObjectDescriptor descriptor_{};
    ControlSessionOptions options_{};
    GuardedControlPlanner planner_;
    std::optional<ActiveContext> active_context_;
    std::optional<ActiveContext> expired_context_;
    std::uint8_t next_control_number_{};
};

} // namespace ar::iec61850::control
