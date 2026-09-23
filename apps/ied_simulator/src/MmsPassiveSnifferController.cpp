// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsPassiveSnifferController.hpp"

#include "ariec61850/capture/pcap.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QMetaObject>
#include <QNetworkInterface>
#include <QPointer>

#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <utility>

namespace capture = ar::iec61850::capture;

namespace {
std::filesystem::path nativePath(const QString& path) {
#ifdef _WIN32
    return std::filesystem::path{path.toStdWString()};
#else
    return std::filesystem::path{path.toStdString()};
#endif
}
bool matchesFilter(const QVariantMap& item, const QString& filter) {
    if (filter == QStringLiteral("All")) return true;
    const auto service = item.value(QStringLiteral("service")).toString();
    const auto kind = item.value(QStringLiteral("kind")).toString();
    if (filter == QStringLiteral("Reports")) {
        return service == QStringLiteral("InformationReport");
    }
    if (filter == QStringLiteral("Read")) return service == QStringLiteral("Read");
    if (filter == QStringLiteral("Write")) return service == QStringLiteral("Write");
    if (filter == QStringLiteral("Control")) return service == QStringLiteral("Control");
    if (filter == QStringLiteral("Association")) {
        return kind == QStringLiteral("Association") ||
            kind == QStringLiteral("OSI") ||
            kind.startsWith(QStringLiteral("Initiate"));
    }
    if (filter == QStringLiteral("Errors")) {
        return kind == QStringLiteral("Decode error") ||
            kind == QStringLiteral("Error") ||
            kind == QStringLiteral("Reject");
    }
    return false;
}
} // namespace

MmsPassiveSnifferController::MmsPassiveSnifferController(QObject* parent)
    : QObject(parent) {
    timer_.setInterval(kPollIntervalMs);
    timer_.setTimerType(Qt::PreciseTimer);
    connect(&timer_, &QTimer::timeout, this,
            &MmsPassiveSnifferController::refreshLive);
    workerPool_.setMaxThreadCount(1);
    workerPool_.setExpiryTimeout(-1);
}

MmsPassiveSnifferController::~MmsPassiveSnifferController() {
    ++generation_;
    timer_.stop();
    receiver_.close();
    workerPool_.clear();
    workerPool_.waitForDone();
}

QStringList MmsPassiveSnifferController::interfaces() const {
    QStringList result;
    for (const auto& adapter : QNetworkInterface::allInterfaces()) {
        if (adapter.flags().testFlag(QNetworkInterface::IsLoopBack)) continue;
        if (!adapter.flags().testFlag(QNetworkInterface::IsUp)) continue;
        result.push_back(adapter.name());
    }
    result.removeDuplicates();
    result.sort();
    return result;
}

void MmsPassiveSnifferController::setInterfaceName(const QString& value) {
    if (capturing_ || busy_) return;
    const auto normalized = value.trimmed();
    if (interfaceName_ == normalized) return;
    interfaceName_ = normalized;
    emit stateChanged();
}

void MmsPassiveSnifferController::setFilter(const QString& value) {
    static const QStringList allowed{
        QStringLiteral("All"), QStringLiteral("Reports"),
        QStringLiteral("Read"), QStringLiteral("Write"),
        QStringLiteral("Control"), QStringLiteral("Association"),
        QStringLiteral("Errors")};
    if (!allowed.contains(value) || filter_ == value) return;
    filter_ = value;
    selectedIndex_ = -1;
    emit eventsChanged();
    emit selectionChanged();
}

QVariantMap MmsPassiveSnifferController::eventMap(
    const capture::PassiveMmsEvent& item) {
    QVariantMap result;
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            item.timestamp.time_since_epoch()).count();
    result.insert(QStringLiteral("timestamp"),
        QDateTime::fromMSecsSinceEpoch(milliseconds).toString(Qt::ISODateWithMs));
    result.insert(QStringLiteral("source"), QString::fromStdString(item.source));
    result.insert(QStringLiteral("destination"), QString::fromStdString(item.destination));
    result.insert(QStringLiteral("kind"), QString::fromStdString(item.kind));
    result.insert(QStringLiteral("service"), QString::fromStdString(item.service));
    result.insert(QStringLiteral("detail"), QString::fromStdString(item.detail));
    result.insert(QStringLiteral("invokeId"),
        item.invoke_id ? QString::number(*item.invoke_id) : QStringLiteral("—"));
    result.insert(QStringLiteral("bytes"), static_cast<qulonglong>(item.wire_bytes));
    return result;
}

QVariantMap MmsPassiveSnifferController::counterMap(
    const capture::PassiveMmsCounters& item) {
    return {
        {QStringLiteral("ethernet"), static_cast<qulonglong>(item.ethernet_packets)},
        {QStringLiteral("tcp"), static_cast<qulonglong>(item.tcp_packets)},
        {QStringLiteral("mms"), static_cast<qulonglong>(item.mms_messages)},
        {QStringLiteral("requests"), static_cast<qulonglong>(item.decoded_requests)},
        {QStringLiteral("responses"), static_cast<qulonglong>(item.decoded_responses)},
        {QStringLiteral("reports"), static_cast<qulonglong>(item.decoded_reports)},
        {QStringLiteral("retransmissions"), static_cast<qulonglong>(item.retransmissions)},
        {QStringLiteral("outOfOrder"), static_cast<qulonglong>(item.out_of_order)},
        {QStringLiteral("gaps"), static_cast<qulonglong>(item.gaps)},
        {QStringLiteral("malformed"), static_cast<qulonglong>(item.malformed)},
        {QStringLiteral("dropped"), static_cast<qulonglong>(item.dropped)}
    };
}

QVariantList MmsPassiveSnifferController::events() const {
    if (filter_ == QStringLiteral("All")) return rows_;
    QVariantList filtered;
    for (const auto& row : rows_) {
        if (matchesFilter(row.toMap(), filter_)) filtered.push_back(row);
    }
    return filtered;
}

QVariantMap MmsPassiveSnifferController::counters() const {
    return counters_;
}

QVariantMap MmsPassiveSnifferController::selectedEvent() const {
    const auto visible = events();
    return selectedIndex_ >= 0 && selectedIndex_ < visible.size()
        ? visible.at(selectedIndex_).toMap() : QVariantMap{};
}

void MmsPassiveSnifferController::select(int index) {
    const auto visible = events();
    selectedIndex_ = index >= 0 && index < visible.size() ? index : -1;
    emit selectionChanged();
}

void MmsPassiveSnifferController::clear() {
    if (busy_) return;
    decoder_.reset();
    rows_.clear();
    counters_ = counterMap(decoder_.counters());
    selectedIndex_ = -1;
    lastError_.clear();
    emit eventsChanged();
    emit selectionChanged();
    emit stateChanged();
}

bool MmsPassiveSnifferController::startCapture() {
    if (busy_ || capturing_ || interfaceName_.isEmpty()) return false;
    if (!interfaces().contains(interfaceName_)) {
        lastError_ = QStringLiteral("Select an available active network interface.");
        emit stateChanged();
        return false;
    }
    std::string error;
    if (!receiver_.open(interfaceName_.toStdString(), error)) {
        lastError_ = QString::fromStdString(error);
        statusText_ = QStringLiteral("Capture unavailable");
        emit stateChanged();
        return false;
    }
    decoder_.reset();
    rows_.clear();
    selectedIndex_ = -1;
    counters_ = counterMap(decoder_.counters());
    lastError_.clear();
    capturing_ = true;
    statusText_ = QStringLiteral("Passive TCP/102 capture");
    timer_.start();
    emit stateChanged();
    emit eventsChanged();
    emit selectionChanged();
    return true;
}

void MmsPassiveSnifferController::stopCapture() {
    timer_.stop();
    receiver_.close();
    capturing_ = false;
    statusText_ = QStringLiteral("Stopped");
    emit stateChanged();
}

void MmsPassiveSnifferController::failCapture(const QString& error) {
    stopCapture();
    lastError_ = error;
    statusText_ = QStringLiteral("Capture error");
    emit stateChanged();
}

void MmsPassiveSnifferController::refreshLive() {
    if (!capturing_) return;
    bool changed = false;
    for (int i = 0; i < kPollBudget; ++i) {
        std::vector<std::uint8_t> frame;
        std::string error;
        const auto received = receiver_.receive(frame, error);
        if (received == capture::RawMmsReceiveResult::none) break;
        if (received == capture::RawMmsReceiveResult::failure) {
            failCapture(QString::fromStdString(error));
            return;
        }
        decoder_.ingest(frame, std::chrono::system_clock::now());
        changed = true;
    }
    if (!changed) return;
    rows_.clear();
    for (const auto& item : decoder_.events()) {
        rows_.push_back(eventMap(item));
    }
    counters_ = counterMap(decoder_.counters());
    if (selectedIndex_ >= events().size()) {
        selectedIndex_ = -1;
        emit selectionChanged();
    }
    emit eventsChanged();
}

bool MmsPassiveSnifferController::importPcap(const QUrl& url) {
    if (capturing_ || busy_) return false;
    if (!url.isLocalFile()) {
        lastError_ = QStringLiteral("PCAP import requires a local file.");
        emit stateChanged();
        return false;
    }
    const auto path = url.toLocalFile();
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 ||
        static_cast<std::uintmax_t>(info.size()) > kMaxPcapBytes) {
        lastError_ = QStringLiteral("PCAP must be a local Ethernet capture of 1..64 MiB.");
        emit stateChanged();
        return false;
    }
    const auto generation = ++generation_;
    const QPointer<MmsPassiveSnifferController> self{this};
    busy_ = true;
    lastError_.clear();
    statusText_ = QStringLiteral("Decoding PCAP…");
    emit stateChanged();

    workerPool_.start([self, path, generation] {
        try {
            const auto packets = capture::PcapReader::read_all(nativePath(path));
            if (packets.size() > kMaxPcapPackets) {
                throw std::runtime_error("PCAP packet count exceeds the 100000 packet bound.");
            }
            capture::PassiveMmsDecoder local;
            for (const auto& packet : packets) local.ingest(packet);
            QVariantList rows;
            for (const auto& item : local.events()) rows.push_back(eventMap(item));
            const auto counters = counterMap(local.counters());
            if (self) {
                QMetaObject::invokeMethod(
                    self, [self, generation, rows, counters] {
                        if (!self || self->generation_ != generation) return;
                        self->rows_ = rows;
                        self->counters_ = counters;
                        self->selectedIndex_ = -1;
                        self->busy_ = false;
                        self->statusText_ = QStringLiteral("Offline PCAP decoded");
                        emit self->eventsChanged();
                        emit self->selectionChanged();
                        emit self->stateChanged();
                    }, Qt::QueuedConnection);
            }
        } catch (const std::exception& ex) {
            if (self) {
                const auto message = QString::fromUtf8(ex.what());
                QMetaObject::invokeMethod(
                    self, [self, generation, message] {
                        if (!self || self->generation_ != generation) return;
                        self->busy_ = false;
                        self->lastError_ = message;
                        self->statusText_ = QStringLiteral("PCAP import failed");
                        emit self->stateChanged();
                    }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}
