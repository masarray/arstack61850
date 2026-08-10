// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"

#include "ariec61850/mms/data_codec.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace ar::iec61850::control {
namespace {

[[nodiscard]] std::string normalize_name(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (const auto character : value) {
        if ((character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9')) {
            result.push_back(character >= 'A' && character <= 'Z'
                ? static_cast<char>(character - 'A' + 'a')
                : character);
        }
    }
    return result;
}

[[nodiscard]] const mms::MmsTypeSpecification* find_child(
    const mms::MmsTypeSpecification& parent,
    const std::string& name) noexcept {
    const auto target = normalize_name(name);
    for (const auto& child : parent.children) {
        if (normalize_name(child.name) == target) {
            return &child;
        }
    }
    return nullptr;
}

[[nodiscard]] mms::MmsObjectName object_name(
    const ControlObjectReference& object,
    const std::string_view functional_constraint,
    const std::string_view leaf) {
    std::array<char, 1'024U> item{};
    std::size_t bytes{};
    if (!build_control_item(object, functional_constraint, leaf, item, bytes)) {
        throw std::invalid_argument("Failed to build bounded control MMS object name.");
    }
    return mms::MmsObjectName::domain_specific(
        std::string{object.domain}, std::string{item.data(), bytes});
}

[[nodiscard]] std::uint8_t next_control_number(std::uint8_t& value) noexcept {
    value = value >= 255U ? 1U : static_cast<std::uint8_t>(value + 1U);
    return value;
}

[[nodiscard]] std::uint64_t time_token(
    const std::chrono::system_clock::time_point value) noexcept {
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        value.time_since_epoch()).count();
    return static_cast<std::uint64_t>(nanos);
}

[[nodiscard]] std::optional<mms::MmsDataValue> timestamp_value(
    const std::chrono::system_clock::time_point value,
    const mms::MmsTypeSpecification& specification) {
    if (specification.kind == mms::MmsTypeKind::utc_time) {
        return mms::MmsDataValue::utc_time(mms::Iec61850UtcTime{value, 0U});
    }
    if (specification.kind != mms::MmsTypeKind::binary_time) {
        return std::nullopt;
    }

    using namespace std::chrono;
    constexpr sys_days binary_epoch{year{1984}/1/1};
    const auto millis = floor<milliseconds>(value);
    const auto day = floor<days>(millis);
    if (day < binary_epoch) {
        std::array<std::uint8_t, 6U> zero{};
        const auto size = specification.size.value_or(6U) == 4U ? 4U : 6U;
        return mms::MmsDataValue::binary_time(
            std::span<const std::uint8_t>{zero}.first(size));
    }

    const auto milliseconds_since_midnight =
        duration_cast<milliseconds>(millis - day).count();
    const auto days_since_epoch = (day - binary_epoch).count();
    if (milliseconds_since_midnight < 0 ||
        milliseconds_since_midnight > static_cast<std::int64_t>(
            std::numeric_limits<std::uint32_t>::max()) ||
        days_since_epoch < 0 || days_since_epoch > 65'535) {
        return std::nullopt;
    }

    const auto ms = static_cast<std::uint32_t>(milliseconds_since_midnight);
    std::array<std::uint8_t, 6U> bytes{
        static_cast<std::uint8_t>((ms >> 24U) & 0xFFU),
        static_cast<std::uint8_t>((ms >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((ms >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(ms & 0xFFU),
        static_cast<std::uint8_t>((static_cast<std::uint16_t>(days_since_epoch) >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(static_cast<std::uint16_t>(days_since_epoch) & 0xFFU),
    };
    const auto size = specification.size.value_or(6U) == 4U ? 4U : 6U;
    return mms::MmsDataValue::binary_time(
        std::span<const std::uint8_t>{bytes}.first(size));
}

[[nodiscard]] GuardedControlPolicy planner_policy(
    const ControlObjectDescriptor& descriptor,
    const ControlSessionOptions& options) noexcept {
    auto policy = options.guarded_policy;
    policy.sbo_timeout_ms = descriptor.sbo_timeout.count() <= 0
        ? 10'000U
        : static_cast<std::uint64_t>(descriptor.sbo_timeout.count());
    return policy;
}

[[nodiscard]] ControlActionResult rejected(
    const ControlAction action,
    std::string message,
    const std::uint8_t control_number = 0U) {
    ControlActionResult result;
    result.action = action;
    result.completion = ControlCompletionState::rejected;
    result.control_number = control_number;
    result.message = std::move(message);
    return result;
}

[[nodiscard]] ControlActionResult unsupported(
    const ControlAction action,
    std::string message) {
    auto result = rejected(action, std::move(message));
    result.completion = ControlCompletionState::unsupported;
    return result;
}

[[nodiscard]] ControlActionResult from_planner_rejection(
    const GuardedControlDecision& decision) {
    auto result = rejected(decision.action, "Guarded control policy rejected the action.",
                           decision.control_number);
    result.control_error = decision.control_error;
    result.add_cause = decision.add_cause;
    return result;
}

[[nodiscard]] bool same_reference_text(
    const std::string& selected,
    const ControlObjectReference& object) {
    if (selected.empty()) {
        return false;
    }
    std::string normalized = selected;
    std::replace(normalized.begin(), normalized.end(), '$', '.');
    const auto marker = std::string{".CO."};
    if (const auto position = normalized.find(marker); position != std::string::npos) {
        normalized.erase(position, marker.size() - 1U);
    }
    const auto expected = std::string{object.domain} + "/" +
        std::string{object.logical_node} + "." + std::string{object.data_object_path};
    const auto relative = std::string{object.logical_node} + "." +
        std::string{object.data_object_path};
    if (normalized == expected || normalized == relative) {
        return true;
    }
    if (normalized.size() >= relative.size() + 1U &&
        normalized.ends_with("/" + relative)) {
        return true;
    }
    // C# oracle permits opaque short selection tokens, but not a token that
    // looks like a different IEC 61850 object reference.
    return normalized.find('/') == std::string::npos &&
        normalized.find('.') == std::string::npos &&
        normalized.find('$') == std::string::npos;
}

} // namespace

ControlObjectSession::ControlObjectSession(
    ControlTransport& transport,
    ControlObjectDescriptor descriptor,
    ControlSessionOptions options)
    : transport_{transport},
      descriptor_{std::move(descriptor)},
      options_{std::move(options)},
      planner_{descriptor_.object, descriptor_.model,
               planner_policy(descriptor_, options_)} {
    if (!transport_.associated()) {
        throw std::invalid_argument("Control session requires an active MMS association.");
    }
    if (!descriptor_.operationally_ready()) {
        throw std::invalid_argument("Control descriptor is not operationally ready.");
    }
    if (options_.application_error_grace_period < std::chrono::milliseconds::zero()) {
        throw std::invalid_argument("Application error grace period must not be negative.");
    }
}

ControlObjectSession::~ControlObjectSession() {
    best_effort_cancel();
}

ControlObjectSession::ActiveContext ControlObjectSession::create_context(
    const ControlRequest& request) {
    ActiveContext context;
    context.request = request;
    context.created = std::chrono::steady_clock::now();

    const auto bound = MmsControlStructureBuilder::bind_control_value(
        request.control_value, descriptor_.ctl_val_specification);
    if (!bound.success()) {
        throw std::invalid_argument(
            "Control value does not match live ctlVal TypeSpecification at " + bound.path + ".");
    }
    context.encoded_ctl_value = mms::MmsDataCodec::encode(bound.value.value());

    const auto assigned = request.control_number.value_or(
        next_control_number(next_control_number_));
    const auto now = std::chrono::system_clock::now();
    const auto* timestamp_specification = find_child(
        descriptor_.operate_specification, "T");
    if (timestamp_specification == nullptr) {
        throw std::runtime_error("Live Oper TypeSpecification has no named T field.");
    }
    const auto timestamp = timestamp_value(now, *timestamp_specification);
    if (!timestamp.has_value()) {
        throw std::runtime_error("Live T field has an unsupported timestamp type.");
    }

    context.mms.control_value = request.control_value;
    context.mms.origin_category = request.origin_category;
    context.mms.origin_identifier = request.origin_identifier;
    context.mms.control_number = assigned;
    context.mms.timestamp = timestamp.value();
    context.mms.test = request.test;
    context.mms.interlock_check = request.interlock_check;
    context.mms.synchro_check = request.synchro_check;
    context.timestamp_token = time_token(now);

    if (request.operate_at.has_value()) {
        const auto* operate_at_specification = find_child(
            descriptor_.operate_specification, "operTm");
        if (operate_at_specification == nullptr) {
            throw std::invalid_argument(
                "Time-activated operation requested but live Oper has no operTm field.");
        }
        const auto operate_at = timestamp_value(
            request.operate_at.value(), *operate_at_specification);
        if (!operate_at.has_value()) {
            throw std::invalid_argument("Live operTm field has an unsupported timestamp type.");
        }
        context.mms.operate_at = operate_at.value();
        context.operate_at_token = time_token(request.operate_at.value());
    }
    return context;
}

ControlSequenceView ControlObjectSession::sequence_view(
    const ControlRequest& request,
    const ActiveContext& context,
    const std::vector<std::uint8_t>& encoded_ctl_value) const noexcept {
    ControlSequenceView sequence;
    sequence.control_value = encoded_ctl_value;
    sequence.origin_category = request.origin_category;
    sequence.origin_identifier = request.origin_identifier;
    sequence.control_number = request.control_number.value_or(context.mms.control_number);
    sequence.timestamp_token = context.timestamp_token;
    sequence.operate_at_token = request.operate_at.has_value()
        ? time_token(request.operate_at.value())
        : 0U;
    sequence.test = request.test;
    sequence.interlock_check = request.interlock_check;
    sequence.synchro_check = request.synchro_check;
    return sequence;
}

bool ControlObjectSession::positive_sbo_selection(
    const mms::MmsDataValue& value) const {
    if (value.kind() == mms::MmsDataKind::boolean) {
        if (const auto* selected = std::get_if<bool>(&value.value())) {
            return *selected;
        }
        return false;
    }
    if (value.kind() == mms::MmsDataKind::visible_string ||
        value.kind() == mms::MmsDataKind::mms_string) {
        if (const auto* selected = std::get_if<std::string>(&value.value())) {
            return same_reference_text(*selected, descriptor_.object);
        }
    }
    return false;
}

ControlActionResult ControlObjectSession::select(
    const ControlRequest& request,
    const std::stop_token stop_token) {
    if (descriptor_.model != ControlModel::select_before_operate_normal) {
        return unsupported(ControlAction::select,
            "Select read is valid only for SBO normal security.");
    }
    if (!transport_.associated()) {
        return rejected(ControlAction::select, "MMS association is not active.");
    }

    auto context = create_context(request);
    const auto planner_decision = planner_.select(
        {transport_.association_id()},
        sequence_view(request, context, context.encoded_ctl_value),
        0U);
    if (!planner_decision.accepted()) {
        return from_planner_rejection(planner_decision);
    }

    const auto selected = transport_.read(
        object_name(descriptor_.object, "CO", "SBO"), stop_token);
    if (!selected.has_value() || !positive_sbo_selection(selected.value())) {
        planner_.on_association_closed(transport_.association_id());
        return rejected(ControlAction::select,
            "SBO read did not return a positive selected-object reference.",
            context.mms.control_number);
    }

    active_context_ = context;
    expired_context_.reset();
    ControlActionResult result;
    result.action = ControlAction::select;
    result.completion = ControlCompletionState::accepted;
    result.request_accepted = true;
    result.control_number = context.mms.control_number;
    result.message = "SBO select accepted; exact sequence retained until Operate/Cancel/timeout.";
    return result;
}

ControlActionResult ControlObjectSession::select_with_value(
    const ControlRequest& request,
    const std::stop_token stop_token) {
    if (descriptor_.model != ControlModel::select_before_operate_enhanced ||
        !descriptor_.select_with_value_specification.has_value()) {
        return unsupported(ControlAction::select_with_value,
            "SelectWithValue is valid only for SBO enhanced security.");
    }
    if (!transport_.associated()) {
        return rejected(ControlAction::select_with_value, "MMS association is not active.");
    }

    auto context = create_context(request);
    const auto planner_decision = planner_.select_with_value(
        {transport_.association_id()},
        sequence_view(request, context, context.encoded_ctl_value),
        0U);
    if (!planner_decision.accepted()) {
        return from_planner_rejection(planner_decision);
    }

    const auto value = MmsControlStructureBuilder::build_select_with_value(
        context.mms,
        descriptor_.select_with_value_specification.value(),
        options_.require_exact_named_control_fields);
    if (!value.success()) {
        planner_.on_association_closed(transport_.association_id());
        return rejected(ControlAction::select_with_value,
            "Cannot construct exact live SBOw structure at " + value.path + ".",
            context.mms.control_number);
    }

    transport_.clear_information_reports();
    const auto write = transport_.write(
        object_name(descriptor_.object, "CO", "SBOw"),
        value.value.value(), stop_token);
    if (!write.success) {
        const auto app_error = wait_for_application_error(
            options_.application_error_grace_period, stop_token);
        planner_.on_association_closed(transport_.association_id());
        return write_failure(ControlAction::select_with_value, context, write, app_error);
    }

    const auto post_error = wait_for_application_error(
        options_.application_error_grace_period, stop_token);
    if (post_error.has_value() && !post_error->positive) {
        planner_.on_association_closed(transport_.association_id());
        ControlActionResult result;
        result.action = ControlAction::select_with_value;
        result.completion = ControlCompletionState::rejected;
        result.request_accepted = true;
        result.control_number = context.mms.control_number;
        result.control_error = post_error->control_error;
        result.add_cause = post_error->add_cause;
        result.raw_control_error = post_error->raw_control_error;
        result.raw_add_cause = post_error->raw_add_cause;
        result.message = "SBOw received asynchronous LastApplError after MMS service acceptance.";
        return result;
    }

    active_context_ = context;
    expired_context_.reset();
    ControlActionResult result;
    result.action = ControlAction::select_with_value;
    result.completion = ControlCompletionState::accepted;
    result.request_accepted = true;
    result.control_number = context.mms.control_number;
    result.message = "SBOw accepted; exact selected sequence retained for Operate.";
    return result;
}

ControlActionResult ControlObjectSession::operate(
    const ControlRequest& request,
    const std::stop_token stop_token) {
    if (!descriptor_.operationally_ready()) {
        return unsupported(ControlAction::operate, "Control descriptor is not operationally ready.");
    }
    if (request.operate_at.has_value() && !descriptor_.supports_time_activated_operate) {
        return unsupported(ControlAction::operate,
            "Time-activated operation requested but live Oper has no operTm.");
    }
    if (!transport_.associated()) {
        return rejected(ControlAction::operate, "MMS association is not active.");
    }

    if (descriptor_.requires_select() && !active_context_.has_value()) {
        if (expired_context_.has_value()) {
            try {
                const auto rebound = MmsControlStructureBuilder::bind_control_value(
                    request.control_value, descriptor_.ctl_val_specification);
                if (rebound.success() &&
                    mms::MmsDataCodec::encode(rebound.value.value()) ==
                        expired_context_->encoded_ctl_value) {
                    const auto number = expired_context_->mms.control_number;
                    expired_context_.reset();
                    ControlActionResult result;
                    result.action = ControlAction::operate;
                    result.completion = ControlCompletionState::timed_out;
                    result.control_number = number;
                    result.message = "SBO selection timeout expired before Operate.";
                    return result;
                }
            } catch (...) {
            }
            expired_context_.reset();
        }
        if (!request.auto_select) {
            return rejected(ControlAction::operate,
                "Object is not selected and auto_select=false.");
        }
        const auto selected = descriptor_.model ==
                ControlModel::select_before_operate_enhanced
            ? select_with_value(request, stop_token)
            : select(request, stop_token);
        if (!selected.success()) {
            return selected;
        }
    }

    ActiveContext context = active_context_.has_value()
        ? active_context_.value()
        : create_context(request);

    if (active_context_.has_value() && selection_expired(context)) {
        expired_context_ = context;
        best_effort_cancel();
        ControlActionResult result;
        result.action = ControlAction::operate;
        result.completion = ControlCompletionState::timed_out;
        result.control_number = context.mms.control_number;
        result.message = "SBO selection timeout expired before Operate.";
        return result;
    }

    const auto incoming_bound = MmsControlStructureBuilder::bind_control_value(
        request.control_value, descriptor_.ctl_val_specification);
    if (!incoming_bound.success()) {
        best_effort_cancel();
        return rejected(ControlAction::operate,
            "Operate ctlVal no longer matches the selected live type.",
            context.mms.control_number);
    }
    const auto incoming_encoded = mms::MmsDataCodec::encode(incoming_bound.value.value());
    const auto planner_decision = planner_.operate(
        {transport_.association_id()},
        sequence_view(request, context, incoming_encoded),
        0U);
    if (!planner_decision.accepted()) {
        best_effort_cancel();
        return from_planner_rejection(planner_decision);
    }

    const auto oper_value = MmsControlStructureBuilder::build_operate(
        context.mms,
        descriptor_.operate_specification,
        options_.require_exact_named_control_fields);
    if (!oper_value.success()) {
        clear_selection();
        return rejected(ControlAction::operate,
            "Cannot construct exact live Oper structure at " + oper_value.path + ".",
            context.mms.control_number);
    }

    transport_.clear_information_reports();
    ControlTransportWriteResult write;
    try {
        write = transport_.write(
            object_name(descriptor_.object, "CO", "Oper"),
            oper_value.value.value(), stop_token);
    } catch (const mms::MmsTransportCancelledError&) {
        clear_selection();
        ControlActionResult result;
        result.action = ControlAction::operate;
        result.completion = ControlCompletionState::cancelled;
        result.control_number = context.mms.control_number;
        result.message = "Control operation cancelled before confirmed Write completion.";
        return result;
    }

    if (!write.success) {
        const auto app_error = wait_for_application_error(
            options_.application_error_grace_period, stop_token);
        clear_selection();
        return write_failure(ControlAction::operate, context, write, app_error);
    }

    if (!descriptor_.enhanced()) {
        clear_selection();
        ControlActionResult result;
        result.action = ControlAction::operate;
        result.completion = ControlCompletionState::accepted;
        result.request_accepted = true;
        result.control_number = context.mms.control_number;
        result.message = "Operate service accepted (normal-security completion boundary).";
        return result;
    }

    const auto termination = wait_for_termination(
        effective_operate_timeout(request), stop_token);
    if (!termination.has_value()) {
        const bool still_associated = transport_.associated();
        clear_selection();
        ControlActionResult result;
        result.action = ControlAction::operate;
        result.completion = still_associated
            ? ControlCompletionState::timed_out
            : ControlCompletionState::association_lost;
        result.request_accepted = true;
        result.control_number = context.mms.control_number;
        result.message = still_associated
            ? "Operate was accepted, but no correlated CommandTermination arrived before timeout."
            : "MMS association was lost while waiting for CommandTermination.";
        return result;
    }

    const auto planner_completion = planner_.command_termination(
        termination->control_error, termination->add_cause);
    clear_selection();
    ControlActionResult result;
    result.action = ControlAction::operate;
    result.completion = termination->positive
        ? ControlCompletionState::positive_termination
        : ControlCompletionState::negative_termination;
    result.request_accepted = true;
    result.command_termination_received = true;
    result.positive_termination = termination->positive;
    result.control_number = context.mms.control_number;
    result.control_error = termination->control_error;
    result.add_cause = termination->add_cause;
    result.raw_control_error = termination->raw_control_error;
    result.raw_add_cause = termination->raw_add_cause;
    result.message = termination->positive
        ? "Positive CommandTermination received."
        : "Negative CommandTermination / LastApplError received.";
    result.diagnostics.push_back(
        "planner_completion=" + std::to_string(static_cast<unsigned>(planner_completion.status)));
    return result;
}

ControlActionResult ControlObjectSession::cancel(
    const std::stop_token stop_token) {
    if (!active_context_.has_value()) {
        return rejected(ControlAction::cancel,
            "No active selection exists for this control session.");
    }
    if (!descriptor_.cancel_specification.has_value()) {
        clear_selection();
        return unsupported(ControlAction::cancel,
            "IED did not expose a decodable Cancel TypeSpecification.");
    }

    const auto context = active_context_.value();
    const auto planner_decision = planner_.cancel(
        {transport_.association_id()}, 0U);
    if (!planner_decision.accepted()) {
        return from_planner_rejection(planner_decision);
    }

    const auto cancel_value = MmsControlStructureBuilder::build_cancel(
        context.mms,
        descriptor_.cancel_specification.value(),
        options_.require_exact_named_control_fields);
    if (!cancel_value.success()) {
        clear_selection();
        return rejected(ControlAction::cancel,
            "Cannot construct exact live Cancel structure at " + cancel_value.path + ".",
            context.mms.control_number);
    }

    const auto write = transport_.write(
        object_name(descriptor_.object, "CO", "Cancel"),
        cancel_value.value.value(), stop_token);
    clear_selection();
    if (!write.success) {
        return write_failure(ControlAction::cancel, context, write);
    }

    ControlActionResult result;
    result.action = ControlAction::cancel;
    result.completion = ControlCompletionState::accepted;
    result.request_accepted = true;
    result.control_number = context.mms.control_number;
    result.message = "Cancel accepted; local SBO ownership released.";
    return result;
}

ControlActionResult ControlObjectSession::write_failure(
    const ControlAction action,
    const ActiveContext& context,
    const ControlTransportWriteResult& write,
    const std::optional<CommandTermination>& app_error) const {
    ControlActionResult result;
    result.action = action;
    result.completion = ControlCompletionState::rejected;
    result.control_number = context.mms.control_number;
    result.mms_failure_code = write.failure_code;
    result.message = "MMS control Write was rejected.";
    if (app_error.has_value()) {
        result.control_error = app_error->control_error;
        result.add_cause = app_error->add_cause;
        result.raw_control_error = app_error->raw_control_error;
        result.raw_add_cause = app_error->raw_add_cause;
        result.diagnostics.push_back(
            app_error->control_error_name + "/" + app_error->add_cause_name);
    }
    return result;
}

std::optional<CommandTermination> ControlObjectSession::wait_for_termination(
    const std::chrono::milliseconds timeout,
    const std::stop_token stop_token) {
    if (timeout <= std::chrono::milliseconds::zero()) {
        return std::nullopt;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (transport_.associated()) {
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::nullopt;
        }
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        if (remaining <= std::chrono::milliseconds::zero()) {
            remaining = std::chrono::milliseconds{1};
        }
        mms::MmsInformationReport report;
        if (!transport_.wait_information_report(remaining, report, stop_token)) {
            return std::nullopt;
        }
        auto termination = CommandTerminationDecoder::decode(report, descriptor_.object);
        if (termination.is_for_control_object && termination.is_termination) {
            return termination;
        }
    }
    return std::nullopt;
}

std::optional<CommandTermination> ControlObjectSession::wait_for_application_error(
    const std::chrono::milliseconds timeout,
    const std::stop_token stop_token) {
    return wait_for_termination(timeout, stop_token);
}

bool ControlObjectSession::selection_expired(const ActiveContext& context) const noexcept {
    return std::chrono::steady_clock::now() - context.created > descriptor_.sbo_timeout;
}

std::chrono::milliseconds ControlObjectSession::effective_operate_timeout(
    const ControlRequest& request) const {
    auto timeout = request.command_termination_timeout.value_or(
        descriptor_.operate_timeout);
    if (timeout <= std::chrono::milliseconds::zero()) {
        timeout = descriptor_.operate_timeout;
    }
    if (!request.operate_at.has_value()) {
        return timeout;
    }
    const auto now = std::chrono::system_clock::now();
    if (request.operate_at.value() <= now) {
        return timeout;
    }
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        request.operate_at.value() - now) + timeout;
}

void ControlObjectSession::best_effort_cancel() noexcept {
    if (!active_context_.has_value()) {
        return;
    }
    const auto context = active_context_.value();
    try {
        if (descriptor_.cancel_specification.has_value() && transport_.associated()) {
            const auto value = MmsControlStructureBuilder::build_cancel(
                context.mms,
                descriptor_.cancel_specification.value(),
                options_.require_exact_named_control_fields);
            if (value.success()) {
                static_cast<void>(transport_.write(
                    object_name(descriptor_.object, "CO", "Cancel"),
                    value.value.value(), {}));
            }
        }
    } catch (...) {
    }
    clear_selection();
}

void ControlObjectSession::clear_selection() noexcept {
    planner_.on_association_closed(transport_.association_id());
    active_context_.reset();
}

void ControlObjectSession::on_association_closed() noexcept {
    if (active_context_.has_value()) {
        expired_context_.reset();
    }
    planner_.on_association_closed(transport_.association_id());
    active_context_.reset();
}

} // namespace ar::iec61850::control
