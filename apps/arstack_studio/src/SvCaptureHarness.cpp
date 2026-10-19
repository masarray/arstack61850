// SPDX-License-Identifier: GPL-3.0-or-later
// Deterministic fixture uses the exact production capture decoder; no Npcap needed.
#include "SvCaptureController.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QDebug>

#include <array>
#include <cstdint>

namespace {
void le32(QByteArray& data, quint32 n) {
    for (int i = 0; i < 4; ++i) data.append(char((n >> (8 * i)) & 0xff));
}
void be32(QByteArray& data, quint32 n) {
    for (int i = 3; i >= 0; --i) data.append(char((n >> (8 * i)) & 0xff));
}
QByteArray tlv(quint8 tag, QByteArray value) {
    QByteArray result;
    result.append(char(tag));
    if (value.size() < 128) {
        result.append(char(value.size()));
    } else {
        result.append(char(0x81));
        result.append(char(value.size()));
    }
    result.append(value);
    return result;
}
QByteArray packet(quint16 sampleCount) {
    QByteArray payload;
    for (int i = 0; i < 8; ++i) {
        be32(payload, quint32(i == 0 ? -1200 : 2000 + i));
        be32(payload, i == 0 ? 0x12345678u : 0u);
    }
    QByteArray count;
    count.append(char(sampleCount >> 8));
    count.append(char(sampleCount & 255));
    const QByteArray asdu = tlv(0x30,
        tlv(0x80, "ARSTACK_SV01") +
        tlv(0x82, count) +
        tlv(0x83, QByteArray(1, char(1))) +
        tlv(0x85, QByteArray(1, char(0))) +
        tlv(0x87, payload));
    const QByteArray pdu = tlv(0x60,
        tlv(0x80, QByteArray(1, char(1))) + tlv(0xa2, asdu));
    QByteArray wire = QByteArray::fromHex("011ccd04000102000000000188ba");
    wire.append(char(0x40));
    wire.append(char(0x00));
    const quint16 length = quint16(pdu.size() + 8);
    wire.append(char(length >> 8));
    wire.append(char(length & 255));
    wire.append(QByteArray(4, char(0)));
    wire.append(pdu);
    return wire;
}
} // namespace

class SvCaptureHarness {
public:
    static bool run() {
        QTemporaryDir dir;
        if (!dir.isValid()) return false;
        const QString pcap = dir.filePath("fixture.pcap");
        const QString csv = dir.filePath("samples.csv");
        const QString seconds = dir.filePath("seconds.csv");
        const QString json = dir.filePath("report.json");
        QFile file(pcap);
        if (!file.open(QIODevice::WriteOnly)) return false;
        QByteArray global = QByteArray::fromHex("d4c3b2a1");
        global.append(QByteArray::fromHex("02000400"));
        le32(global, 0);
        le32(global, 0);
        le32(global, 65535);
        le32(global, 1);
        file.write(global);
        const std::array<quint16, 3> counts{3999, 0, 2};
        for (int i = 0; i < 3; ++i) {
            const QByteArray wire = packet(counts[i]);
            QByteArray header;
            le32(header, 1000);
            le32(header, quint32(i) * 250);
            le32(header, quint32(wire.size()));
            le32(header, quint32(wire.size()));
            file.write(header);
            file.write(wire);
        }
        file.close();
        const auto result = SvCaptureController::decodePcap(
            pcap, csv, seconds, json, 120, QStringLiteral("TEST_ETHERNET"),
            {{QStringLiteral("firmware_build_id"), QStringLiteral("synthetic-fixture")}});
        QFile reportFile(json), csvFile(csv), secondsFile(seconds);
        if (!reportFile.open(QIODevice::ReadOnly) ||
            !csvFile.open(QIODevice::ReadOnly) ||
            !secondsFile.open(QIODevice::ReadOnly)) return false;
        const QJsonObject parsed = QJsonDocument::fromJson(reportFile.readAll()).object();
        const QByteArray csvRows = csvFile.readAll();
        const QByteArray secondsRows = secondsFile.readAll();
        const bool ok =
            parsed.value("sv_frames").toDouble() == 3 &&
            parsed.value("missing_sample_counts").toDouble() == 1 &&
            parsed.value("duplicate_sample_counts").toDouble() == 0 &&
            parsed.value("csv_rows").toDouble() == 3 &&
            parsed.value("physical_acceptance").toBool() == false &&
            parsed.value("studio_telemetry_snapshot_UNVERIFIED").toObject()
                  .value("firmware_build_id").toString() == "synthetic-fixture" &&
            csvRows.contains("IA,IA_quality") &&
            csvRows.contains("IN,IN_quality") &&
            csvRows.contains("UN,UN_quality") &&
            csvRows.contains("-1200,0x12345678") &&
            csvRows.contains("ARSTACK_SV01") &&
            secondsRows.contains("utc_second,sv_frames,missing_smpcnt") &&
            result.value("status").toString().contains("3 packets");
        if (!ok) qCritical() << "Failed production PCAP fixture:" << result << parsed;
        return ok;
    }
};

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (!SvCaptureHarness::run()) return 1;
    qInfo() << "Native SMV PCAP/CSV/JSON capture regression: PASS (synthetic only)";
    return 0;
}
