// SPDX-License-Identifier: GPL-3.0-or-later
#include "SvCaptureController.hpp"

#include <QtConcurrentRun>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>
#include <QTimer>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <limits>
#include <utility>

namespace {
// Capture SV + native L2 PTP in ONE time axis: earlier SV-only captures
// cannot establish whether a smpSynch transition is supported by PTP traffic.
constexpr auto kSvBpf =
    "(ether[12:2]=0x88ba or ether[12:2]=0x88f7) or "
    "((ether[12:2]=0x8100 or ether[12:2]=0x88a8) and "
    "(ether[16:2]=0x88ba or ether[16:2]=0x88f7)) or "
    "((ether[12:2]=0x8100 or ether[12:2]=0x88a8) and "
    "(ether[16:2]=0x8100 or ether[16:2]=0x88a8) and "
    "(ether[20:2]=0x88ba or ether[20:2]=0x88f7))";
constexpr std::array<const char*, 8> kChannels{"IA", "IB", "IC", "IN", "UA", "UB", "UC", "UN"};

quint16 be16(const uchar* b) {
    return quint16((quint16(b[0]) << 8) | quint16(b[1]));
}
quint32 be32(const uchar* b) {
    return (quint32(b[0]) << 24) | (quint32(b[1]) << 16) |
           (quint32(b[2]) << 8) | quint32(b[3]);
}
quint32 le32(const uchar* b) {
    return (quint32(b[3]) << 24) | (quint32(b[2]) << 16) |
           (quint32(b[1]) << 8) | quint32(b[0]);
}
QString mac(const uchar* b) {
    QStringList parts;
    for (int i = 0; i < 6; ++i)
        parts.append(QString::number(b[i], 16).rightJustified(2, QLatin1Char('0')));
    return parts.join(QLatin1Char(':')).toUpper();
}

// Strict, bounded BER TLV reader. Only definite lengths are accepted.
bool tlv(const QByteArrayView data, qsizetype& offset, quint8& tag, QByteArrayView& value) {
    if (offset + 2 > data.size()) return false;
    const auto* p = reinterpret_cast<const uchar*>(data.data());
    tag = p[offset++];
    qsizetype len = p[offset++];
    if (len & 0x80) {
        const int octets = int(len & 0x7f);
        if (octets < 1 || octets > 3 || offset + octets > data.size()) return false;
        len = 0;
        for (int i = 0; i < octets; ++i) len = (len << 8) | p[offset++];
    }
    if (len < 0 || len > data.size() - offset) return false;
    value = data.sliced(offset, len);
    offset += len;
    return true;
}
struct Sample {
    QString svId;
    quint16 appId{};
    quint32 count{};
    quint32 synch{};
    std::array<qint32, 8> values{};
    std::array<quint32, 8> qualities{};
    QString src;
    QString dst;
};
bool parseSvFrame(const QByteArrayView frame, Sample& sample) {
    if (frame.size() < 22) return false;
    const auto* p = reinterpret_cast<const uchar*>(frame.data());
    qsizetype at = 12;
    quint16 proto = be16(p + at);
    for (int i = 0; i < 2 && (proto == 0x8100 || proto == 0x88a8); ++i) {
        at += 4;
        if (frame.size() < at + 2) return false;
        proto = be16(p + at);
    }
    if (proto != 0x88ba || frame.size() < at + 10) return false;
    sample.dst = mac(p);
    sample.src = mac(p + 6);
    sample.appId = be16(p + at + 2);
    const auto declared = be16(p + at + 4);
    if (declared < 8 || declared > frame.size() - at - 2) return false;
    QByteArrayView apdu = frame.sliced(at + 10, declared - 8);
    qsizetype pos = 0;
    quint8 tag{};
    QByteArrayView outer;
    if (!tlv(apdu, pos, tag, outer) || tag != 0x60 || pos != apdu.size()) return false;
    QByteArrayView asdus;
    pos = 0;
    bool hasSequence = false;
    while (pos < outer.size()) {
        QByteArrayView field;
        if (!tlv(outer, pos, tag, field)) return false;
        if (tag == 0xa2) {
            if (hasSequence) return false;
            hasSequence = true;
            asdus = field;
        }
    }
    if (!hasSequence) return false;
    pos = 0;
    QByteArrayView asdu;
    if (!tlv(asdus, pos, tag, asdu) || tag != 0x30 || pos != asdus.size()) return false;
    pos = 0;
    bool idSeen = false, countSeen = false, payloadSeen = false;
    while (pos < asdu.size()) {
        QByteArrayView value;
        if (!tlv(asdu, pos, tag, value)) return false;
        const auto* bytes = reinterpret_cast<const uchar*>(value.data());
        if (tag == 0x80) {
            if (idSeen || value.isEmpty() || value.size() > 128) return false;
            sample.svId = QString::fromLatin1(value.data(), value.size());
            if (sample.svId.contains(QLatin1Char('"'))) return false;
            idSeen = true;
        } else if (tag == 0x82) {
            if (countSeen || value.isEmpty() || value.size() > 3) return false;
            sample.count = 0;
            for (const auto ch : value) sample.count = (sample.count << 8) | quint8(ch);
            countSeen = true;
        } else if (tag == 0x85) {
            if (value.isEmpty() || value.size() > 2) return false;
            sample.synch = 0;
            for (const auto ch : value) sample.synch = (sample.synch << 8) | quint8(ch);
        } else if (tag == 0x87) {
            if (payloadSeen || value.size() != 64) return false;
            for (int i = 0; i < 8; ++i) {
                const quint32 raw = be32(bytes + i * 8);
                sample.values[i] = raw <= INT32_MAX ? qint32(raw)
                    : qint32(qint64(raw) - 0x100000000LL);
                sample.qualities[i] = be32(bytes + i * 8 + 4);
            }
            payloadSeen = true;
        }
    }
    return idSeen && countSeen && payloadSeen;
}
QString formatError(const QString& prefix, const QString& detail) {
    return prefix + QStringLiteral(": ") + detail;
}

// Capture remains a single Studio action. The external driver/recorder is
// discovered automatically; the operator never enters a shell command.
QString findDumpcap() {
    QString found = QStandardPaths::findExecutable(QStringLiteral("dumpcap"));
#ifdef Q_OS_WIN
    if (found.isEmpty()) {
        for (const QString& path : {
                 QStringLiteral("C:/Program Files/Wireshark/dumpcap.exe"),
                 QStringLiteral("C:/Program Files (x86)/Wireshark/dumpcap.exe")}) {
            if (QFileInfo::exists(path)) { found = path; break; }
        }
    }
#endif
    return found;
}
} // namespace

SvCaptureController::SvCaptureController(QObject* parent) : QObject(parent) {
    dumpcap_ = findDumpcap();
    auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if (documents.isEmpty()) documents = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    outputFolder_ = QDir(documents).filePath(QStringLiteral("ARStack Studio/Captures"));
    connect(&discovery_, &QProcess::finished, this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
        if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
            setStatus(QStringLiteral("Adapter discovery failed. Install Wireshark/Npcap and check capture permissions."));
            return;
        }
        interfaceIds_.clear();
        interfaceLabels_.clear();
        suggestedInterface_ = -1;
        const QRegularExpression lineRe(QStringLiteral(R"(^\s*\d+\.\s+(\S+)(?:\s+\((.*)\))?)"));
        const QString output = QString::fromUtf8(discovery_.readAllStandardOutput());
        for (const auto& line : output.split(QLatin1Char('\n'))) {
            const auto match = lineRe.match(line);
            if (!match.hasMatch()) continue;
            const QString id = match.captured(1);
            const QString label = match.captured(2).trimmed();
            interfaceIds_.append(id);
            interfaceLabels_.append(label.isEmpty() ? id : label + QStringLiteral(" · ") + id);
        }
        int eligible = 0;
        for (int i = 0; i < interfaceLabels_.size(); ++i) {
            const QString label = interfaceLabels_.at(i).toLower();
            if (label.contains(QStringLiteral("loopback")) ||
                label.contains(QStringLiteral("bluetooth")) ||
                label.contains(QStringLiteral("wan miniport")) ||
                label.contains(QStringLiteral("wireless")) ||
                label.contains(QStringLiteral("wi-fi")) ||
                label.contains(QStringLiteral("wifi"))) continue;
            ++eligible;
            suggestedInterface_ = i;
        }
        if (eligible != 1) suggestedInterface_ = -1;
        emit interfacesChanged();
        setStatus(interfaceIds_.isEmpty()
            ? QStringLiteral("No Npcap capture interfaces found.")
            : eligible == 1
                ? QStringLiteral("Ethernet adapter identified. One-click capture is ready.")
                : QStringLiteral("Select the mirrored/TAP Ethernet interface once; automatic selection is ambiguous."));
    });
    connect(&discovery_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        setStatus(QStringLiteral("Unable to enumerate network capture adapters. Install Wireshark with Npcap."));
    });
    connect(&capture_, &QProcess::stateChanged, this, [this] { emit stateChanged(); });
    connect(&capture_, &QProcess::finished, this, &SvCaptureController::onCaptureFinished);
    connect(&capture_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            setStatus(QStringLiteral("dumpcap could not start; check Npcap privileges or installation."));
    });
    connect(&decoder_, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        const QVariantMap report = decoder_.result();
        lastReport_ = report.value(QStringLiteral("json")).toString();
        summary_ = report.value(QStringLiteral("summary")).toMap();
        setStatus(report.value(QStringLiteral("status")).toString());
        emit stateChanged();
    });
    QTimer::singleShot(0, this, &SvCaptureController::refreshInterfaces);
}

SvCaptureController::~SvCaptureController() {
    if (capture_.state() != QProcess::NotRunning) {
        capture_.kill();
        capture_.waitForFinished(1000);
    }
    if (discovery_.state() != QProcess::NotRunning) {
        discovery_.kill();
        discovery_.waitForFinished(500);
    }
    decoder_.waitForFinished(); // protects the worker's read-only file paths at shutdown
}
void SvCaptureController::setStatus(const QString& text) {
    status_ = text;
    emit statusChanged();
}
void SvCaptureController::refreshInterfaces() {
    if (discovery_.state() != QProcess::NotRunning || capturing() || processing()) return;
    // Refresh installation state too; no Studio restart is required after
    // first-time installing Npcap/Wireshark from the in-app help button.
    dumpcap_ = findDumpcap();
    if (dumpcap_.isEmpty()) {
        setStatus(QStringLiteral("Capture driver not ready. Select 'Install capture support', complete Wireshark + Npcap setup, then Refresh."));
        return;
    }
    setStatus(QStringLiteral("Finding capture interfaces..."));
    discovery_.setProgram(dumpcap_);
    discovery_.setArguments({QStringLiteral("-D")});
    discovery_.start();
}
QString SvCaptureController::captureStartBlocker(const QVariantMap& telemetry) {
    // The requested measurement is a running-SV capture, not a PTP-only trace.
    // Cached nonzero FPS must never override a STOPPED publisher readback.
    if (!telemetry.contains(QStringLiteral("running")) ||
        !telemetry.value(QStringLiteral("running")).toBool()) {
        return QStringLiteral("SV publisher is STOPPED. Start 4I+4V before "
                              "capturing; historical FPS/PTP RUNNING is not live SV.");
    }
    return {};
}

void SvCaptureController::writeInvalidCaptureReport(const QString& reason) const {
    if (activeJson_.isEmpty()) return;
    const QJsonObject report{
        {QStringLiteral("schema"), QStringLiteral("arstack.sv.capture.v2")},
        {QStringLiteral("status"), QStringLiteral("INVALID_CAPTURE")},
        {QStringLiteral("error"), reason},
        {QStringLiteral("sv_frames"), 0},
        {QStringLiteral("physical_acceptance"), false},
        {QStringLiteral("pcap"), activePcap_},
        {QStringLiteral("interface"), activeAdapter_},
        {QStringLiteral("requested_seconds"), activeSecondsRequested_},
        {QStringLiteral("studio_telemetry_snapshot_UNVERIFIED"),
         QJsonObject::fromVariantMap(activeStudioTelemetry_)},
        {QStringLiteral("studio_telemetry_series_UNVERIFIED"),
         QJsonArray::fromVariantList(activeStudioTelemetrySeries_)}
    };
    QSaveFile file(activeJson_);
    if (file.open(QIODevice::WriteOnly) &&
        file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) >= 0)
        file.commit();
}

bool SvCaptureController::startCapture(int index, int seconds, const QVariantMap& studioTelemetry) {
    const QString blocker = captureStartBlocker(studioTelemetry);
    if (!blocker.isEmpty()) {
        setStatus(QStringLiteral("CAPTURE BLOCKED: ") + blocker);
        return false;
    }

    if (capturing() || processing() || discovery_.state() != QProcess::NotRunning ||
        index < 0 || index >= interfaceIds_.size() ||
        (seconds != 30 && seconds != 120 && seconds != 3600)) {
        setStatus(QStringLiteral("Select a valid adapter and 30 s, 120 s or 3600 s capture."));
        return false;
    }
    if (!QDir().mkpath(outputFolder_)) {
        setStatus(QStringLiteral("Unable to create capture folder. Check Documents permissions."));
        return false;
    }
    const QStorageInfo storage(outputFolder_);
    const qint64 required = qint64(seconds + 5) * 4000 * 240 + 128LL * 1024 * 1024;
    if (!storage.isValid() || storage.bytesAvailable() < required) {
        setStatus(QStringLiteral("Insufficient capture disk space (budget: %1 MB).")
                      .arg(required / 1024 / 1024));
        return false;
    }
    const QString filename = QStringLiteral("ARStack_SV_%1")
        .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")));
    activePcap_ = QDir(outputFolder_).filePath(filename + QStringLiteral(".pcap"));
    activeCsv_ = QDir(outputFolder_).filePath(filename + QStringLiteral("_samples.csv"));
    activeSeconds_ = QDir(outputFolder_).filePath(filename + QStringLiteral("_seconds.csv"));
    activeEvents_ = QDir(outputFolder_).filePath(filename + QStringLiteral("_events.csv"));
    activeJson_ = QDir(outputFolder_).filePath(filename + QStringLiteral("_report.json"));
    activeAdapter_ = interfaceIds_.at(index);
    activeStudioTelemetry_ = studioTelemetry;
    activeStudioTelemetrySeries_.clear();
    activeStudioTelemetrySeries_.append(studioTelemetry);
    activeSecondsRequested_ = seconds;
    abortRequested_ = false;
    captureInterruptionReason_.clear();
    lastReport_.clear();
    summary_.clear();
    const QStringList args{
        QStringLiteral("-i"), activeAdapter_,
        QStringLiteral("-f"), QString::fromLatin1(kSvBpf),
        QStringLiteral("-s"), QStringLiteral("0"),
        QStringLiteral("-P"), // classic PCAP, not default PCAPNG
        QStringLiteral("-a"), QStringLiteral("duration:%1").arg(seconds + 5),
        QStringLiteral("-w"), activePcap_
    };
    capture_.setProgram(dumpcap_);
    capture_.setArguments(args);
    capture_.start();
    setStatus(QStringLiteral("Capturing filtered SV on %1 for %2 seconds. Do not disconnect TAP/SPAN.")
                  .arg(activeAdapter_).arg(seconds));
    emit stateChanged();
    return true;
}
void SvCaptureController::sampleTelemetry(const QVariantMap& passiveSnapshot) {
    // Bounded passive read of already-published QML device properties; no
    // console commands, network polling or intervention in active SV TX.
    if (!capturing() || activeStudioTelemetrySeries_.size() >= 4000) return;
    QVariantMap entry = passiveSnapshot;
    entry.insert(QStringLiteral("observed_utc"),
        QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    activeStudioTelemetrySeries_.append(entry);
    if (!captureStartBlocker(entry).isEmpty()) {
        // A STOPPED publisher invalidates a continuous-SV measurement.
        // Keep the raw PCAP forensics, never label it a successful capture.
        captureInterruptionReason_ = QStringLiteral(
            "SV publisher STOPPED during capture. Inspect device SVLIFE "
            "stop cause; packets recorded before STOP are only partial evidence.");
        abortRequested_ = true;
        capture_.terminate();
        setStatus(QStringLiteral("CAPTURE INVALID: ") + captureInterruptionReason_);
    }
}
void SvCaptureController::abortCapture() {
    if (!capturing()) return;
    abortRequested_ = true;
    capture_.terminate();
    QTimer::singleShot(2500, &capture_, [this] {
        if (capture_.state() != QProcess::NotRunning) capture_.kill();
    });
    setStatus(QStringLiteral("Capture aborted; incomplete PCAP is not accepted as evidence."));
}
void SvCaptureController::onCaptureFinished(int exitCode, QProcess::ExitStatus exitStatus) {
    emit stateChanged();
    if (abortRequested_) {
        const QString reason = captureInterruptionReason_.isEmpty()
            ? QStringLiteral("Capture manually canceled before completion.")
            : captureInterruptionReason_;
        writeInvalidCaptureReport(reason);
        setStatus(QStringLiteral("CAPTURE INVALID: ") + reason +
                  QStringLiteral(" See _report.json."));
        return;
    }
    if (exitCode != 0 || exitStatus != QProcess::NormalExit ||
        QFileInfo(activePcap_).size() < 64) {
        const QString reason = QStringLiteral(
            "Zero Ethernet SV/PTP packets: PCAP header only or dumpcap failure. "
            "Check SV RUNNING, physical capture NIC/TAP-SPAN, multicast "
            "forwarding and Npcap. dumpcap: ") +
            QString::fromUtf8(capture_.readAllStandardError()).right(360);
        writeInvalidCaptureReport(reason);
        setStatus(QStringLiteral("CAPTURE INVALID: ") + reason +
                  QStringLiteral(" See _report.json."));
        return;
    }
    decodeCapturedFile();
}
void SvCaptureController::decodeCapturedFile() {
    setStatus(QStringLiteral("Writing filtered SV CSV and JSON report (off the UI thread)..."));
    decoder_.setFuture(QtConcurrent::run(&SvCaptureController::decodePcap,
        activePcap_, activeCsv_, activeSeconds_, activeEvents_, activeJson_, activeSecondsRequested_, activeAdapter_,
        activeStudioTelemetry_, activeStudioTelemetrySeries_));
    emit stateChanged();
}
void SvCaptureController::openOutputFolder() {
    if (!QDir().mkpath(outputFolder_)) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(outputFolder_));
}
void SvCaptureController::openCaptureSupportHelp() {
    // Operator-only setup action. Do not silently install kernel drivers or
    // download executable binaries without explicit system authorization.
    QDesktopServices::openUrl(
        QUrl(QStringLiteral("https://www.wireshark.org/download.html")));
}

QVariantMap SvCaptureController::decodePcap(
    const QString& pcapFile, const QString& csvFile, const QString& secondsFile,
    const QString& eventsFile, const QString& jsonFile, int requestedSeconds, const QString& adapter,
    const QVariantMap& studioTelemetry,
    const QVariantList& studioTelemetrySeries) {
    QVariantMap result;
    result.insert(QStringLiteral("json"), jsonFile);
    result.insert(QStringLiteral("status"), QStringLiteral("Capture failed: PCAP parser error."));
    QFile in(pcapFile);
    if (!in.open(QIODevice::ReadOnly)) {
        result.insert(QStringLiteral("status"), QStringLiteral("Failed to open captured PCAP."));
        return result;
    }
    const QByteArray global = in.read(24);
    if (global.size() != 24) {
        result.insert(QStringLiteral("status"), QStringLiteral("Truncated PCAP header."));
        return result;
    }
    const QByteArray signature = global.left(4);
    const bool little = signature == QByteArray::fromHex("d4c3b2a1") ||
                        signature == QByteArray::fromHex("4d3cb2a1");
    const bool big = signature == QByteArray::fromHex("a1b2c3d4") ||
                     signature == QByteArray::fromHex("a1b23c4d");
    if (!little && !big) {
        result.insert(QStringLiteral("status"), QStringLiteral("Capture is not classic PCAP."));
        return result;
    }
    const bool nanoseconds = signature == QByteArray::fromHex("4d3cb2a1") ||
                             signature == QByteArray::fromHex("a1b23c4d");
    const auto word = [little](const uchar* p) { return little ? le32(p) : be32(p); };
    const auto* gh = reinterpret_cast<const uchar*>(global.constData());
    if (word(gh + 20) != 1) {
        result.insert(QStringLiteral("status"), QStringLiteral("Capture link type must be Ethernet."));
        return result;
    }

    QFile csv(csvFile);
    QFile secondsCsv(secondsFile);
    QFile eventsCsv(eventsFile);
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        !secondsCsv.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        !eventsCsv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.insert(QStringLiteral("status"), QStringLiteral("Cannot create CSV files in Documents."));
        return result;
    }
    QTextStream stream(&csv);
    QTextStream secondsStream(&secondsCsv);
    QTextStream eventStream(&eventsCsv);
    eventStream << "timestamp_us,event,source_mac,sv_id,previous_smp_cnt,"
                   "current_smp_cnt,missing_estimate,host_delta_us,detail\n";
    stream << "timestamp_us,source_mac,destination_mac,appid,sv_id,smp_cnt,smp_synch";
    for (const auto* name : kChannels)
        stream << ',' << name << ',' << name << "_quality";
    stream << '\n';
    secondsStream << "utc_second,sv_frames,missing_smpcnt,duplicates,malformed\n";
    // Preserve every raw PCAP packet. 3600s CSV exports only every 20th
    // validated frame; per-second counters always use every frame.
    const quint64 csvStride = requestedSeconds >= 3600 ? 20 : 1;
    // The frequency is operator context, NOT wire authority. Every numerical
    // measurement below is computed from the actual 8x64-bit SV wire payload.
    bool frequencyValid = false;
    const double intendedHz = studioTelemetry
        .value(QStringLiteral("operator_frequency_hz_UNVERIFIED"))
        .toDouble(&frequencyValid);
    frequencyValid = frequencyValid && std::isfinite(intendedHz) &&
                     intendedHz > 0.0 && intendedHz <= 1000.0;
    std::array<double, 4000> sineBasis{}, cosineBasis{};
    if (frequencyValid) {
        for (std::size_t i = 0; i < sineBasis.size(); ++i) {
            const double rad = 2.0 * std::numbers::pi * intendedHz *
                static_cast<double>(i) / 4000.0;
            sineBasis[i] = std::sin(rad);
            cosineBasis[i] = std::cos(rad);
        }
    }
    std::array<double, 8> powerSums{}, sums{}, sinSums{}, cosSums{};
    std::array<quint64, 8> nonzeroQualityCounts{};
    std::array<qint32, 8> minimums{}, maximums{};
    quint64 hostGapEvents = 0, maximumHostGapUs = 0;
    quint64 matched = 0, malformed = 0, missing = 0, duplicates = 0;
    quint64 firstTimestamp = 0, lastTimestamp = 0, csvRows = 0;
    QHash<QString, quint32> previousCounters;
    QHash<QString, quint32> previousSync;
    QHash<QString, quint64> previousTimestamps;
    quint64 ptpFrames = 0, malformedPtp = 0, syncTransitions = 0;
    quint64 sampleJumpEvents = 0, backwardOrResetEvents = 0;
    QVariantMap ptpMessageCounts;
    QVariantList transitionTimeline;
    quint64 lastBucket = std::numeric_limits<quint64>::max();
    quint64 bucketFrames = 0, bucketMissing = 0, bucketDuplicates = 0, bucketMalformed = 0;
     QVariantMap syncCounts;
    QVariantMap streams;
    QString parseFailure;
    constexpr quint32 kSampleWrap = 4000;

    const auto finishBucket = [&] {
        if (lastBucket == std::numeric_limits<quint64>::max()) return;
        secondsStream << lastBucket << ',' << bucketFrames << ',' << bucketMissing
                      << ',' << bucketDuplicates << ',' << bucketMalformed << '\n';
        bucketFrames = bucketMissing = bucketDuplicates = bucketMalformed = 0;
    };
    while (!in.atEnd()) {
        const QByteArray header = in.read(16);
        if (header.isEmpty()) break;
        if (header.size() != 16) {
            parseFailure = QStringLiteral("Truncated PCAP record header.");
            break;
        }
        const auto* ph = reinterpret_cast<const uchar*>(header.constData());
        const quint32 sec = word(ph), sub = word(ph + 4);
        const quint32 captured = word(ph + 8), original = word(ph + 12);
        if (captured > 65535 || captured < 14 || original < captured ||
            sub >= (nanoseconds ? 1000000000U : 1000000U)) {
            parseFailure = QStringLiteral("Invalid PCAP packet length/timestamp.");
            break;
        }
        const QByteArray data = in.read(captured);
        if (data.size() != qsizetype(captured)) {
            parseFailure = QStringLiteral("Truncated PCAP packet.");
            break;
        }
        const quint64 timestamp = quint64(sec) * 1000000ULL +
                                  (nanoseconds ? sub / 1000U : sub);
        const quint64 bucket = sec;
        if (lastBucket != bucket) {
            finishBucket();
            lastBucket = bucket;
        }
        // PTP EtherType 0x88F7 is explicitly observed, not mislabeled as
        // malformed SV. Its message header is parsed defensively.
        const auto* ethernet = reinterpret_cast<const uchar*>(data.constData());
        qsizetype ethernetTypeAt = 12;
        quint16 ethernetType = be16(ethernet + ethernetTypeAt);
        for (int tag = 0; tag < 2 &&
             (ethernetType == 0x8100 || ethernetType == 0x88a8); ++tag) {
            ethernetTypeAt += 4;
            if (data.size() < ethernetTypeAt + 2) break;
            ethernetType = be16(ethernet + ethernetTypeAt);
        }
        if (ethernetType == 0x88f7) {
            const qsizetype ptpStart = ethernetTypeAt + 2;
            const uchar* ptp = ethernet + ptpStart;
            if (data.size() < ptpStart + 34 ||
                (ptp[1] & 0x0fU) != 2U ||
                be16(ptp + 2) < 34U ||
                ptpStart + be16(ptp + 2) > data.size()) {
                ++malformedPtp;
                continue;
            }
            ++ptpFrames;
            const int type = ptp[0] & 0x0fU;
            const QString message = type == 0 ? QStringLiteral("SYNC")
                : type == 8 ? QStringLiteral("FOLLOW_UP")
                : type == 11 ? QStringLiteral("ANNOUNCE")
                : QStringLiteral("TYPE_%1").arg(type);
            ptpMessageCounts.insert(message, ptpMessageCounts.value(message).toULongLong() + 1);
            if (type == 0 || type == 8 || type == 11) {
                eventStream << timestamp << ",PTP_" << message << ','
                            << mac(ethernet + 6) << ",,,,0,0,"
                            << "domain=" << int(ptp[4])
                            << ";sequence=" << be16(ptp + 30) << '\n';
            }
            continue;
        }
        if (ethernetType != 0x88ba) {
            // A BPF-matching unrecognized EtherType is not SV evidence.
            ++malformed;
            ++bucketMalformed;
            continue;
        }
        Sample record;
        if (!parseSvFrame(QByteArrayView(data), record)) {
            ++malformed;
            ++bucketMalformed;
            continue;
        }
        if (matched == 0) firstTimestamp = timestamp;
        lastTimestamp = timestamp;
        ++matched;
        ++bucketFrames;
        const auto syncKey = QString::number(record.synch);
        syncCounts.insert(syncKey, syncCounts.value(syncKey).toULongLong() + 1);
        const QString streamKey = record.src + QLatin1Char('/') +
                                  QString::number(record.appId) + QLatin1Char('/') + record.svId;
        streams.insert(streamKey, streams.value(streamKey).toULongLong() + 1);
        const auto previous = previousCounters.constFind(streamKey);
        if (previous != previousCounters.cend()) {
            const quint32 diff = (record.count + kSampleWrap - previous.value()) % kSampleWrap;
            const auto oldTimestamp = previousTimestamps.value(streamKey);
            const quint64 hostDelta = timestamp >= oldTimestamp ? timestamp - oldTimestamp : 0;
            maximumHostGapUs = std::max(maximumHostGapUs, hostDelta);
            if (hostDelta > 1000) {
                ++hostGapEvents;
                eventStream << timestamp << ",HOST_CAPTURE_INTERVAL,"
                            << record.src << ',' << record.svId << ','
                            << previous.value() << ',' << record.count
                            << ",0," << hostDelta
                            << ",host_timestamp_not_hardware_egress\n";
            }
            if (diff == 0) {
                ++duplicates;
                ++bucketDuplicates;
                eventStream << timestamp << ",DUPLICATE," << record.src << ','
                            << record.svId << ',' << previous.value() << ','
                            << record.count << ",0," << hostDelta
                            << ",repeat_counter\n";
            } else if (diff != 1) {
                ++sampleJumpEvents;
                // Large modulo jumps may indicate a reset/reordered stream,
                // not thousands of missing publisher packets.
                if (diff > kSampleWrap / 2) {
                    ++backwardOrResetEvents;
                    eventStream << timestamp << ",COUNTER_RESET_OR_REORDER,"
                                << record.src << ',' << record.svId << ','
                                << previous.value() << ',' << record.count
                                << ",0," << hostDelta << ",ambiguous_backward_jump\n";
                } else {
                    missing += diff - 1;
                    bucketMissing += diff - 1;
                    eventStream << timestamp << ",SAMPLE_GAP,"
                                << record.src << ',' << record.svId << ','
                                << previous.value() << ',' << record.count
                                << ',' << diff - 1 << ',' << hostDelta
                                << ",capture_cannot_prove_where_frames_were_lost\n";
                }
            }
        }
        const auto oldSync = previousSync.constFind(streamKey);
        if (oldSync != previousSync.cend() && oldSync.value() != record.synch) {
            ++syncTransitions;
            eventStream << timestamp << ",SMPSYNCH_CHANGE,"
                        << record.src << ',' << record.svId
                        << ',' << record.count << ',' << record.count << ",0,0,"
                        << oldSync.value() << "_to_" << record.synch << '\n';
            if (transitionTimeline.size() < 256) {
                transitionTimeline.append(QVariantMap{
                    {QStringLiteral("timestamp_us"), qulonglong(timestamp)},
                    {QStringLiteral("from"), oldSync.value()},
                    {QStringLiteral("to"), record.synch},
                    {QStringLiteral("smp_cnt"), record.count}
                });
            }
        }
        previousCounters.insert(streamKey, record.count);
        previousSync.insert(streamKey, record.synch);
        previousTimestamps.insert(streamKey, timestamp);
        for (std::size_t i = 0; i < kChannels.size(); ++i) {
            const auto value = record.values[i];
            if (matched == 1) minimums[i] = maximums[i] = value;
            else {
                minimums[i] = std::min(minimums[i], value);
                maximums[i] = std::max(maximums[i], value);
            }
            const double x = static_cast<double>(value);
            powerSums[i] += x * x;
            sums[i] += x;
            if (frequencyValid && record.count < 4000U) {
                sinSums[i] += x * sineBasis[record.count];
                cosSums[i] += x * cosineBasis[record.count];
            }
            if (record.qualities[i] != 0U) ++nonzeroQualityCounts[i];
        }
        if ((matched - 1) % csvStride != 0) continue;
        ++csvRows;
        stream << timestamp << ',' << record.src << ',' << record.dst << ','
               << record.appId << ',' << record.svId << ','
               << record.count << ',' << record.synch;
        for (int i = 0; i < 8; ++i) {
            stream << ',' << record.values[i] << ",0x"
                   << QString::number(record.qualities[i], 16).rightJustified(8, QLatin1Char('0')).toUpper();
        }
        stream << '\n';
        if (stream.status() != QTextStream::Ok) {
            parseFailure = QStringLiteral("CSV write error (disk full?).");
            break;
        }
    }
    finishBucket();
    stream.flush();
    secondsStream.flush();
    eventStream.flush();
    if (csv.error() != QFile::NoError || secondsCsv.error() != QFile::NoError ||
        eventsCsv.error() != QFile::NoError)
        parseFailure = QStringLiteral("CSV output failed (disk full?).");
    csv.close();
    secondsCsv.close();
    eventsCsv.close();
    const double span = matched > 1 ? (lastTimestamp - firstTimestamp) / 1000000.0 : 0.;
    const double observedFps = span > 0 ? (matched - 1) / span : 0.;
    QVariantMap channelMeasurements;
    if (matched > 0) {
        for (std::size_t i = 0; i < kChannels.size(); ++i) {
            const double n = static_cast<double>(matched);
            QVariantMap item{
                {QStringLiteral("min_wire_counts"), minimums[i]},
                {QStringLiteral("max_wire_counts"), maximums[i]},
                {QStringLiteral("mean_wire_counts"), sums[i] / n},
                {QStringLiteral("rms_wire_counts"), std::sqrt(powerSums[i] / n)},
                {QStringLiteral("nonzero_quality_frame_count"), qulonglong(nonzeroQualityCounts[i])}
            };
            if (frequencyValid) {
                const double a = 2.0 * sinSums[i] / n;
                const double b = 2.0 * cosSums[i] / n;
                const double amplitude = std::hypot(a, b) / std::sqrt(2.0);
                item.insert(QStringLiteral("fundamental_rms_wire_counts"), amplitude);
                item.insert(QStringLiteral("fundamental_phase_degrees"),
                    amplitude <= 0.001 ? QVariant{} :
                    QVariant(std::atan2(b, a) * 180.0 / std::numbers::pi));
            }
            channelMeasurements.insert(QString::fromLatin1(kChannels[i]), item);
        }
    }
    QVariantMap report{
        {QStringLiteral("schema"), QStringLiteral("arstack.sv.capture.v2")},
        {QStringLiteral("source"), QStringLiteral("independent ethernet PCAP via dumpcap/Npcap")},
        {QStringLiteral("capture_filter"), QString::fromLatin1(kSvBpf)},
        {QStringLiteral("interface"), adapter},
        {QStringLiteral("studio_telemetry_snapshot_UNVERIFIED"), studioTelemetry},
        {QStringLiteral("studio_telemetry_series_UNVERIFIED"), studioTelemetrySeries},
        {QStringLiteral("telemetry_warning"),
         QStringLiteral("Passive Studio snapshots, not firmware timing proofs. "
                        "No extra device polling was sent during capture.")},
        {QStringLiteral("pcap"), pcapFile},
        {QStringLiteral("samples_csv"), csvFile},
        {QStringLiteral("seconds_csv"), secondsFile},
        {QStringLiteral("events_csv"), eventsFile},
        {QStringLiteral("ptp_l2_frames"), qulonglong(ptpFrames)},
        {QStringLiteral("wire_channel_measurements"), channelMeasurements},
        {QStringLiteral("fundamental_reference_hz_UNVERIFIED"),
            frequencyValid ? QVariant(intendedHz) : QVariant{}},
        {QStringLiteral("fundamental_reference_note"),
            QStringLiteral("Fundamental fits use the operator scenario frequency "
                           "and smpCnt at assumed 4000 fps; they do not establish "
                           "physical timing, phasor calibration or CT shaping. "
                           "If frequency changes mid-capture, re-capture steady state.")},
        {QStringLiteral("host_timestamp_gaps_over_1ms"), qulonglong(hostGapEvents)},
        {QStringLiteral("max_host_timestamp_gap_us"), qulonglong(maximumHostGapUs)},
        {QStringLiteral("ptp_malformed_frames"), qulonglong(malformedPtp)},
        {QStringLiteral("ptp_l2_message_counts"), ptpMessageCounts},
        {QStringLiteral("sync_transition_count"), qulonglong(syncTransitions)},
        {QStringLiteral("sync_transitions"), transitionTimeline},
        {QStringLiteral("counter_discontinuity_events"), qulonglong(sampleJumpEvents)},
        {QStringLiteral("counter_reset_or_reorder_events"), qulonglong(backwardOrResetEvents)},
        {QStringLiteral("ptp_proof_limitations"),
         QStringLiteral("PTP frames correlate local traffic only; clock lock, "
                        "smpSynch quality and exact SV egress phase remain unverified. "
                        "No PTP frames does not prove PTP was stopped.")},
        {QStringLiteral("csv_row_stride"), qulonglong(csvStride)},
        {QStringLiteral("csv_rows"), qulonglong(csvRows)},
        {QStringLiteral("requested_seconds"), requestedSeconds},
        {QStringLiteral("captured_span_seconds"), span},
        {QStringLiteral("observed_fps"), observedFps},
        {QStringLiteral("sample_count_wrap_assumed"), int(kSampleWrap)},
        {QStringLiteral("sv_frames"), qulonglong(matched)},
        {QStringLiteral("malformed_frames"), qulonglong(malformed)},
        {QStringLiteral("missing_sample_counts"), qulonglong(missing)},
        {QStringLiteral("duplicate_sample_counts"), qulonglong(duplicates)},
        {QStringLiteral("smpSynch_distribution"), syncCounts},
        {QStringLiteral("streams"), streams},
        {QStringLiteral("physical_acceptance"), false},
        {QStringLiteral("qualifications"),
         QStringLiteral("Adapter/SPAN packet drops, PTP lock, per-channel RMS/phase equivalence, "
                        "source identity, CPU and ISR measurements require independent acceptance.")},
    };
    const bool usable = parseFailure.isEmpty() && matched > 0;
    const bool anomalies = missing > 0 || duplicates > 0 ||
        malformed > 0 || malformedPtp > 0 || backwardOrResetEvents > 0;
    report.insert(QStringLiteral("status"), !usable ? QStringLiteral("INVALID_CAPTURE")
        : anomalies ? QStringLiteral("CAPTURED_WITH_ANOMALIES")
                    : QStringLiteral("CAPTURED_NOT_ACCEPTED"));
    if (!parseFailure.isEmpty()) report.insert(QStringLiteral("error"), parseFailure);
    QFile json(jsonFile);
    if (!json.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        json.write(QJsonDocument::fromVariant(report).toJson(QJsonDocument::Indented)) < 0) {
        result.insert(QStringLiteral("status"), QStringLiteral("Capture parsed but JSON report could not be written."));
        return result;
    }
    json.close();
    if (usable) {
        // The Studio operator view needs compact truthful wire facts, not
        // the full 3600-entry telemetry series kept in the evidence JSON.
        result.insert(QStringLiteral("summary"), QVariantMap{
            {QStringLiteral("sv_frames"), qulonglong(matched)},
            {QStringLiteral("ptp_frames"), qulonglong(ptpFrames)},
            {QStringLiteral("missing_samples"), qulonglong(missing)},
            {QStringLiteral("sync_transitions"), qulonglong(syncTransitions)},
            {QStringLiteral("observed_fps"), observedFps},
            {QStringLiteral("status"), report.value(QStringLiteral("status"))},
            {QStringLiteral("physical_acceptance"), false}
        });
    }
    result.insert(QStringLiteral("status"), usable
        ? QStringLiteral("Capture saved: %1 SV / %2 PTP frames, %3 missing, %4 sync transitions. "
                         "Status: %5. Share report.json + events.csv first; keep PCAP for deep analysis.")
              .arg(matched).arg(ptpFrames).arg(missing).arg(syncTransitions)
              .arg(anomalies ? QStringLiteral("ANOMALIES") : QStringLiteral("NOT PHYSICAL PASS"))
        : formatError(QStringLiteral("Capture validation failed"),
                      parseFailure.isEmpty() ? QStringLiteral("No valid SV packets.") : parseFailure));
    return result;
}
