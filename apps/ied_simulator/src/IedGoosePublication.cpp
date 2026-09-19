// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedCommissioningModel.hpp"

#include "ariec61850/ethernet/ethernet.hpp"

#include <QByteArray>
#include <QDateTime>
#include <QNetworkInterface>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace {
QString qstringGoose(const std::string& value) {
    return QString::fromStdString(value);
}

QString logicalNodeGoose(const ar::iec61850::scl::SclDataSetEntry& entry) {
    return qstringGoose(entry.prefix) + qstringGoose(entry.ln_class) + qstringGoose(entry.ln_inst);
}

QString normalizedObject(QString value) {
    value.replace(QLatin1Char('$'), QLatin1Char('.'));
    return value;
}

bool pointMatchesBooleanText(const QString& value) {
    const auto normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("true") || normalized == QStringLiteral("false") ||
        normalized == QStringLiteral("on") || normalized == QStringLiteral("off") ||
        normalized == QStringLiteral("1") || normalized == QStringLiteral("0");
}

bool booleanValue(const QString& value) {
    const auto normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("true") || normalized == QStringLiteral("on") ||
        normalized == QStringLiteral("1");
}

std::optional<std::vector<std::uint8_t>> hexBytes(const QString& text) {
    auto normalized = text.trimmed();
    normalized.remove(QLatin1Char(' '));
    normalized.remove(QLatin1Char(':'));
    if (normalized.startsWith(QStringLiteral("0x"), Qt::CaseInsensitive)) normalized.remove(0, 2);
    if (normalized.isEmpty() || (normalized.size() % 2) != 0) return std::nullopt;
    const auto bytes = QByteArray::fromHex(normalized.toLatin1());
    if (bytes.size() * 2 != normalized.size()) return std::nullopt;
    std::vector<std::uint8_t> result;
    result.reserve(static_cast<std::size_t>(bytes.size()));
    for (const char byte : bytes) result.push_back(static_cast<std::uint8_t>(byte));
    return result;
}

QString transportErrorText(const std::string& error) {
    return QString::fromStdString(error);
}
} // namespace

QStringList IedCommissioningModel::gooseInterfaces() const {
    QStringList result;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& networkInterface : interfaces) {
        const auto flags = networkInterface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        const auto name = networkInterface.name().trimmed();
        if (name.isEmpty() || result.contains(name)) continue;
        result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void IedCommissioningModel::setGooseInterfaceName(const QString& interfaceName) {
    const auto normalized = interfaceName.trimmed();
    if (gooseInterfaceName_ == normalized) return;
    if (goosePublishing()) {
        if (backend_ != nullptr) {
            backend_->appendActivity(
                QStringLiteral("GOOSE"),
                QStringLiteral("Stop active GOOSE publication before changing the Ethernet interface."),
                QStringLiteral("Warning"));
        }
        return;
    }
    gooseInterfaceName_ = normalized;
    goosePublicationStatus_ = normalized.isEmpty()
        ? QStringLiteral("Select an Ethernet interface")
        : QStringLiteral("Ready on %1").arg(normalized);
    emit goosePublicationChanged();
}

QString IedCommissioningModel::selectedGooseIdentity() const {
    if (selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) return {};
    const auto& handle = handles_.at(selectedHandleIndex_);
    return handle.kind == Kind::goose ? handle.identity : QString{};
}

int IedCommissioningModel::goosePublisherIndexForIdentity(const QString& identity) const noexcept {
    for (int index = 0; index < static_cast<int>(goosePublishers_.size()); ++index) {
        if (goosePublishers_[static_cast<std::size_t>(index)].identity == identity) return index;
    }
    return -1;
}

QVariantMap IedCommissioningModel::selectedGooseRuntime() const {
    QVariantMap result;
    result.insert(QStringLiteral("active"), false);
    const auto identity = selectedGooseIdentity();
    if (identity.isEmpty()) return result;
    const int index = goosePublisherIndexForIdentity(identity);
    if (index < 0) return result;
    const auto& slot = goosePublishers_[static_cast<std::size_t>(index)];
    result.insert(QStringLiteral("active"), true);
    result.insert(QStringLiteral("interface"), gooseInterfaceName_);
    result.insert(QStringLiteral("transmits"), static_cast<qulonglong>(slot.transmitCount));
    result.insert(QStringLiteral("stateChanges"), static_cast<qulonglong>(slot.stateChangeCount));
    result.insert(QStringLiteral("lastTransmitMs"), slot.lastTransmitMilliseconds);
    result.insert(QStringLiteral("controlReference"), slot.controlReference);
    result.insert(QStringLiteral("dataSetReference"), slot.dataSetReference);
    result.insert(QStringLiteral("memberCount"), slot.pointStoreIndices.size());
    if (slot.runtime != nullptr) {
        result.insert(
            QStringLiteral("stNum"),
            static_cast<qulonglong>(slot.runtime->session().state_number()));
        result.insert(
            QStringLiteral("nextSqNum"),
            static_cast<qulonglong>(slot.runtime->session().next_sequence_number()));
    }
    return result;
}

std::optional<int> IedCommissioningModel::goosePointIndexFor(
    const int iedIndex,
    const ar::iec61850::scl::SclDataSetEntry& entry) const {
    if (backend_ == nullptr || iedIndex < 0 || iedIndex >= backend_->ieds_.size()) {
        return std::nullopt;
    }
    const auto iedName = backend_->ieds_.at(iedIndex).toMap().value(QStringLiteral("name")).toString();
    if (iedName.isEmpty()) return std::nullopt;

    const auto exactReference = qstringGoose(entry.signal_reference);
    if (!exactReference.isEmpty()) {
        const int exact = backend_->pointStore_.indexOf(iedName, exactReference);
        if (exact >= 0) return exact;
    }

    const auto expectedDomain = qstringGoose(entry.ied_name) + qstringGoose(entry.ld_inst);
    const auto expectedNode = logicalNodeGoose(entry);
    const auto expectedObject = normalizedObject(qstringGoose(entry.do_name));
    const auto expectedAttribute = normalizedObject(qstringGoose(entry.da_name));
    const auto expectedFc = qstringGoose(entry.functional_constraint);

    const auto& records = backend_->pointStore_.records();
    for (int index = 0; index < records.size(); ++index) {
        const auto& point = records.at(index);
        if (point.iedName != iedName) continue;
        if (!expectedDomain.isEmpty() && point.mmsDomain != expectedDomain) continue;
        if (!expectedNode.isEmpty() && point.logicalNode != expectedNode) continue;
        if (!expectedFc.isEmpty() &&
            point.functionalConstraint.compare(expectedFc, Qt::CaseInsensitive) != 0) continue;
        if (!expectedObject.isEmpty() && normalizedObject(point.dataObject) != expectedObject) continue;
        if (!expectedAttribute.isEmpty() &&
            normalizedObject(point.dataAttribute) != expectedAttribute) continue;
        return index;
    }
    return std::nullopt;
}

std::optional<ar::iec61850::mms::MmsDataValue>
IedCommissioningModel::gooseValueForPoint(const IedPointStore::PointRecord& point) const {
    using ar::iec61850::mms::Iec61850UtcTime;
    using ar::iec61850::mms::MmsDataValue;

    const auto type = (point.type + QLatin1Char(' ') + point.rawType).trimmed().toUpper();
    const auto attribute = point.dataAttribute.trimmed().toLower();

    if (attribute == QStringLiteral("q") || type.contains(QStringLiteral("QUALITY"))) {
        const auto bytes = hexBytes(point.value);
        if (!bytes.has_value() || bytes->size() < 2U || bytes->front() > 7U) return std::nullopt;
        return MmsDataValue::bit_string(
            bytes->front(),
            std::span<const std::uint8_t>{bytes->data() + 1, bytes->size() - 1U});
    }

    if (attribute == QStringLiteral("t") || type.contains(QStringLiteral("TIMESTAMP")) ||
        type.contains(QStringLiteral("UTC"))) {
        QString timestampText = point.value.trimmed();
        const QRegularExpression expression{QStringLiteral("unix-ms\\s*=\\s*(-?\\d+)")};
        const auto match = expression.match(timestampText);
        if (match.hasMatch()) timestampText = match.captured(1);
        bool ok{};
        const qint64 milliseconds = timestampText.toLongLong(&ok);
        if (!ok) return std::nullopt;
        return MmsDataValue::utc_time(Iec61850UtcTime{
            std::chrono::system_clock::time_point{std::chrono::milliseconds{milliseconds}}, 0U});
    }

    if (type.contains(QStringLiteral("BOOLEAN")) ||
        (pointMatchesBooleanText(point.value) &&
         (point.options.contains(QStringLiteral("true"), Qt::CaseInsensitive) ||
          point.options.contains(QStringLiteral("false"), Qt::CaseInsensitive)))) {
        return MmsDataValue::boolean(booleanValue(point.value));
    }

    if (type.contains(QStringLiteral("FLOAT")) || type.contains(QStringLiteral("DOUBLE")) ||
        type.contains(QStringLiteral("REAL"))) {
        bool ok{};
        const double value = point.value.toDouble(&ok);
        if (!ok || !std::isfinite(value)) return std::nullopt;
        return MmsDataValue::floating_point(value);
    }

    if (type.contains(QStringLiteral("UINT")) || type.contains(QStringLiteral("UNSIGNED"))) {
        bool ok{};
        const qulonglong value = point.value.toULongLong(&ok, 0);
        if (!ok) return std::nullopt;
        return MmsDataValue::unsigned_integer(static_cast<std::uint64_t>(value));
    }

    if (type.contains(QStringLiteral("INT")) || type.contains(QStringLiteral("INTEGER"))) {
        bool ok{};
        const qlonglong value = point.value.toLongLong(&ok, 0);
        if (!ok) return std::nullopt;
        return MmsDataValue::integer(static_cast<std::int64_t>(value));
    }

    if (type.contains(QStringLiteral("OCTET"))) {
        const auto bytes = hexBytes(point.value);
        if (!bytes.has_value()) return std::nullopt;
        return MmsDataValue::octet_string(*bytes);
    }

    return MmsDataValue::visible_string(point.value.toStdString());
}

bool IedCommissioningModel::buildGooseValues(
    const GoosePublisherSlot& slot,
    std::vector<ar::iec61850::mms::MmsDataValue>& values,
    QVector<QString>* const valueSnapshot) const {
    values.clear();
    values.reserve(static_cast<std::size_t>(slot.pointStoreIndices.size()));
    if (valueSnapshot != nullptr) {
        valueSnapshot->clear();
        valueSnapshot->reserve(slot.pointStoreIndices.size());
    }
    if (backend_ == nullptr) return false;

    for (const int pointIndex : slot.pointStoreIndices) {
        const auto* point = backend_->pointStore_.at(pointIndex);
        if (point == nullptr) return false;
        auto value = gooseValueForPoint(*point);
        if (!value.has_value()) return false;
        values.push_back(std::move(*value));
        if (valueSnapshot != nullptr) valueSnapshot->push_back(point->value);
    }
    return true;
}

bool IedCommissioningModel::startSelectedGoosePublication() {
    using namespace ar::iec61850;

    if (backend_ == nullptr || backend_->selectedIedIndex_ < 0 ||
        selectedHandleIndex_ < 0 || selectedHandleIndex_ >= handles_.size()) {
        return false;
    }
    const auto& handle = handles_.at(selectedHandleIndex_);
    if (handle.kind != Kind::goose) return false;
    if (goosePublisherIndexForIdentity(handle.identity) >= 0) return true;
    if (static_cast<int>(goosePublishers_.size()) >= kGoosePublisherCapacity) {
        failGoosePublication(QStringLiteral("GOOSE publisher capacity is limited to 16 active streams."));
        return false;
    }

    const int iedIndex = backend_->selectedIedIndex_;
    const auto* iedRuntime = backend_->runtimeAt(iedIndex);
    if (iedRuntime == nullptr || iedRuntime->state != IedFleetController::RuntimeState::running) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"),
            QStringLiteral("Start the selected MMS simulator runtime before enabling GOOSE publication."),
            QStringLiteral("Warning"));
        return false;
    }
    if (gooseInterfaceName_.trimmed().isEmpty()) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"),
            QStringLiteral("Select an explicit Ethernet interface before enabling GOOSE publication."),
            QStringLiteral("Warning"));
        goosePublicationStatus_ = QStringLiteral("Interface required");
        emit goosePublicationChanged();
        return false;
    }
    if (!gooseInterfaces().contains(gooseInterfaceName_)) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"),
            QStringLiteral("The selected Ethernet interface is not currently up/running: %1")
                .arg(gooseInterfaceName_),
            QStringLiteral("Error"));
        goosePublicationStatus_ = QStringLiteral("Interface unavailable");
        emit goosePublicationChanged();
        return false;
    }
    if (handle.documentIndex < 0 || handle.documentIndex >= static_cast<int>(backend_->documents_.size())) {
        return false;
    }
    const auto& document = backend_->documents_[static_cast<std::size_t>(handle.documentIndex)].document;
    if (handle.sourceIndex < 0 || handle.sourceIndex >= static_cast<int>(document.goose_streams.size())) {
        return false;
    }
    const auto& stream = document.goose_streams[static_cast<std::size_t>(handle.sourceIndex)];

    if (!stream.address.app_id.has_value() || !stream.address.destination_mac.has_value()) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"),
            QStringLiteral("GOOSE publication rejected: APPID and destination MAC must both be configured in SCL."),
            QStringLiteral("Error"));
        return false;
    }
    if (stream.entries.empty() || stream.entries.size() > static_cast<std::size_t>(kGooseMemberCapacity)) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"),
            QStringLiteral("GOOSE publication rejected: DataSet must resolve to 1..256 canonical members."),
            QStringLiteral("Error"));
        return false;
    }
    if (stream.min_time_milliseconds == 0U ||
        stream.max_time_milliseconds < stream.min_time_milliseconds) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"),
            QStringLiteral("GOOSE publication rejected: SCL MinTime/MaxTime retransmission settings are invalid."),
            QStringLiteral("Error"));
        return false;
    }
    if (stream.address.vlan_id.has_value() != stream.address.vlan_priority.has_value()) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"),
            QStringLiteral("GOOSE publication rejected: VLAN ID and priority must be configured together."),
            QStringLiteral("Error"));
        return false;
    }

    GoosePublisherSlot slot;
    slot.iedIndex = iedIndex;
    slot.sessionKey = iedSessionKey(iedIndex);
    slot.identity = handle.identity;
    slot.controlReference = qstringGoose(stream.control_block_reference);
    slot.dataSetReference = qstringGoose(stream.data_set_reference);
    slot.minTimeMilliseconds = stream.min_time_milliseconds;
    slot.pointStoreIndices.reserve(static_cast<int>(stream.entries.size()));
    for (const auto& entry : stream.entries) {
        const auto pointIndex = goosePointIndexFor(iedIndex, entry);
        if (!pointIndex.has_value()) {
            backend_->appendActivity(
                QStringLiteral("GOOSE"),
                QStringLiteral("GOOSE publication rejected: one DataSet member could not be resolved to the canonical point store."),
                QStringLiteral("Error"));
            return false;
        }
        slot.pointStoreIndices.push_back(*pointIndex);
    }

    std::vector<mms::MmsDataValue> values;
    if (!buildGooseValues(slot, values, &slot.lastValues)) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"),
            QStringLiteral("GOOSE publication rejected: one DataSet value cannot be represented on the MMS/GOOSE wire."),
            QStringLiteral("Error"));
        return false;
    }

    const bool openingTransport = !gooseTransport_.active();
    if (openingTransport) {
        std::string error;
        if (!gooseTransport_.open(gooseInterfaceName_.toStdString(), error)) {
            goosePublicationStatus_ = transportErrorText(error);
            backend_->appendActivity(
                QStringLiteral("GOOSE"), goosePublicationStatus_, QStringLiteral("Error"));
            emit goosePublicationChanged();
            return false;
        }
    }

    goose::GooseFrame frame;
    frame.destination = ethernet::MacAddress{
        std::span<const std::uint8_t>{stream.address.destination_mac->data(),
                                      stream.address.destination_mac->size()}};
    frame.source = gooseTransport_.source_mac();
    frame.app_id = *stream.address.app_id;
    if (stream.address.vlan_id.has_value()) {
        frame.vlan = ethernet::VlanTag{
            *stream.address.vlan_priority,
            *stream.address.vlan_id};
    }
    frame.pdu.go_cb_ref = stream.control_block_reference;
    frame.pdu.data_set_reference = stream.data_set_reference;
    frame.pdu.go_id = stream.go_id;
    frame.pdu.configuration_revision = stream.configuration_revision;
    frame.pdu.time_allowed_to_live_milliseconds =
        std::max<std::uint32_t>(1U, stream.min_time_milliseconds * 2U);

    slot.runtime = std::make_unique<goose::GoosePublisherRuntime>(
        goose::GoosePublisherSession{std::move(frame)},
        stream.min_time_milliseconds,
        stream.max_time_milliseconds);

    const auto publication = slot.runtime->start(
        values,
        mms::Iec61850UtcTime{std::chrono::system_clock::now(), 0U},
        goose::GoosePublisherRuntime::clock::now());
    if (!transmitGoosePublication(slot, publication, false)) {
        const auto failure = goosePublicationStatus_;
        if (openingTransport && goosePublishers_.empty()) gooseTransport_.close();
        failGoosePublication(failure);
        return false;
    }

    goosePublishers_.push_back(std::move(slot));
    goosePublicationStatus_ = QStringLiteral("Publishing %1 stream%2 on %3")
        .arg(goosePublishers_.size())
        .arg(goosePublishers_.size() == 1U ? QString{} : QStringLiteral("s"))
        .arg(gooseInterfaceName_);
    gooseValuesDirty_ = false;

    connect(
        backend_, &IedFleetController::valuesChanged,
        this, &IedCommissioningModel::markGooseValuesDirty,
        Qt::UniqueConnection);
    connect(
        backend_, &IedFleetController::runtimeChanged,
        this, &IedCommissioningModel::pruneGoosePublishers,
        Qt::UniqueConnection);
    connect(
        backend_, &IedFleetController::selectionChanged,
        this, &IedCommissioningModel::pruneGoosePublishers,
        Qt::UniqueConnection);
    ensureGooseTimer();

    backend_->appendActivity(
        QStringLiteral("GOOSE"),
        QStringLiteral("Publishing %1 on %2 · APPID %3 · %4 member%5.")
            .arg(qstringGoose(stream.control_block_reference), gooseInterfaceName_,
                 qstringGoose(stream.address.app_id_text))
            .arg(stream.entries.size())
            .arg(stream.entries.size() == 1U ? QString{} : QStringLiteral("s")),
        QStringLiteral("Success"),
        backend_->ieds_.at(iedIndex).toMap().value(QStringLiteral("name")).toString());
    emit goosePublicationChanged();
    return true;
}

bool IedCommissioningModel::stopSelectedGoosePublication() {
    const auto identity = selectedGooseIdentity();
    if (identity.isEmpty()) return false;
    const int index = goosePublisherIndexForIdentity(identity);
    if (index < 0) return false;
    auto& slot = goosePublishers_[static_cast<std::size_t>(index)];
    if (slot.runtime != nullptr) slot.runtime->stop();
    goosePublishers_.erase(goosePublishers_.begin() + index);

    if (goosePublishers_.empty()) {
        if (gooseTimer_ != nullptr) gooseTimer_->stop();
        gooseTransport_.close();
        gooseValuesDirty_ = false;
        goosePublicationStatus_ = gooseInterfaceName_.isEmpty()
            ? QStringLiteral("Stopped")
            : QStringLiteral("Ready on %1").arg(gooseInterfaceName_);
    } else {
        ensureGooseTimer();
        goosePublicationStatus_ = QStringLiteral("Publishing %1 streams on %2")
            .arg(goosePublishers_.size()).arg(gooseInterfaceName_);
    }
    emit goosePublicationChanged();
    return true;
}

void IedCommissioningModel::stopAllGoosePublication() {
    for (auto& slot : goosePublishers_) {
        if (slot.runtime != nullptr) slot.runtime->stop();
    }
    goosePublishers_.clear();
    if (gooseTimer_ != nullptr) gooseTimer_->stop();
    gooseTransport_.close();
    gooseValuesDirty_ = false;
    goosePublicationStatus_ = gooseInterfaceName_.isEmpty()
        ? QStringLiteral("Stopped")
        : QStringLiteral("Ready on %1").arg(gooseInterfaceName_);
    emit goosePublicationChanged();
}

void IedCommissioningModel::markGooseValuesDirty() {
    if (!goosePublishers_.empty()) gooseValuesDirty_ = true;
}

void IedCommissioningModel::ensureGooseTimer() {
    if (goosePublishers_.empty()) return;
    if (gooseTimer_ == nullptr) {
        gooseTimer_ = new QTimer(this);
        gooseTimer_->setTimerType(Qt::PreciseTimer);
        connect(gooseTimer_, &QTimer::timeout, this, &IedCommissioningModel::advanceGoosePublication);
    }
    std::uint32_t minimum = 10U;
    for (const auto& slot : goosePublishers_) {
        minimum = std::min(minimum, std::max<std::uint32_t>(1U, slot.minTimeMilliseconds));
    }
    gooseTimer_->setInterval(static_cast<int>(std::clamp<std::uint32_t>(minimum, 1U, 10U)));
    if (!gooseTimer_->isActive()) gooseTimer_->start();
}

void IedCommissioningModel::pruneGoosePublishers() {
    if (goosePublishers_.empty()) return;
    if (backend_ == nullptr || backend_->selectedIedIndex_ < 0) {
        stopAllGoosePublication();
        return;
    }
    const auto currentSession = iedSessionKey(backend_->selectedIedIndex_);
    for (const auto& slot : goosePublishers_) {
        const auto* runtime = backend_->runtimeAt(slot.iedIndex);
        if (slot.sessionKey != currentSession || runtime == nullptr ||
            runtime->state != IedFleetController::RuntimeState::running) {
            stopAllGoosePublication();
            return;
        }
    }
}

bool IedCommissioningModel::transmitGoosePublication(
    GoosePublisherSlot& slot,
    const ar::iec61850::goose::GoosePublication& publication,
    const bool stateChange) {
    std::string error;
    if (!gooseTransport_.transmit(publication.ethernet_bytes, error)) {
        goosePublicationStatus_ = transportErrorText(error);
        return false;
    }
    ++slot.transmitCount;
    ++gooseTransmitCount_;
    if (stateChange) {
        ++slot.stateChangeCount;
        ++gooseStateChangeCount_;
    }
    slot.lastTransmitMilliseconds = QDateTime::currentMSecsSinceEpoch();
    const auto now = slot.lastTransmitMilliseconds;
    if (now - gooseLastUiNotifyMilliseconds_ >= 100) {
        gooseLastUiNotifyMilliseconds_ = now;
        emit goosePublicationChanged();
    }
    return true;
}

void IedCommissioningModel::advanceGoosePublication() {
    if (goosePublishers_.empty()) {
        if (gooseTimer_ != nullptr) gooseTimer_->stop();
        return;
    }
    pruneGoosePublishers();
    if (goosePublishers_.empty()) return;

    using Runtime = ar::iec61850::goose::GoosePublisherRuntime;
    if (gooseValuesDirty_) {
        gooseValuesDirty_ = false;
        for (auto& slot : goosePublishers_) {
            std::vector<ar::iec61850::mms::MmsDataValue> values;
            QVector<QString> snapshot;
            if (!buildGooseValues(slot, values, &snapshot)) {
                failGoosePublication(QStringLiteral(
                    "GOOSE source value became unrepresentable; publication stopped fail-closed."));
                return;
            }
            if (snapshot == slot.lastValues) continue;
            const auto publication = slot.runtime->state_change(
                values,
                ar::iec61850::mms::Iec61850UtcTime{std::chrono::system_clock::now(), 0U},
                Runtime::clock::now());
            if (!transmitGoosePublication(slot, publication, true)) {
                failGoosePublication(goosePublicationStatus_);
                return;
            }
            slot.lastValues = std::move(snapshot);
        }
    }

    const auto now = Runtime::clock::now();
    for (auto& slot : goosePublishers_) {
        auto publication = slot.runtime->poll(now);
        if (!publication.has_value()) continue;
        if (!transmitGoosePublication(slot, *publication, false)) {
            failGoosePublication(goosePublicationStatus_);
            return;
        }
    }
}

void IedCommissioningModel::failGoosePublication(const QString& reason) {
    const auto failure = reason.isEmpty()
        ? QStringLiteral("GOOSE publication failed closed.") : reason;
    for (auto& slot : goosePublishers_) {
        if (slot.runtime != nullptr) slot.runtime->stop();
    }
    goosePublishers_.clear();
    if (gooseTimer_ != nullptr) gooseTimer_->stop();
    gooseTransport_.close();
    gooseValuesDirty_ = false;
    goosePublicationStatus_ = failure;
    if (backend_ != nullptr) {
        backend_->appendActivity(
            QStringLiteral("GOOSE"), failure, QStringLiteral("Error"));
    }
    emit goosePublicationChanged();
}
