// SPDX-License-Identifier: GPL-3.0-or-later

#include "GooseMonitorController.hpp"

#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/mms/data_value.hpp"

#include <QByteArray>
#include <QNetworkInterface>
#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace {
using ar::iec61850::goose::GooseSequenceStatus;
using ar::iec61850::mms::MmsDataKind;
using ar::iec61850::mms::MmsDataValue;

QString appIdText(const std::uint16_t appId) {
    return QStringLiteral("0x%1").arg(
        QString::number(appId, 16).rightJustified(4, QLatin1Char('0')).toUpper());
}

QString rawHex(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return QStringLiteral("—");
    const auto raw = QByteArray(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(std::min<std::size_t>(
            bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))));
    return QString::fromLatin1(raw.toHex(' ')).toUpper();
}

bool sequenceAnomaly(const GooseSequenceStatus status) noexcept {
    return status == GooseSequenceStatus::duplicate ||
        status == GooseSequenceStatus::sequence_gap ||
        status == GooseSequenceStatus::sequence_regression ||
        status == GooseSequenceStatus::state_jump ||
        status == GooseSequenceStatus::state_regression;
}

bool sequenceIssue(const GooseSequenceStatus status) noexcept {
    return status == GooseSequenceStatus::sequence_gap ||
        status == GooseSequenceStatus::sequence_regression ||
        status == GooseSequenceStatus::state_jump ||
        status == GooseSequenceStatus::state_regression;
}
} // namespace

GooseMonitorController::GooseMonitorController(QObject* parent)
    : QAbstractListModel(parent) {
    monitorClock_.start();
    captureTimer_ = new QTimer(this);
    captureTimer_->setInterval(kPollIntervalMilliseconds);
    captureTimer_->setTimerType(Qt::PreciseTimer);
    connect(captureTimer_, &QTimer::timeout, this, &GooseMonitorController::pollCapture);
}

GooseMonitorController::~GooseMonitorController() {
    stopCapture();
}

int GooseMonitorController::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(streams_.size());
}

QVariant GooseMonitorController::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& slot = streams_[static_cast<std::size_t>(index.row())];
    const auto& frame = slot.frame;
    switch (role) {
    case SourceMacRole: return QString::fromStdString(frame.source.to_string());
    case DestinationMacRole: return QString::fromStdString(frame.destination.to_string());
    case AppIdRole: return static_cast<int>(frame.app_id);
    case AppIdTextRole: return appIdText(frame.app_id);
    case VlanIdRole: return frame.vlan.has_value() ? QVariant{static_cast<int>(frame.vlan->vlan_id)} : QVariant{};
    case VlanPriorityRole:
        return frame.vlan.has_value() ? QVariant{static_cast<int>(frame.vlan->priority_code_point)} : QVariant{};
    case GoIdRole: return QString::fromStdString(frame.pdu.go_id);
    case GoCbRefRole: return QString::fromStdString(frame.pdu.go_cb_ref);
    case DataSetReferenceRole: return QString::fromStdString(frame.pdu.data_set_reference);
    case ConfRevRole: return static_cast<qulonglong>(frame.pdu.configuration_revision);
    case StateNumberRole: return static_cast<qulonglong>(frame.pdu.state_number);
    case SequenceNumberRole: return static_cast<qulonglong>(frame.pdu.sequence_number);
    case TtlRole: return static_cast<qulonglong>(frame.pdu.time_allowed_to_live_milliseconds);
    case StatusRole: return slot.status;
    case TimedOutRole: return slot.timedOut;
    case LateRole: return slot.late;
    case AnomalyRole: return slot.anomaly;
    case PacketCountRole: return static_cast<qulonglong>(slot.packetCount);
    case LastSeenMsRole: return slot.lastSeenMilliseconds;
    case ValuesSummaryRole: return slot.valuesSummary;
    case ValueCountRole: return slot.values.size();
    default: return {};
    }
}

QHash<int, QByteArray> GooseMonitorController::roleNames() const {
    return {
        {SourceMacRole, "sourceMac"},
        {DestinationMacRole, "destinationMac"},
        {AppIdRole, "appId"},
        {AppIdTextRole, "appIdText"},
        {VlanIdRole, "vlanId"},
        {VlanPriorityRole, "vlanPriority"},
        {GoIdRole, "goId"},
        {GoCbRefRole, "goCbRef"},
        {DataSetReferenceRole, "dataSetReference"},
        {ConfRevRole, "confRev"},
        {StateNumberRole, "stNum"},
        {SequenceNumberRole, "sqNum"},
        {TtlRole, "ttlMs"},
        {StatusRole, "status"},
        {TimedOutRole, "timedOut"},
        {LateRole, "late"},
        {AnomalyRole, "anomaly"},
        {PacketCountRole, "packets"},
        {LastSeenMsRole, "lastSeenMs"},
        {ValuesSummaryRole, "valuesSummary"},
        {ValueCountRole, "valueCount"},
    };
}

QStringList GooseMonitorController::interfaces() const {
    QStringList result;
    for (const auto& networkInterface : QNetworkInterface::allInterfaces()) {
        const auto flags = networkInterface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp) ||
            !flags.testFlag(QNetworkInterface::IsRunning) ||
            flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        const auto name = networkInterface.name().trimmed();
        if (!name.isEmpty() && !result.contains(name)) result.push_back(name);
    }
    std::sort(result.begin(), result.end());
    return result;
}

void GooseMonitorController::setInterfaceName(const QString& value) {
    const auto normalized = value.trimmed();
    if (normalized == interfaceName_) return;
    if (capturing_) {
        appendEvent(QStringLiteral("Stop GOOSE monitoring before changing the Ethernet interface."));
        emit monitorChanged();
        return;
    }
    interfaceName_ = normalized;
    statusText_ = normalized.isEmpty()
        ? QStringLiteral("Select an Ethernet interface")
        : QStringLiteral("Ready on %1").arg(normalized);
    emit monitorChanged();
}

bool GooseMonitorController::startCapture() {
    if (capturing_) return true;
    if (interfaceName_.isEmpty()) {
        statusText_ = QStringLiteral("Interface required");
        appendEvent(QStringLiteral("GOOSE monitoring rejected: select an explicit Ethernet interface."));
        emit monitorChanged();
        return false;
    }
    std::string error;
    if (!transport_.open(interfaceName_.toStdString(), error)) {
        statusText_ = QStringLiteral("Capture failed: %1").arg(QString::fromStdString(error));
        appendEvent(statusText_);
        emit monitorChanged();
        return false;
    }
    capturing_ = true;
    statusText_ = QStringLiteral("Capturing GOOSE on %1").arg(interfaceName_);
    lastExpiryScanMilliseconds_ = monitorClock_.elapsed();
    captureTimer_->start();
    appendEvent(statusText_);
    emit monitorChanged();
    return true;
}

void GooseMonitorController::stopCapture() {
    if (captureTimer_ != nullptr) captureTimer_->stop();
    transport_.close();
    if (!capturing_) return;
    capturing_ = false;
    statusText_ = QStringLiteral("Stopped");
    appendEvent(QStringLiteral("GOOSE monitoring stopped."));
    emit monitorChanged();
}

void GooseMonitorController::clear() {
    beginResetModel();
    streams_.clear();
    endResetModel();
    retainedPackets_.clear();
    recentEvents_.clear();
    selectedRow_ = -1;
    packetCount_ = 0;
    decodeErrorCount_ = 0;
    rejectedFrameCount_ = 0;
    droppedStreamCount_ = 0;
    duplicateCount_ = 0;
    sequenceIssueCount_ = 0;
    timeoutCount_ = 0;
    statusText_ = capturing_
        ? QStringLiteral("Capturing GOOSE on %1").arg(interfaceName_)
        : QStringLiteral("Stopped");
    emit selectionChanged();
    emit monitorChanged();
}

void GooseMonitorController::select(const int row) {
    if (row < 0 || row >= rowCount() || row == selectedRow_) return;
    selectedRow_ = row;
    emit selectionChanged();
}

QVariantMap GooseMonitorController::selectedStream() const {
    if (selectedRow_ < 0 || selectedRow_ >= rowCount()) return {};
    return streamMap(streams_[static_cast<std::size_t>(selectedRow_)]);
}

QStringList GooseMonitorController::selectedValues() const {
    if (selectedRow_ < 0 || selectedRow_ >= rowCount()) return {};
    return streams_[static_cast<std::size_t>(selectedRow_)].values;
}

QVariantMap GooseMonitorController::streamMap(const StreamSlot& slot) const {
    QVariantMap result;
    const auto& frame = slot.frame;
    result.insert(QStringLiteral("sourceMac"), QString::fromStdString(frame.source.to_string()));
    result.insert(QStringLiteral("destinationMac"), QString::fromStdString(frame.destination.to_string()));
    result.insert(QStringLiteral("appId"), static_cast<int>(frame.app_id));
    result.insert(QStringLiteral("appIdText"), appIdText(frame.app_id));
    result.insert(
        QStringLiteral("vlanId"),
        frame.vlan.has_value() ? QVariant{static_cast<int>(frame.vlan->vlan_id)} : QVariant{});
    result.insert(
        QStringLiteral("vlanPriority"),
        frame.vlan.has_value() ? QVariant{static_cast<int>(frame.vlan->priority_code_point)} : QVariant{});
    result.insert(QStringLiteral("goId"), QString::fromStdString(frame.pdu.go_id));
    result.insert(QStringLiteral("goCbRef"), QString::fromStdString(frame.pdu.go_cb_ref));
    result.insert(QStringLiteral("dataSetReference"), QString::fromStdString(frame.pdu.data_set_reference));
    result.insert(QStringLiteral("confRev"), static_cast<qulonglong>(frame.pdu.configuration_revision));
    result.insert(QStringLiteral("stNum"), static_cast<qulonglong>(frame.pdu.state_number));
    result.insert(QStringLiteral("sqNum"), static_cast<qulonglong>(frame.pdu.sequence_number));
    result.insert(QStringLiteral("ttlMs"), static_cast<qulonglong>(frame.pdu.time_allowed_to_live_milliseconds));
    result.insert(QStringLiteral("status"), slot.status);
    result.insert(QStringLiteral("timedOut"), slot.timedOut);
    result.insert(QStringLiteral("late"), slot.late);
    result.insert(QStringLiteral("anomaly"), slot.anomaly);
    result.insert(QStringLiteral("packets"), static_cast<qulonglong>(slot.packetCount));
    result.insert(QStringLiteral("lastSeenMs"), slot.lastSeenMilliseconds);
    result.insert(QStringLiteral("valueCount"), slot.values.size());
    result.insert(QStringLiteral("valuesSummary"), slot.valuesSummary);
    return result;
}

QString GooseMonitorController::sequenceStatusText(const GooseSequenceStatus status) {
    switch (status) {
    case GooseSequenceStatus::first: return QStringLiteral("First frame");
    case GooseSequenceStatus::retransmission: return QStringLiteral("Retransmission");
    case GooseSequenceStatus::state_change: return QStringLiteral("State change");
    case GooseSequenceStatus::duplicate: return QStringLiteral("Duplicate");
    case GooseSequenceStatus::sequence_gap: return QStringLiteral("Sequence gap");
    case GooseSequenceStatus::sequence_regression: return QStringLiteral("Out of order");
    case GooseSequenceStatus::state_jump: return QStringLiteral("State jump");
    case GooseSequenceStatus::state_regression: return QStringLiteral("Stale state");
    case GooseSequenceStatus::identity_mismatch: return QStringLiteral("Identity mismatch");
    }
    return QStringLiteral("Unknown");
}

QString GooseMonitorController::formatValue(const MmsDataValue& value, const int depth) {
    if (depth > 4) return QStringLiteral("…");
    const auto& scalar = value.value();
    switch (value.kind()) {
    case MmsDataKind::boolean:
        if (const auto* v = std::get_if<bool>(&scalar)) return *v ? QStringLiteral("true") : QStringLiteral("false");
        break;
    case MmsDataKind::integer:
        if (const auto* v = std::get_if<std::int64_t>(&scalar)) return QString::number(*v);
        break;
    case MmsDataKind::unsigned_integer:
        if (const auto* v = std::get_if<std::uint64_t>(&scalar)) return QString::number(*v);
        break;
    case MmsDataKind::floating_point:
        if (const auto* v = std::get_if<float>(&scalar)) return QString::number(*v, 'g', 8);
        if (const auto* v = std::get_if<double>(&scalar)) return QString::number(*v, 'g', 12);
        break;
    case MmsDataKind::visible_string:
    case MmsDataKind::mms_string:
        if (const auto* v = std::get_if<std::string>(&scalar)) return QString::fromStdString(*v);
        break;
    case MmsDataKind::utc_time:
        if (const auto* v = std::get_if<ar::iec61850::mms::Iec61850UtcTime>(&scalar)) {
            const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
                v->value.time_since_epoch()).count();
            return QStringLiteral("UTC %1 ms q=0x%2")
                .arg(milliseconds)
                .arg(QString::number(v->quality, 16).rightJustified(2, QLatin1Char('0')).toUpper());
        }
        break;
    case MmsDataKind::array:
    case MmsDataKind::structure: {
        QStringList children;
        const auto& values = value.children();
        const auto limit = std::min<std::size_t>(values.size(), 12U);
        for (std::size_t index = 0; index < limit; ++index) {
            children.push_back(formatValue(values[index], depth + 1));
        }
        if (values.size() > limit) children.push_back(QStringLiteral("… +%1").arg(values.size() - limit));
        const bool array = value.kind() == MmsDataKind::array;
        return (array ? QStringLiteral("[") : QStringLiteral("{")) +
            children.join(QStringLiteral(", ")) +
            (array ? QStringLiteral("]") : QStringLiteral("}"));
    }
    case MmsDataKind::bit_string: return QStringLiteral("bits %1").arg(rawHex(value.raw_value()));
    case MmsDataKind::octet_string: return QStringLiteral("octets %1").arg(rawHex(value.raw_value()));
    case MmsDataKind::binary_time: return QStringLiteral("binary-time %1").arg(rawHex(value.raw_value()));
    case MmsDataKind::bcd: return QStringLiteral("bcd %1").arg(rawHex(value.raw_value()));
    case MmsDataKind::boolean_array: return QStringLiteral("bool-array %1").arg(rawHex(value.raw_value()));
    case MmsDataKind::object_id: return QStringLiteral("object-id %1").arg(rawHex(value.raw_value()));
    case MmsDataKind::unknown: return QStringLiteral("unknown %1").arg(rawHex(value.raw_value()));
    }
    return QStringLiteral("—");
}

QString GooseMonitorController::streamKey(const ar::iec61850::goose::GooseFrame& frame) {
    return QString::fromStdString(frame.source.to_string()) + QLatin1Char('|') +
        QString::number(frame.app_id) + QLatin1Char('|') + QString::fromStdString(frame.pdu.go_cb_ref);
}

int GooseMonitorController::indexForKey(const QString& key) const noexcept {
    for (int index = 0; index < rowCount(); ++index) {
        if (streams_[static_cast<std::size_t>(index)].key == key) return index;
    }
    return -1;
}

bool GooseMonitorController::ingestFrameBytes(
    const std::span<const std::uint8_t> frameBytes,
    const clock::time_point arrivalTime) {
    return ingestFrameInternal(frameBytes, arrivalTime, std::chrono::system_clock::now());
}

bool GooseMonitorController::ingestFrameInternal(
    const std::span<const std::uint8_t> frameBytes,
    const clock::time_point arrivalTime,
    const std::chrono::system_clock::time_point captureTimestamp) {
    ar::iec61850::goose::GooseFrame decoded;
    if (!ar::iec61850::goose::GooseFrameCodec::try_decode(frameBytes, decoded)) {
        ++decodeErrorCount_;
        appendEvent(QStringLiteral("Rejected malformed/non-GOOSE Ethernet frame."));
        emit monitorChanged();
        return false;
    }
    if (decoded.pdu.values.size() > static_cast<std::size_t>(kMemberCapacity)) {
        ++rejectedFrameCount_;
        appendEvent(QStringLiteral("Rejected GOOSE frame: allData exceeds 256 members."));
        emit monitorChanged();
        return false;
    }

    const auto key = streamKey(decoded);
    int row = indexForKey(key);
    if (row < 0) {
        if (rowCount() >= kStreamCapacity) {
            ++droppedStreamCount_;
            appendEvent(QStringLiteral("Dropped new GOOSE identity: stream table capacity is 256."));
            emit monitorChanged();
            return false;
        }
        row = rowCount();
        beginInsertRows(QModelIndex{}, row, row);
        streams_.emplace_back();
        streams_.back().key = key;
        endInsertRows();
        if (selectedRow_ < 0) {
            selectedRow_ = row;
            emit selectionChanged();
        }
    }

    auto& slot = streams_[static_cast<std::size_t>(row)];
    const bool wasTimedOut = slot.timedOut;
    ar::iec61850::goose::GooseSupervisionResult supervision;
    try {
        supervision = slot.supervisor.observe(decoded.pdu, arrivalTime);
    } catch (const std::exception& error) {
        ++rejectedFrameCount_;
        appendEvent(QStringLiteral("Rejected GOOSE frame: %1").arg(QString::fromUtf8(error.what())));
        emit monitorChanged();
        return false;
    }
    if (!supervision.accepted) {
        ++rejectedFrameCount_;
        appendEvent(QStringLiteral("Rejected GOOSE identity mismatch."));
        emit monitorChanged();
        return false;
    }

    if (supervision.status == GooseSequenceStatus::duplicate) ++duplicateCount_;
    bool issue = sequenceIssue(supervision.status);
    if (supervision.state_change_sequence_not_zero ||
        supervision.value_changed_without_state_increment ||
        supervision.configuration_revision_changed) {
        issue = true;
    }
    if (issue) ++sequenceIssueCount_;
    if (supervision.expired_before_arrival && !wasTimedOut) ++timeoutCount_;

    slot.frame = std::move(decoded);
    slot.packetCount++;
    slot.lastSeenMilliseconds = monitorClock_.elapsed();
    slot.timedOut = false;
    slot.late = supervision.expired_before_arrival;
    slot.anomaly = sequenceAnomaly(supervision.status) || issue || slot.late;
    slot.status = sequenceStatusText(supervision.status);
    if (slot.late) slot.status.prepend(QStringLiteral("Late after timeout · "));
    if (supervision.missed_sequence_count > 0U) {
        slot.status += QStringLiteral(" · missed %1").arg(supervision.missed_sequence_count);
    }
    if (supervision.state_change_sequence_not_zero) slot.status += QStringLiteral(" · sqNum not zero");
    if (supervision.value_changed_without_state_increment) {
        slot.status += QStringLiteral(" · value changed without stNum");
    }
    if (supervision.configuration_revision_changed) slot.status += QStringLiteral(" · ConfRev changed");

    slot.values.clear();
    slot.values.reserve(static_cast<int>(slot.frame.pdu.values.size()));
    for (std::size_t index = 0; index < slot.frame.pdu.values.size(); ++index) {
        slot.values.push_back(
            QStringLiteral("[%1] %2").arg(index).arg(formatValue(slot.frame.pdu.values[index])));
    }
    QStringList summary;
    const int summaryCount = std::min(3, slot.values.size());
    for (int index = 0; index < summaryCount; ++index) summary.push_back(slot.values.at(index));
    if (slot.values.size() > summaryCount) {
        summary.push_back(QStringLiteral("… +%1").arg(slot.values.size() - summaryCount));
    }
    slot.valuesSummary = summary.join(QStringLiteral("  "));

    ++packetCount_;
    retainPacket(frameBytes, captureTimestamp);
    appendEvent(
        QStringLiteral("%1 · %2 · st=%3 sq=%4 · %5")
            .arg(appIdText(slot.frame.app_id), QString::fromStdString(slot.frame.pdu.go_cb_ref))
            .arg(slot.frame.pdu.state_number)
            .arg(slot.frame.pdu.sequence_number)
            .arg(slot.status));

    const auto modelIndex = index(row, 0);
    emit dataChanged(modelIndex, modelIndex);
    if (selectedRow_ == row) emit selectionChanged();
    emit monitorChanged();
    return true;
}

void GooseMonitorController::checkTimeoutsAt(const clock::time_point now) {
    bool changed = false;
    for (int row = 0; row < rowCount(); ++row) {
        auto& slot = streams_[static_cast<std::size_t>(row)];
        const auto expiry = slot.supervisor.check_expiry(now);
        if (!expiry.has_value()) continue;
        slot.timedOut = true;
        slot.anomaly = true;
        slot.status = QStringLiteral("Timeout");
        ++timeoutCount_;
        appendEvent(
            QStringLiteral("TIMEOUT · %1 · st=%2 sq=%3 ttl=%4 ms")
                .arg(QString::fromStdString(slot.frame.pdu.go_cb_ref))
                .arg(expiry->state_number)
                .arg(expiry->sequence_number)
                .arg(expiry->time_allowed_to_live_milliseconds));
        const auto modelIndex = index(row, 0);
        emit dataChanged(modelIndex, modelIndex);
        if (selectedRow_ == row) emit selectionChanged();
        changed = true;
    }
    if (changed) emit monitorChanged();
}

void GooseMonitorController::pollCapture() {
    if (!capturing_) return;
    for (int count = 0; count < kPollFrameBudget; ++count) {
        std::vector<std::uint8_t> frame;
        std::string error;
        const auto result = transport_.receive(frame, error);
        if (result == ar::iec61850::goose::RawEthernetReceiveResult::none) break;
        if (result == ar::iec61850::goose::RawEthernetReceiveResult::failure) {
            failCapture(QString::fromStdString(error));
            return;
        }
        static_cast<void>(ingestFrameInternal(
            frame,
            clock::now(),
            std::chrono::system_clock::now()));
    }

    const qint64 now = monitorClock_.elapsed();
    if (now - lastExpiryScanMilliseconds_ >= kExpiryScanIntervalMilliseconds) {
        lastExpiryScanMilliseconds_ = now;
        checkTimeoutsAt(clock::now());
    }
}

void GooseMonitorController::appendEvent(QString event) {
    if (event.isEmpty()) return;
    while (recentEvents_.size() >= kEventCapacity) recentEvents_.removeFirst();
    recentEvents_.push_back(std::move(event));
}

void GooseMonitorController::retainPacket(
    const std::span<const std::uint8_t> frameBytes,
    const std::chrono::system_clock::time_point timestamp) {
    while (static_cast<int>(retainedPackets_.size()) >= kRetainedPacketCapacity) {
        retainedPackets_.pop_front();
    }
    retainedPackets_.push_back(ar::iec61850::capture::PcapPacket{
        timestamp,
        std::vector<std::uint8_t>{frameBytes.begin(), frameBytes.end()}});
}

bool GooseMonitorController::exportPcap(const QUrl& fileUrl) {
    const auto localFile = fileUrl.toLocalFile();
    if (localFile.isEmpty()) {
        statusText_ = QStringLiteral("PCAP export requires a local file path.");
        emit monitorChanged();
        return false;
    }
    if (retainedPackets_.empty()) {
        statusText_ = QStringLiteral("No retained GOOSE packets to export.");
        emit monitorChanged();
        return false;
    }
    try {
        std::vector<ar::iec61850::capture::PcapPacket> packets;
        packets.reserve(retainedPackets_.size());
        for (const auto& packet : retainedPackets_) packets.push_back(packet);
        ar::iec61850::capture::PcapWriter::write_all(
            std::filesystem::path{localFile.toStdString()}, packets);
        statusText_ = QStringLiteral("Saved %1 GOOSE packets to %2")
            .arg(packets.size())
            .arg(localFile);
        appendEvent(statusText_);
        emit monitorChanged();
        return true;
    } catch (const std::exception& error) {
        statusText_ = QStringLiteral("PCAP export failed: %1").arg(QString::fromUtf8(error.what()));
        appendEvent(statusText_);
        emit monitorChanged();
        return false;
    }
}

void GooseMonitorController::failCapture(const QString& reason) {
    if (captureTimer_ != nullptr) captureTimer_->stop();
    transport_.close();
    capturing_ = false;
    statusText_ = QStringLiteral("Capture stopped: %1").arg(reason);
    appendEvent(statusText_);
    emit monitorChanged();
}
