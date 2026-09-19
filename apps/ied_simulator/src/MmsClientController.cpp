// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsClientController.hpp"
#include "MmsSclClientProjection.hpp"

#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/live_model.hpp"
#include "ariec61850/mms/scl_association.hpp"
#include "ariec61850/mms/scl_sync_health.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/scl/parser.hpp"

#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace mms = ar::iec61850::mms;
namespace scl = ar::iec61850::scl;

namespace {
std::span<const std::uint8_t> responsePayload(const mms::MmsConfirmedExchangeResult& exchange) {
    return exchange.presentation_payload.empty()
        ? exchange.envelope.mms_payload
        : std::span<const std::uint8_t>{exchange.presentation_payload};
}

struct ReadValue final {
    QString key;
    QString display;
};

QVector<ReadValue> readTargets(
    mms::MmsAssociationRuntime& association,
    const QVector<MmsLiveTreeModel::ReadTarget>& targets,
    const std::stop_token stopToken) {
    if (targets.isEmpty()) return {};
    const auto invokeId = association.next_invoke_id();
    mms::MmsReadRequest request;
    request.invoke_id = invokeId;
    request.variables.reserve(static_cast<std::size_t>(targets.size()));
    for (const auto& target : targets) {
        request.variables.push_back(mms::MmsObjectName::domain_specific(
            target.domain.toStdString(), target.item.toStdString()));
    }
    const auto encoded = mms::MmsServiceCodec::encode_read_request_p_data(
        request, association.negotiated().presentation_context_id);
    const auto exchange = association.exchange_confirmed(encoded, invokeId, stopToken);
    if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
        throw std::runtime_error("MMS Read did not return Confirmed-Response.");
    }
    const auto response = mms::MmsServiceCodec::decode_read_response(
        responsePayload(exchange), invokeId);
    if (response.results.size() != static_cast<std::size_t>(targets.size())) {
        throw std::runtime_error("MMS Read result count does not match requested variables.");
    }
    QVector<ReadValue> values;
    values.reserve(targets.size());
    for (int index = 0; index < targets.size(); ++index) {
        const auto& result = response.results[static_cast<std::size_t>(index)];
        if (!result.success()) continue;
        values.push_back({
            targets.at(index).key,
            QString::fromStdString(mms::MmsDataCodec::to_display_string(*result.value))});
    }
    if (values.isEmpty()) throw std::runtime_error("MMS Read returned no successful AccessResult.");
    return values;
}

mms::MmsDataValue parseWriteValue(
    const MmsLiveTreeModel::Selection& selection,
    const QString& text) {
    const auto type = selection.mmsType.trimmed().toLower();
    const auto value = text.trimmed();
    if (type == QStringLiteral("boolean")) {
        if (value.compare(QStringLiteral("true"), Qt::CaseInsensitive) == 0 ||
            value == QStringLiteral("1") || value.compare(QStringLiteral("on"), Qt::CaseInsensitive) == 0) {
            return mms::MmsDataValue::boolean(true);
        }
        if (value.compare(QStringLiteral("false"), Qt::CaseInsensitive) == 0 ||
            value == QStringLiteral("0") || value.compare(QStringLiteral("off"), Qt::CaseInsensitive) == 0) {
            return mms::MmsDataValue::boolean(false);
        }
        throw std::invalid_argument("Boolean Write requires true/false, 1/0 or on/off.");
    }
    if (type == QStringLiteral("integer")) {
        bool ok{};
        const auto parsed = value.toLongLong(&ok, 0);
        if (!ok) throw std::invalid_argument("Integer Write value is invalid.");
        return mms::MmsDataValue::integer(parsed);
    }
    if (type == QStringLiteral("unsigned")) {
        bool ok{};
        const auto parsed = value.toULongLong(&ok, 0);
        if (!ok) throw std::invalid_argument("Unsigned Write value is invalid.");
        return mms::MmsDataValue::unsigned_integer(parsed);
    }
    if (type == QStringLiteral("floating-point")) {
        bool ok{};
        const auto parsed = value.toDouble(&ok);
        if (!ok) throw std::invalid_argument("Floating-point Write value is invalid.");
        if (selection.sclType.compare(QStringLiteral("FLOAT64"), Qt::CaseInsensitive) == 0) {
            return mms::MmsDataValue::floating_point(parsed);
        }
        return mms::MmsDataValue::floating_point(static_cast<float>(parsed));
    }
    if (type == QStringLiteral("visible-string")) {
        return mms::MmsDataValue::visible_string(text.toStdString());
    }
    if (type == QStringLiteral("mms-string")) {
        return mms::MmsDataValue::mms_string(text.toStdString());
    }
    throw std::invalid_argument("Selected exact MMS type is not in the guarded scalar Write set.");
}

QString snapshotKey(const arstack::iedsim::SclSnapshotValue& value) {
    return QString::fromStdString(value.domain) + QLatin1Char('\x1f') +
        QString::fromStdString(value.item);
}
} // namespace

struct MmsClientController::WorkerState final {
    std::unique_ptr<mms::MmsTcpLiveDiscoverySession> session;
    std::shared_ptr<mms::MmsLiveDiscoveryResult> discovery;
    std::shared_ptr<scl::SclDocument> trustedScl;
    std::shared_ptr<mms::MmsSclAssistedConnectResult> sclSnapshot;
};

MmsClientController::MmsClientController(QObject* parent)
    : QObject(parent), treeModel_(this), workerState_(std::make_shared<WorkerState>()),
      stopSource_(std::make_shared<std::stop_source>()) {
    ioPool_.setMaxThreadCount(1);
    ioPool_.setExpiryTimeout(-1);
}

MmsClientController::~MmsClientController() {
    if (stopSource_) stopSource_->request_stop();
    ioPool_.clear();
    ioPool_.waitForDone();
}

void MmsClientController::setHost(const QString& value) {
    const auto normalized = value.trimmed();
    if (host_ == normalized) return;
    host_ = normalized;
    emit configurationChanged();
}

void MmsClientController::setPort(const int value) {
    if (value < 1 || value > 65'535 || port_ == value) return;
    port_ = value;
    emit configurationChanged();
}

void MmsClientController::setTrustedSclPath(const QString& value) {
    const auto normalized = value.trimmed();
    if (trustedSclPath_ == normalized) return;
    trustedSclPath_ = normalized;
    if (trustedSclPath_.isEmpty()) {
        appendDiagnostic(QStringLiteral(
            "Trusted SCL source cleared; next Connect will use full live discovery."));
    } else {
        appendDiagnostic(QStringLiteral("Trusted SCL source armed · %1").arg(trustedSclPath_));
    }
    emit configurationChanged();
}

bool MmsClientController::connected() const noexcept { return state_ == State::connected; }

bool MmsClientController::busy() const noexcept {
    return state_ == State::connecting || state_ == State::discovering || operationBusy_;
}

QString MmsClientController::stateText() const {
    switch (state_) {
    case State::disconnected: return QStringLiteral("Disconnected");
    case State::connecting: return QStringLiteral("Connecting");
    case State::discovering: return trustedSclAvailable()
        ? QStringLiteral("Synchronizing SCL")
        : QStringLiteral("Discovering");
    case State::connected: return operationBusy_ ? QStringLiteral("Working") : QStringLiteral("Connected");
    case State::faulted: return QStringLiteral("Faulted");
    }
    return {};
}

QString MmsClientController::endpoint() const {
    return host_ + QLatin1Char(':') + QString::number(port_);
}

QString MmsClientController::lastDiagnostic() const {
    return diagnostics_.isEmpty() ? QString{} : diagnostics_.constLast();
}

void MmsClientController::appendDiagnostic(const QString& text) {
    if (text.isEmpty()) return;
    constexpr auto maximumDiagnostics = 128;
    while (diagnostics_.size() >= maximumDiagnostics) diagnostics_.removeFirst();
    diagnostics_.push_back(text);
    emit diagnosticsChanged();
}

QString MmsClientController::diagnosticsText() const { return diagnostics_.join(QLatin1Char('\n')); }

void MmsClientController::clearModelState() {
    treeModel_.clear();
    iedName_.clear();
    modelSummary_.clear();
    modelSource_.clear();
    associationProfile_.clear();
    logicalDeviceCount_ = 0;
    logicalNodeCount_ = 0;
    dataObjectCount_ = 0;
    dataAttributeCount_ = 0;
    emit modelChanged();
}

void MmsClientController::setOperationBusy(const bool value) {
    if (operationBusy_ == value) return;
    operationBusy_ = value;
    emit stateChanged();
}

std::shared_ptr<std::stop_source> MmsClientController::replaceStopSource() {
    if (stopSource_) stopSource_->request_stop();
    stopSource_ = std::make_shared<std::stop_source>();
    return stopSource_;
}

bool MmsClientController::connectToIed() {
    if (busy() || connected()) return false;
    if (host_.trimmed().isEmpty() || port_ < 1 || port_ > 65'535) {
        lastError_ = QStringLiteral("A non-empty host and TCP port 1..65535 are required.");
        state_ = State::faulted;
        emit stateChanged();
        return false;
    }

    const auto stop = replaceStopSource();
    const auto generation = ++generation_;
    const auto requestedHost = host_.trimmed();
    const auto requestedPort = port_;
    const auto requestedTrustedSclPath = trustedSclPath_.trimmed();
    state_ = State::connecting;
    operationBusy_ = false;
    lastError_.clear();
    clearModelState();
    appendDiagnostic(QStringLiteral("Connect %1:%2 · generation %3 · model source %4")
                         .arg(requestedHost)
                         .arg(requestedPort)
                         .arg(generation)
                         .arg(requestedTrustedSclPath.isEmpty()
                                  ? QStringLiteral("live discovery")
                                  : QStringLiteral("trusted SCL")));
    emit stateChanged();

    const QPointer<MmsClientController> self{this};
    const auto worker = workerState_;
    ioPool_.start([
        self, worker, stop, generation, requestedHost, requestedPort, requestedTrustedSclPath] {
        try {
            if (worker->session) worker->session->disconnect();
            worker->session.reset();
            worker->discovery.reset();
            worker->trustedScl.reset();
            worker->sclSnapshot.reset();

            std::shared_ptr<scl::SclDocument> trustedScl;
            mms::MmsSclAssociationContext sclAssociation;
            if (!requestedTrustedSclPath.isEmpty()) {
                scl::SclParser parser;
                trustedScl = std::make_shared<scl::SclDocument>(
                    parser.load(std::filesystem::path{requestedTrustedSclPath.toStdString()}));
                sclAssociation = mms::resolve_scl_association_context(
                    *trustedScl, requestedHost.toStdString());
            }

            mms::MmsAssociationOptions associationOptions;
            associationOptions.connect_timeout = std::chrono::milliseconds{5'000};
            associationOptions.request_timeout = std::chrono::milliseconds{5'000};
            associationOptions.addressing = sclAssociation.addressing;

            if (self && trustedScl && sclAssociation.selected()) {
                const auto associationText = sclAssociation.uses_engineering_addressing()
                    ? QStringLiteral("SCL engineering OSI addressing")
                    : QStringLiteral("compatibility OSI defaults");
                const auto endpointText = sclAssociation.scl_host.empty()
                    ? QStringLiteral("not specified")
                    : QStringLiteral("%1:%2")
                          .arg(QString::fromStdString(sclAssociation.scl_host))
                          .arg(sclAssociation.scl_port);
                const auto ied = QString::fromStdString(sclAssociation.ied_name);
                const auto ap = QString::fromStdString(sclAssociation.access_point_name);
                QMetaObject::invokeMethod(
                    self,
                    [self, generation, associationText, endpointText, ied, ap] {
                        if (!self || self->generation_ != generation) return;
                        self->appendDiagnostic(
                            QStringLiteral("Trusted SCL association · IED %1 · AP %2 · SCL endpoint %3 · %4")
                                .arg(ied, ap, endpointText, associationText));
                    },
                    Qt::QueuedConnection);
            }

            auto session = std::make_unique<mms::MmsTcpLiveDiscoverySession>(
                mms::TcpMmsTransportOptions{}, associationOptions);
            session->connect(
                {requestedHost.toStdString(), static_cast<std::uint16_t>(requestedPort)},
                stop->get_token());

            if (self) {
                const bool usingTrustedScl = trustedScl != nullptr;
                QMetaObject::invokeMethod(self, [self, generation, usingTrustedScl] {
                    if (!self || self->generation_ != generation) return;
                    self->state_ = State::discovering;
                    self->appendDiagnostic(usingTrustedScl
                        ? QStringLiteral(
                              "Association accepted; validating Domains and reading trusted SCL FC roots.")
                        : QStringLiteral("Association accepted; discovering live model."));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }

            const auto associationProfile =
                QString::fromStdString(session->association().active_association_profile());

            if (trustedScl) {
                auto snapshot = std::make_shared<mms::MmsSclAssistedConnectResult>(
                    session->synchronize_scl(
                        *trustedScl,
                        sclAssociation.ied_name,
                        {},
                        stop->get_token()));
                const auto health =
                    mms::MmsSclSynchronizationHealthClassifier::evaluate(*snapshot);
                const auto healthSummary = QString::fromStdString(health.summary());
                mms::MmsSclSynchronizationHealthClassifier::require_compatible(health);

                auto model = std::make_shared<mms::MmsLiveModelDocument>(
                    arstack::iedsim::build_scl_live_model(*trustedScl, *snapshot));
                auto initialValues = std::make_shared<std::vector<arstack::iedsim::SclSnapshotValue>>(
                    arstack::iedsim::collect_scl_snapshot_values(*trustedScl, *snapshot));

                worker->discovery.reset();
                worker->trustedScl = trustedScl;
                worker->sclSnapshot = snapshot;
                worker->session = std::move(session);

                if (self) {
                    QMetaObject::invokeMethod(
                        self,
                        [self, generation, model, snapshot, initialValues, associationProfile, healthSummary] {
                            if (!self || self->generation_ != generation) return;
                            self->treeModel_.applyDocument(*model);
                            for (const auto& value : *initialValues) {
                                self->treeModel_.applyReadValue(
                                    snapshotKey(value), QString::fromStdString(value.display));
                            }
                            self->iedName_ = QString::fromStdString(model->identity.ied_name);
                            self->modelSummary_ = QString::fromStdString(model->summary);
                            self->modelSource_ = QStringLiteral("Trusted SCL + initial snapshot");
                            self->associationProfile_ = associationProfile;
                            self->logicalDeviceCount_ =
                                static_cast<int>(model->coverage.logical_device_count);
                            self->logicalNodeCount_ =
                                static_cast<int>(model->coverage.logical_node_count);
                            self->dataObjectCount_ =
                                static_cast<int>(model->coverage.data_object_count);
                            self->dataAttributeCount_ =
                                static_cast<int>(model->coverage.data_attribute_count);
                            self->state_ = State::connected;
                            self->lastError_.clear();
                            self->appendDiagnostic(healthSummary);
                            self->appendDiagnostic(
                                QStringLiteral(
                                    "SCL synchronization complete · %1 Domain(s) online · %2 Read(s) · "
                                    "%3 mapped leaf value(s) · missing %4 · extra %5")
                                    .arg(snapshot->domains.online.size())
                                    .arg(snapshot->read_request_count)
                                    .arg(snapshot->mapped_leaf_count)
                                    .arg(snapshot->domains.missing.size())
                                    .arg(snapshot->domains.extra.size()));
                            emit self->modelChanged();
                            emit self->stateChanged();
                        },
                        Qt::QueuedConnection);
                }
                return;
            }

            mms::MmsLiveDiscoveryOptions options;
            options.maximum_variable_type_probes = 8'192U;
            options.maximum_data_set_directories = 4'096U;
            options.maximum_report_control_probes = 1'024U;
            auto discovery = std::make_shared<mms::MmsLiveDiscoveryResult>(
                session->discover(options, stop->get_token()));
            auto model = std::make_shared<mms::MmsLiveModelDocument>(
                mms::MmsLiveModelBuilder::build(*discovery));
            worker->discovery = discovery;
            worker->trustedScl.reset();
            worker->sclSnapshot.reset();
            worker->session = std::move(session);

            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, model, associationProfile] {
                    if (!self || self->generation_ != generation) return;
                    self->treeModel_.applyDocument(*model);
                    self->iedName_ = QString::fromStdString(model->identity.ied_name);
                    self->modelSummary_ = QString::fromStdString(model->summary);
                    self->modelSource_ = QStringLiteral("Live MMS discovery");
                    self->associationProfile_ = associationProfile;
                    self->logicalDeviceCount_ = static_cast<int>(model->coverage.logical_device_count);
                    self->logicalNodeCount_ = static_cast<int>(model->coverage.logical_node_count);
                    self->dataObjectCount_ = static_cast<int>(model->coverage.data_object_count);
                    self->dataAttributeCount_ = static_cast<int>(model->coverage.data_attribute_count);
                    self->state_ = State::connected;
                    self->lastError_.clear();
                    self->appendDiagnostic(
                        QStringLiteral("Discovery complete · %1 LD · %2 LN · %3 DA")
                            .arg(self->logicalDeviceCount_)
                            .arg(self->logicalNodeCount_)
                            .arg(self->dataAttributeCount_));
                    emit self->modelChanged();
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (worker->session) worker->session->disconnect();
            worker->session.reset();
            worker->discovery.reset();
            worker->trustedScl.reset();
            worker->sclSnapshot.reset();
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->state_ = State::faulted;
                    self->lastError_ = message;
                    self->appendDiagnostic(QStringLiteral("Connection failed · %1").arg(message));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

void MmsClientController::disconnectFromIed() {
    const auto previous = stopSource_;
    if (previous) previous->request_stop();
    stopSource_ = std::make_shared<std::stop_source>();
    ++generation_;
    state_ = State::disconnected;
    operationBusy_ = false;
    lastError_.clear();
    clearModelState();
    appendDiagnostic(QStringLiteral("Disconnected · session generation invalidated."));
    emit stateChanged();

    const auto worker = workerState_;
    ioPool_.start([worker] {
        if (worker->session) worker->session->disconnect();
        worker->session.reset();
        worker->discovery.reset();
        worker->trustedScl.reset();
        worker->sclSnapshot.reset();
    });
}

bool MmsClientController::reconnect() {
    disconnectFromIed();
    return connectToIed();
}

void MmsClientController::startRead(
    QVector<MmsLiveTreeModel::ReadTarget> targets,
    QString operationName) {
    if (targets.isEmpty() || !connected() || operationBusy_) return;
    setOperationBusy(true);
    const auto generation = generation_;
    const auto stop = stopSource_;
    const auto worker = workerState_;
    const QPointer<MmsClientController> self{this};
    ioPool_.start([self, worker, stop, generation, targets = std::move(targets), operationName = std::move(operationName)] {
        try {
            if (!worker->session || !worker->session->associated()) {
                throw std::runtime_error("MMS association is not active.");
            }
            const auto values = readTargets(worker->session->association(), targets, stop->get_token());
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, values, operationName] {
                    if (!self || self->generation_ != generation) return;
                    for (const auto& value : values) self->treeModel_.applyReadValue(value.key, value.display);
                    self->setOperationBusy(false);
                    self->lastError_.clear();
                    self->appendDiagnostic(
                        QStringLiteral("%1 complete · %2 value(s)").arg(operationName).arg(values.size()));
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message, operationName] {
                    if (!self || self->generation_ != generation) return;
                    self->setOperationBusy(false);
                    self->lastError_ = message;
                    self->appendDiagnostic(QStringLiteral("%1 failed · %2").arg(operationName).arg(message));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
}

bool MmsClientController::readSelected() {
    if (!connected() || operationBusy_) return false;
    const auto targets = treeModel_.selectedReadTargets();
    if (targets.isEmpty()) return false;
    startRead(targets, QStringLiteral("Read selected"));
    return true;
}

bool MmsClientController::refreshVisible(const int firstRow, const int lastRow) {
    if (!connected() || operationBusy_) return false;
    const auto targets = treeModel_.readTargetsForVisibleRange(firstRow, lastRow, 64);
    if (targets.isEmpty()) return false;
    startRead(targets, QStringLiteral("Refresh visible"));
    return true;
}

bool MmsClientController::writeSelected(const QString& textValue) {
    if (!connected() || operationBusy_) return false;
    const auto selection = treeModel_.selectionSnapshot();
    if (!selection || !selection->writable) {
        lastError_ = QStringLiteral("Selected attribute is not in the guarded writable FC/type set.");
        emit stateChanged();
        return false;
    }

    mms::MmsDataValue value = mms::MmsDataValue::boolean(false);
    try {
        value = parseWriteValue(*selection, textValue);
    } catch (const std::exception& exception) {
        lastError_ = QString::fromUtf8(exception.what());
        emit stateChanged();
        return false;
    }

    setOperationBusy(true);
    const auto generation = generation_;
    const auto stop = stopSource_;
    const auto worker = workerState_;
    const auto target = MmsLiveTreeModel::ReadTarget{
        selection->key, selection->domain, selection->item};
    const auto reference = selection->reference;
    const QPointer<MmsClientController> self{this};
    ioPool_.start([self, worker, stop, generation, target, reference, value = std::move(value)]() mutable {
        try {
            if (!worker->session || !worker->session->associated()) {
                throw std::runtime_error("MMS association is not active.");
            }
            auto& association = worker->session->association();
            const auto invokeId = association.next_invoke_id();
            mms::MmsWriteRequest request;
            request.invoke_id = invokeId;
            request.variables.push_back(mms::MmsObjectName::domain_specific(
                target.domain.toStdString(), target.item.toStdString()));
            request.values.push_back(std::move(value));
            const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
                request, association.negotiated().presentation_context_id);
            const auto exchange = association.exchange_confirmed(encoded, invokeId, stop->get_token());
            if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
                throw std::runtime_error("MMS Write did not return Confirmed-Response.");
            }
            const auto response = mms::MmsServiceCodec::decode_write_response(
                responsePayload(exchange), invokeId);
            if (response.results.size() != 1U || !response.results.front().success) {
                throw std::runtime_error("MMS Write returned a failed AccessResult.");
            }
            const auto values = readTargets(
                association, QVector<MmsLiveTreeModel::ReadTarget>{target}, stop->get_token());
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, reference, values] {
                    if (!self || self->generation_ != generation) return;
                    for (const auto& read : values) self->treeModel_.applyReadValue(read.key, read.display);
                    self->setOperationBusy(false);
                    self->lastError_.clear();
                    self->appendDiagnostic(QStringLiteral("Guarded Write verified · %1").arg(reference));
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message, reference] {
                    if (!self || self->generation_ != generation) return;
                    self->setOperationBusy(false);
                    self->lastError_ = message;
                    self->appendDiagnostic(QStringLiteral("Write failed · %1 · %2").arg(reference).arg(message));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}