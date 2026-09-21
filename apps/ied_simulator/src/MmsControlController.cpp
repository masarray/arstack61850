// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsControlController.hpp"

#include "ariec61850/control/control_session.hpp"
#include "ariec61850/mms/live_discovery.hpp"

#include <QByteArray>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace control = ar::iec61850::control;
namespace mms = ar::iec61850::mms;

namespace {

bool authorizeExplicitOperatorAction(
    void*,
    control::ControlAction,
    const control::ControlObjectReference&,
    const control::ControlClientIdentity&,
    const control::ControlSequenceView&) noexcept {
    // The GUI performs an explicit modal confirmation before invoking the
    // controller. This callback exists only to cross the guarded-planner
    // authorization boundary; there is intentionally no background caller.
    return true;
}

QString modelName(const control::ControlModel model) {
    switch (model) {
    case control::ControlModel::status_only: return QStringLiteral("status-only");
    case control::ControlModel::direct_normal: return QStringLiteral("direct-normal");
    case control::ControlModel::select_before_operate_normal: return QStringLiteral("sbo-normal");
    case control::ControlModel::direct_enhanced: return QStringLiteral("direct-enhanced");
    case control::ControlModel::select_before_operate_enhanced: return QStringLiteral("sbo-enhanced");
    case control::ControlModel::unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString completionName(const control::ControlCompletionState state) {
    switch (state) {
    case control::ControlCompletionState::accepted: return QStringLiteral("accepted");
    case control::ControlCompletionState::positive_termination: return QStringLiteral("positive-termination");
    case control::ControlCompletionState::negative_termination: return QStringLiteral("negative-termination");
    case control::ControlCompletionState::rejected: return QStringLiteral("rejected");
    case control::ControlCompletionState::unsupported: return QStringLiteral("unsupported");
    case control::ControlCompletionState::timed_out: return QStringLiteral("timed-out");
    case control::ControlCompletionState::association_lost: return QStringLiteral("association-lost");
    case control::ControlCompletionState::cancelled: return QStringLiteral("cancelled");
    }
    return QStringLiteral("unknown");
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool parseSwitch(const std::string& value) {
    const auto normalized = lower(value);
    if (normalized == "true" || normalized == "1" || normalized == "on" ||
        normalized == "yes") {
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "off" ||
        normalized == "no") {
        return false;
    }
    throw std::invalid_argument("Boolean control value requires true/false, on/off, or 1/0.");
}

control::ControlValue parseControlValue(
    const QString& rawKind,
    const QString& rawValue,
    const QString& rawCdc) {
    auto kind = lower(rawKind.trimmed().toStdString());
    const auto value = rawValue.trimmed().toStdString();
    const auto cdc = lower(rawCdc.trimmed().toStdString());

    if (kind.empty() || kind == "auto") {
        if (cdc == "spc") kind = "bool";
        else if (cdc == "dpc") kind = "dpc";
        else if (cdc == "apc") kind = "float";
        else if (cdc == "bsc") kind = "step";
        else if (cdc == "inc" || cdc == "isc" || cdc == "inc/isc") kind = "int";
        else {
            throw std::invalid_argument(
                "Cannot infer a safe ctlVal type for this CDC; choose an explicit value type.");
        }
    }

    if (kind == "bool" || kind == "boolean") {
        return control::ControlValue::boolean(parseSwitch(value));
    }
    if (kind == "dpc" || kind == "double-point") {
        const auto normalized = lower(value);
        if (normalized == "off" || normalized == "1") {
            return control::ControlValue::double_point(control::DoublePointValue::off);
        }
        if (normalized == "on" || normalized == "2") {
            return control::ControlValue::double_point(control::DoublePointValue::on);
        }
        if (normalized == "intermediate" || normalized == "0") {
            return control::ControlValue::double_point(control::DoublePointValue::intermediate);
        }
        if (normalized == "bad" || normalized == "3") {
            return control::ControlValue::double_point(control::DoublePointValue::bad);
        }
        throw std::invalid_argument("DPC control value requires off, on, intermediate, or bad.");
    }
    if (kind == "int" || kind == "integer") {
        std::size_t consumed{};
        const auto parsed = std::stoll(value, &consumed, 0);
        if (consumed != value.size()) throw std::invalid_argument("Invalid signed integer control value.");
        return control::ControlValue::integer(static_cast<std::int64_t>(parsed));
    }
    if (kind == "uint" || kind == "unsigned") {
        std::size_t consumed{};
        const auto parsed = std::stoull(value, &consumed, 0);
        if (consumed != value.size()) throw std::invalid_argument("Invalid unsigned control value.");
        return control::ControlValue::unsigned_integer(static_cast<std::uint64_t>(parsed));
    }
    if (kind == "float" || kind == "double") {
        std::size_t consumed{};
        const auto parsed = std::stod(value, &consumed);
        if (consumed != value.size()) throw std::invalid_argument("Invalid floating-point control value.");
        return control::ControlValue::floating_point(parsed);
    }
    if (kind == "step") {
        const auto colon = value.find(':');
        const auto positionText = value.substr(0U, colon);
        std::size_t consumed{};
        const auto parsed = std::stoll(positionText, &consumed, 0);
        if (consumed != positionText.size()) throw std::invalid_argument("Invalid step-position control value.");
        bool transient{};
        if (colon != std::string::npos) transient = parseSwitch(value.substr(colon + 1U));
        return control::ControlValue::step_position(
            {static_cast<std::int64_t>(parsed), transient});
    }
    throw std::invalid_argument(
        "Control value type must be auto, bool, dpc, int, uint, float, or step.");
}

control::OriginCategory originCategory(const int value) {
    if (value < static_cast<int>(control::OriginCategory::not_supported) ||
        value > static_cast<int>(control::OriginCategory::process)) {
        throw std::invalid_argument("Origin category must be in IEC 61850 range 0..8.");
    }
    return static_cast<control::OriginCategory>(value);
}

QVariantMap resultMap(const control::ControlActionResult& result) {
    QVariantMap map;
    map.insert(QStringLiteral("completion"), completionName(result.completion));
    map.insert(QStringLiteral("success"), result.success());
    map.insert(QStringLiteral("requestAccepted"), result.request_accepted);
    map.insert(QStringLiteral("commandTerminationReceived"), result.command_termination_received);
    map.insert(QStringLiteral("positiveTermination"), result.positive_termination);
    map.insert(QStringLiteral("controlNumber"), result.control_number);
    map.insert(QStringLiteral("controlError"), static_cast<int>(result.control_error));
    map.insert(QStringLiteral("addCause"), static_cast<int>(result.add_cause));
    map.insert(QStringLiteral("rawControlError"), static_cast<qlonglong>(result.raw_control_error));
    map.insert(QStringLiteral("rawAddCause"), static_cast<qlonglong>(result.raw_add_cause));
    if (result.mms_failure_code.has_value()) {
        map.insert(QStringLiteral("mmsFailureCode"), static_cast<qulonglong>(*result.mms_failure_code));
    }
    map.insert(QStringLiteral("message"), QString::fromStdString(result.message));
    QStringList diagnostics;
    for (const auto& item : result.diagnostics) diagnostics.push_back(QString::fromStdString(item));
    map.insert(QStringLiteral("diagnostics"), diagnostics);
    return map;
}

} // namespace

struct MmsControlController::WorkerState final {
    std::unique_ptr<mms::MmsTcpLiveDiscoverySession> session;
    std::unique_ptr<control::MmsAssociationControlTransport> transport;
    std::unique_ptr<control::ControlObjectSession> controlSession;
    std::string objectReference;
};

MmsControlController::MmsControlController(QObject* parent)
    : QObject(parent),
      workerState_(std::make_shared<WorkerState>()),
      stopSource_(std::make_shared<std::stop_source>()) {
    ioPool_.setMaxThreadCount(1);
}

MmsControlController::~MmsControlController() {
    disconnectFromIed();
    ioPool_.waitForDone();
}

void MmsControlController::setHost(const QString& value) {
    const auto normalized = value.trimmed();
    if (normalized.isEmpty() || normalized == host_ || connected_ || busy_) return;
    host_ = normalized;
    emit configurationChanged();
}

void MmsControlController::setPort(const int value) {
    if (value < 1 || value > 65'535 || value == port_ || connected_ || busy_) return;
    port_ = value;
    emit configurationChanged();
}

void MmsControlController::setEngineeringContext(IedEngineeringContextController* value) {
    if (engineeringContext_ == value) return;
    engineeringContext_ = value;
    emit engineeringContextChanged();
}

void MmsControlController::resetDescriptorUi() {
    prepared_ = false;
    objectReference_.clear();
    modelName_.clear();
    cdc_.clear();
    requiresSelect_ = false;
    enhanced_ = false;
    activeSelection_ = false;
    supportsCancel_ = false;
    supportsTimeActivated_ = false;
    supportsCommandTermination_ = false;
    discoveryEvidence_.clear();
}

std::shared_ptr<std::stop_source> MmsControlController::replaceStopSource() {
    if (stopSource_) stopSource_->request_stop();
    stopSource_ = std::make_shared<std::stop_source>();
    return stopSource_;
}

bool MmsControlController::prepareObject(const QString& objectReference) {
    const auto reference = objectReference.trimmed();
    if (reference.isEmpty() || busy_) return false;
    if (!engineeringContext_ || !engineeringContext_->loaded() || engineeringContext_->selectionRequired()) {
        lastError_ = QStringLiteral("A single active canonical IED engineering context is required.");
        emit stateChanged();
        return false;
    }
    if (prepared_ && connected_ && objectReference_ == reference) return true;

    const auto stop = replaceStopSource();
    const auto generation = ++generation_;
    const auto host = host_;
    const auto port = port_;
    const QPointer<MmsControlController> self{this};
    const auto worker = workerState_;

    busy_ = true;
    lastError_.clear();
    lastStatus_ = QStringLiteral("Preparing control object…");
    resetDescriptorUi();
    emit stateChanged();

    ioPool_.start([self, worker, stop, generation, host, port, reference] {
        try {
            if (worker->session) worker->session->disconnect(stop->get_token());
            worker->controlSession.reset();
            worker->transport.reset();
            worker->session.reset();
            worker->objectReference = reference.toStdString();

            mms::MmsAssociationOptions associationOptions;
            associationOptions.connect_timeout = std::chrono::milliseconds{5'000};
            associationOptions.request_timeout = std::chrono::milliseconds{5'000};
            auto session = std::make_unique<mms::MmsTcpLiveDiscoverySession>(
                mms::TcpMmsTransportOptions{}, associationOptions);
            mms::MmsEndpoint endpoint;
            endpoint.host = host.toStdString();
            endpoint.port = static_cast<std::uint16_t>(port);
            session->connect(endpoint, stop->get_token());

            auto transport = std::make_unique<control::MmsAssociationControlTransport>(
                session->association());
            const auto descriptor = control::ControlDescriptorDiscovery::discover(
                *transport, worker->objectReference, {}, stop->get_token());

            control::ControlSessionOptions options;
            options.guarded_policy.authorize = authorizeExplicitOperatorAction;
            options.guarded_policy.sbo_timeout_ms =
                descriptor.sbo_timeout.count() > 0
                    ? static_cast<std::uint64_t>(descriptor.sbo_timeout.count())
                    : 10'000U;

            auto controlSession = std::make_unique<control::ControlObjectSession>(
                *transport, descriptor, options);

            QStringList evidence;
            for (const auto& item : descriptor.discovery_evidence) {
                evidence.push_back(QString::fromStdString(item));
            }
            const auto uiModel = modelName(descriptor.model);
            const auto uiCdc = QString::fromStdString(descriptor.cdc);
            const auto requiresSelect = descriptor.requires_select();
            const auto enhanced = descriptor.enhanced();
            const auto supportsCancel = descriptor.cancel_specification.has_value();
            const auto supportsTime = descriptor.supports_time_activated_operate;
            const auto supportsTermination = descriptor.supports_command_termination;

            worker->session = std::move(session);
            worker->transport = std::move(transport);
            worker->controlSession = std::move(controlSession);

            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, reference, uiModel, uiCdc,
                                                 requiresSelect, enhanced, supportsCancel,
                                                 supportsTime, supportsTermination, evidence] {
                    if (!self || self->generation_ != generation) return;
                    self->busy_ = false;
                    self->connected_ = true;
                    self->prepared_ = true;
                    self->objectReference_ = reference;
                    self->modelName_ = uiModel;
                    self->cdc_ = uiCdc;
                    self->requiresSelect_ = requiresSelect;
                    self->enhanced_ = enhanced;
                    self->supportsCancel_ = supportsCancel;
                    self->supportsTimeActivated_ = supportsTime;
                    self->supportsCommandTermination_ = supportsTermination;
                    self->discoveryEvidence_ = evidence;
                    self->lastStatus_ = QStringLiteral("Control object ready · %1 · %2")
                        .arg(uiModel, uiCdc);
                    self->lastError_.clear();
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            worker->controlSession.reset();
            worker->transport.reset();
            if (worker->session) worker->session->disconnect();
            worker->session.reset();
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->busy_ = false;
                    self->connected_ = false;
                    self->resetDescriptorUi();
                    self->lastError_ = message;
                    self->lastStatus_ = QStringLiteral("Control preparation failed");
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

bool MmsControlController::startAction(
    const RequestedAction action,
    const QString& valueKind,
    const QString& value,
    const int originCategoryValue,
    const QString& originIdentifier,
    const bool test,
    const bool interlockCheck,
    const bool synchroCheck,
    const bool autoSelect) {
    if (busy_ || !connected_ || !prepared_) return false;
    if (action == RequestedAction::cancel && !activeSelection_) {
        lastError_ = QStringLiteral("No active SBO selection exists to cancel.");
        emit stateChanged();
        return false;
    }

    const auto originBytes = originIdentifier.toUtf8();
    if (originBytes.size() > 64) {
        lastError_ = QStringLiteral("Origin identifier exceeds the bounded 64-byte IEC 61850 control limit.");
        emit stateChanged();
        return false;
    }

    const auto stop = stopSource_;
    const auto generation = generation_;
    const auto worker = workerState_;
    const auto cdc = cdc_;
    const QPointer<MmsControlController> self{this};

    busy_ = true;
    lastError_.clear();
    lastStatus_ = QStringLiteral("Control action in progress…");
    emit stateChanged();

    ioPool_.start([self, worker, stop, generation, action, valueKind, value, cdc,
                   originCategoryValue, originBytes, test, interlockCheck,
                   synchroCheck, autoSelect] {
        try {
            if (!worker->session || !worker->session->associated() || !worker->controlSession) {
                throw std::runtime_error("Browser control association is not active.");
            }

            control::ControlRequest request;
            if (action != RequestedAction::cancel) {
                request.control_value = parseControlValue(valueKind, value, cdc);
                request.origin_category = originCategory(originCategoryValue);
                request.origin_identifier.assign(
                    reinterpret_cast<const std::uint8_t*>(originBytes.constData()),
                    reinterpret_cast<const std::uint8_t*>(originBytes.constData()) + originBytes.size());
                request.test = test;
                request.interlock_check = interlockCheck;
                request.synchro_check = synchroCheck;
                request.auto_select = autoSelect;
            }

            control::ControlActionResult result;
            switch (action) {
            case RequestedAction::select:
                if (!worker->controlSession->descriptor().requires_select()) {
                    throw std::invalid_argument("Select is valid only for an SBO control model.");
                }
                result = worker->controlSession->descriptor().model ==
                        control::ControlModel::select_before_operate_enhanced
                    ? worker->controlSession->select_with_value(request, stop->get_token())
                    : worker->controlSession->select(request, stop->get_token());
                break;
            case RequestedAction::operate:
                result = worker->controlSession->operate(request, stop->get_token());
                break;
            case RequestedAction::selectAndOperate: {
                if (!worker->controlSession->descriptor().requires_select()) {
                    throw std::invalid_argument("Select + Operate is valid only for an SBO control model.");
                }
                auto selected = worker->controlSession->descriptor().model ==
                        control::ControlModel::select_before_operate_enhanced
                    ? worker->controlSession->select_with_value(request, stop->get_token())
                    : worker->controlSession->select(request, stop->get_token());
                if (!selected.success()) {
                    result = std::move(selected);
                    break;
                }
                request.auto_select = false;
                result = worker->controlSession->operate(request, stop->get_token());
                break;
            }
            case RequestedAction::cancel:
                result = worker->controlSession->cancel(stop->get_token());
                break;
            }

            const auto map = resultMap(result);
            const auto selectionActive = worker->controlSession->has_active_selection();
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, map, selectionActive] {
                    if (!self || self->generation_ != generation) return;
                    self->busy_ = false;
                    self->activeSelection_ = selectionActive;
                    self->lastResult_ = map;
                    self->lastStatus_ = map.value(QStringLiteral("message")).toString();
                    self->lastError_ = map.value(QStringLiteral("success")).toBool()
                        ? QString{}
                        : self->lastStatus_;
                    emit self->resultChanged();
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->busy_ = false;
                    self->lastError_ = message;
                    self->lastStatus_ = QStringLiteral("Control action rejected");
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

bool MmsControlController::select(
    const QString& valueKind,
    const QString& value,
    const int originCategoryValue,
    const QString& originIdentifier,
    const bool test,
    const bool interlockCheck,
    const bool synchroCheck) {
    return startAction(
        RequestedAction::select, valueKind, value, originCategoryValue,
        originIdentifier, test, interlockCheck, synchroCheck, false);
}

bool MmsControlController::operate(
    const QString& valueKind,
    const QString& value,
    const int originCategoryValue,
    const QString& originIdentifier,
    const bool test,
    const bool interlockCheck,
    const bool synchroCheck,
    const bool autoSelect) {
    return startAction(
        RequestedAction::operate, valueKind, value, originCategoryValue,
        originIdentifier, test, interlockCheck, synchroCheck, autoSelect);
}

bool MmsControlController::selectAndOperate(
    const QString& valueKind,
    const QString& value,
    const int originCategoryValue,
    const QString& originIdentifier,
    const bool test,
    const bool interlockCheck,
    const bool synchroCheck) {
    return startAction(
        RequestedAction::selectAndOperate, valueKind, value, originCategoryValue,
        originIdentifier, test, interlockCheck, synchroCheck, false);
}

bool MmsControlController::cancel() {
    return startAction(
        RequestedAction::cancel, {}, {}, static_cast<int>(control::OriginCategory::station_control),
        {}, false, false, false, false);
}

void MmsControlController::disconnectFromIed() {
    const auto stop = replaceStopSource();
    const auto generation = ++generation_;
    connected_ = false;
    busy_ = false;
    resetDescriptorUi();
    lastError_.clear();
    lastStatus_ = QStringLiteral("Control association disconnected");
    emit stateChanged();

    const auto worker = workerState_;
    const QPointer<MmsControlController> self{this};
    ioPool_.start([self, worker, stop, generation] {
        if (worker->controlSession) worker->controlSession->on_association_closed();
        worker->controlSession.reset();
        worker->transport.reset();
        if (worker->session) worker->session->disconnect(stop->get_token());
        worker->session.reset();
        worker->objectReference.clear();
        if (self) {
            QMetaObject::invokeMethod(self, [self, generation] {
                if (!self || self->generation_ != generation) return;
                emit self->stateChanged();
            }, Qt::QueuedConnection);
        }
    });
}
