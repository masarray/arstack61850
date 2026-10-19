// SPDX-License-Identifier: GPL-3.0-or-later
#include "SvCaptureController.hpp"

#include <QtConcurrentRun>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTextStream>
#include <QUrl>
#include <QVariantList>
#include <QTimer>

#include <array>
#include <cstdint>
#include <limits>
#include <utility>

namespace {
constexpr auto kSvBpf =
    "ether[12:2]=0x88ba or "
    "((ether[12:2]=0x8100 or ether[12:2]=0x88a8) and ether[16:2]=0x88ba) or "
    "((ether[12:2]=0x8100 or ether[12:2]=0x88a8) and "
    "(ether[16:2]=0x8100 or ether[16:2]=0x88a8) and ether[20:2]=0x88ba)";
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
} // namespace

SvCaptureController::SvCaptureController(QObject* parent) : QObject(parent) {
    dumpcap_ = QStandardPaths::findExecutable(QStringLiteral("dumpcap"));
#ifdef Q_OS_WIN
    if (dumpcap_.isEmpty()) {
        for (const QString& path : {
                 QStringLiteral("C:/Program Files/Wireshark/dumpcap.exe"),
                 QStringLiteral("C:/Program Files (x86)/Wireshark/dumpcap.exe")}) {
            if (QFileInfo::exists(path)) { dumpcap_ = path; break; }
        }
    }
#endif
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
    if (dumpcap_.isEmpty()) {
        setStatus(QStringLiteral("Capture requires Wireshark dumpcap + Npcap. Install Wireshark including Npcap, then Refresh."));
        return;
    }
    setStatus(QStringLiteral("Finding capture interfaces..."));
    discovery_.setProgram(dumpcap_);
    discovery_.setArguments({QStringLiteral("-D")});
    discovery_.start();
}
bool SvCaptureController::startCapture(int index, int seconds) {
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
    const qint64 required = qint64(seconds) * 4000 * 240 + 128LL * 1024 * 1024;
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
    activeJson_ = QDir(outputFolder_).filePath(filename + QStringLiteral("_report.json"));
    activeAdapter_ = interfaceIds_.at(index);
    activeSecondsRequested_ = seconds;
    abortRequested_ = false;
    lastReport_.clear();
    const QStringList args{
        QStringLiteral("-i"), activeAdapter_,
        QStringLiteral("-f"), QString::fromLatin1(kSvBpf),
        QStringLiteral("-s"), QStringLiteral("0"),
        QStringLiteral("-P"), // classic PCAP, not default PCAPNG
        QStringLiteral("-a"), QStringLiteral("duration:%1").arg(seconds),
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
        setStatus(QStringLiteral("Capture canceled. No success report generated."));
        return;
    }
    if (exitCode != 0 || exitStatus != QProcess::NormalExit ||
        QFileInfo(activePcap_).size() < 64) {
        setStatus(QStringLiteral("Capture failed or empty. Check adapter/TAP, Npcap and capture permissions. ")
            + QString::fromUtf8(capture_.readAllStandardError()).right(360));
        return;
    }
    decodeCapturedFile();
}
void SvCaptureController::decodeCapturedFile() {
    setStatus(QStringLiteral("Writing filtered SV CSV and JSON report (off the UI thread)..."));
    decoder_.setFuture(QtConcurrent::run(&SvCaptureController::decodePcap,
        activePcap_, activeCsv_, activeSeconds_, activeJson_, activeSecondsRequested_, activeAdapter_));
    emit stateChanged();
}
void SvCaptureController::openOutputFolder() {
    if (!QDir().mkpath(outputFolder_)) return;
    QDesktopServices::openUrl(QUrl::fromLocalFile(outputFolder_));
}

QVariantMap SvCaptureController::decodePcap(
    const QString& pcapFile, const QString& csvFile, const QString& secondsFile,
    const QString& jsonFile, int requestedSeconds, const QString& adapter) {
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
    if (!csv.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        !secondsCsv.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        result.insert(QStringLiteral("status"), QStringLiteral("Cannot create CSV files in Documents."));
        return result;
    }
    QTextStream stream(&csv);
    QTextStream secondsStream(&secondsCsv);
    stream << "timestamp_us,source_mac,destination_mac,appid,sv_id,smp_cnt,smp_synch";
    for (const auto* name : kChannels)
        stream << ',' << name << ',' << name << "_quality";
    stream << '\n';
    secondsStream << "utc_second,sv_frames,missing_smpcnt,duplicates,malformed\n";
    // Preserve every raw PCAP packet. 3600s CSV exports only every 20th
    // validated frame; per-second counters always use every frame.
    const quint64 csvStride = requestedSeconds >= 3600 ? 20 : 1;
    quint64 matched = 0, malformed = 0, missing = 0, duplicates = 0;
    quint64 firstTimestamp = 0, lastTimestamp = 0, csvRows = 0;
    quint32 lastCount = 0;
    quint64 lastBucket = std::numeric_limits<quint64>::max();
    quint64 bucketFrames = 0, bucketMissing = 0, bucketDuplicates = 0, bucketMalformed = 0;
    bool hasPrevious = false;
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
        if (hasPrevious) {
            const quint32 diff = (record.count + kSampleWrap - lastCount) % kSampleWrap;
            if (diff == 0) {
                ++duplicates;
                ++bucketDuplicates;
            } else if (diff != 1) {
                missing += diff - 1;
                bucketMissing += diff - 1;
            }
        }
        hasPrevious = true;
        lastCount = record.count;
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
    if (csv.error() != QFile::NoError || secondsCsv.error() != QFile::NoError)
        parseFailure = QStringLiteral("CSV output failed (disk full?).");
    csv.close();
    secondsCsv.close();
    const double span = matched > 1 ? (lastTimestamp - firstTimestamp) / 1000000.0 : 0.;
    const double observedFps = span > 0 ? (matched - 1) / span : 0.;
    QVariantMap report{
        {QStringLiteral("schema"), QStringLiteral("arstack.sv.capture.v1")},
        {QStringLiteral("source"), QStringLiteral("independent ethernet PCAP via dumpcap/Npcap")},
        {QStringLiteral("capture_filter"), QString::fromLatin1(kSvBpf)},
        {QStringLiteral("interface"), adapter},
        {QStringLiteral("pcap"), pcapFile},
        {QStringLiteral("samples_csv"), csvFile},
        {QStringLiteral("seconds_csv"), secondsFile},
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
    report.insert(QStringLiteral("status"), usable ? QStringLiteral("CAPTURED_NOT_ACCEPTED")
                                                 : QStringLiteral("INVALID_CAPTURE"));
    if (!parseFailure.isEmpty()) report.insert(QStringLiteral("error"), parseFailure);
    QFile json(jsonFile);
    if (!json.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        json.write(QJsonDocument::fromVariant(report).toJson(QJsonDocument::Indented)) < 0) {
        result.insert(QStringLiteral("status"), QStringLiteral("Capture parsed but JSON report could not be written."));
        return result;
    }
    json.close();
    result.insert(QStringLiteral("status"), usable
        ? QStringLiteral("SV evidence ready: %1 packets, %2 CSV rows, %3 missing; open folder to share PCAP + CSV + JSON.")
              .arg(matched).arg(csvRows).arg(missing)
        : formatError(QStringLiteral("Capture validation failed"),
                      parseFailure.isEmpty() ? QStringLiteral("No valid SV packets.") : parseFailure));
    return result;
}
