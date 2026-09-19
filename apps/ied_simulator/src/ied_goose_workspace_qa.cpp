// SPDX-License-Identifier: GPL-3.0-or-later

#include "GooseMonitorController.hpp"

#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/goose/frame_codec.hpp"
#include "ariec61850/mms/data_value.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QUrl>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace ar::iec61850;
#define REQUIRE(condition, message) do { if (!(condition)) throw std::runtime_error(message); } while (false)

std::vector<std::uint8_t> encode_variant(
    goose::GooseFrame frame,
    const std::uint32_t state,
    const std::uint32_t sequence,
    const std::uint32_t ttl = 20U) {
    frame.pdu.state_number = state;
    frame.pdu.sequence_number = sequence;
    frame.pdu.time_allowed_to_live_milliseconds = ttl;
    return goose::GooseFrameCodec::encode(frame);
}

QString field(const QVariantMap& map, const char* key) {
    return map.value(QString::fromLatin1(key)).toString();
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        if (argc < 2) {
            std::cerr << "usage: ied_goose_workspace_qa <publisher-pcap>\n";
            return 2;
        }

        const auto packets = capture::PcapReader::read_all(std::filesystem::path{argv[1]});
        REQUIRE(packets.size() == 3U, "Stage M publisher evidence must contain exactly 3 packets");

        GooseMonitorController monitor;
        const auto base = GooseMonitorController::clock::time_point{} + std::chrono::seconds{10};
        const auto firstTimestamp = packets.front().timestamp;
        for (const auto& packet : packets) {
            const auto relative = std::chrono::duration_cast<std::chrono::milliseconds>(
                packet.timestamp - firstTimestamp);
            REQUIRE(monitor.ingestFrameBytes(packet.frame, base + relative),
                    "monitor rejected Stage M GOOSE PCAP evidence");
        }
        REQUIRE(monitor.rowCount() == 1, "publisher PCAP should project one monitored stream");
        REQUIRE(monitor.packetCount() == 3U, "publisher PCAP packet count mismatch");
        monitor.select(0);
        const auto publisherEvidence = monitor.selectedStream();
        REQUIRE(field(publisherEvidence, "appIdText") == QStringLiteral("0x1001"),
                "APPID was not projected by monitor");
        REQUIRE(publisherEvidence.value(QStringLiteral("stNum")).toULongLong() == 2U,
                "state-change stNum was not observed");
        REQUIRE(publisherEvidence.value(QStringLiteral("sqNum")).toULongLong() == 0U,
                "state-change sqNum reset was not observed");
        REQUIRE(!field(publisherEvidence, "goCbRef").isEmpty(), "gocbRef missing");
        REQUIRE(!field(publisherEvidence, "dataSetReference").isEmpty(), "DataSet missing");
        REQUIRE(publisherEvidence.value(QStringLiteral("valueCount")).toInt() > 0,
                "decoded allData values were not exposed");

        goose::GooseFrame baseFrame;
        REQUIRE(goose::GooseFrameCodec::try_decode(packets.back().frame, baseFrame),
                "could not decode Stage M evidence for sequence QA");
        const auto lastRelative = std::chrono::duration_cast<std::chrono::milliseconds>(
            packets.back().timestamp - firstTimestamp);
        auto arrival = base + lastRelative + std::chrono::milliseconds{1};

        const auto duplicate = encode_variant(baseFrame, 2U, 0U);
        REQUIRE(monitor.ingestFrameBytes(duplicate, arrival), "duplicate frame rejected unexpectedly");
        REQUIRE(monitor.duplicateCount() == 1U, "duplicate was not classified");

        arrival += std::chrono::milliseconds{1};
        const auto gap = encode_variant(baseFrame, 2U, 3U);
        REQUIRE(monitor.ingestFrameBytes(gap, arrival), "sequence-gap frame rejected unexpectedly");
        REQUIRE(monitor.sequenceIssueCount() >= 1U, "sequence gap was not classified");

        arrival += std::chrono::milliseconds{1};
        const auto regression = encode_variant(baseFrame, 2U, 2U);
        REQUIRE(monitor.ingestFrameBytes(regression, arrival), "sequence regression rejected unexpectedly");

        arrival += std::chrono::milliseconds{1};
        const auto stateJump = encode_variant(baseFrame, 4U, 0U);
        REQUIRE(monitor.ingestFrameBytes(stateJump, arrival), "state jump rejected unexpectedly");

        arrival += std::chrono::milliseconds{1};
        const auto stateRegression = encode_variant(baseFrame, 3U, 0U, 5U);
        REQUIRE(monitor.ingestFrameBytes(stateRegression, arrival), "state regression rejected unexpectedly");
        REQUIRE(monitor.sequenceIssueCount() >= 4U,
                "gap/regression/state-jump/state-regression anomaly count incomplete");

        monitor.checkTimeoutsAt(arrival + std::chrono::milliseconds{20});
        REQUIRE(monitor.timeoutCount() >= 1U, "GOOSE TTL timeout was not surfaced");
        REQUIRE(monitor.selectedStream().value(QStringLiteral("timedOut")).toBool(),
                "selected stream timeout indicator missing");

        QTemporaryDir temporary;
        REQUIRE(temporary.isValid(), "could not create temporary directory for PCAP export");
        const auto exportedPath = temporary.filePath(QStringLiteral("monitor-evidence.pcap"));
        REQUIRE(monitor.exportPcap(QUrl::fromLocalFile(exportedPath)), "PCAP export failed");
        REQUIRE(QFileInfo::exists(exportedPath), "PCAP export file was not created");
        const auto exported = capture::PcapReader::read_all(
            std::filesystem::path{exportedPath.toStdString()});
        REQUIRE(exported.size() == monitor.retainedPacketCount(),
                "PCAP export did not preserve the retained monitor packet set");

        const std::vector<std::uint8_t> malformed{0x00U, 0x01U, 0x02U};
        REQUIRE(!monitor.ingestFrameBytes(malformed, arrival + std::chrono::milliseconds{21}),
                "malformed Ethernet frame was accepted");
        REQUIRE(monitor.decodeErrorCount() == 1U, "malformed-frame counter mismatch");

        auto oversize = baseFrame;
        oversize.pdu.state_number = 5U;
        oversize.pdu.sequence_number = 0U;
        oversize.pdu.values.clear();
        for (int index = 0; index < monitor.memberCapacity() + 1; ++index) {
            oversize.pdu.values.push_back(mms::MmsDataValue::boolean((index & 1) != 0));
        }
        const auto oversizeBytes = goose::GooseFrameCodec::encode(oversize);
        REQUIRE(!monitor.ingestFrameBytes(oversizeBytes, arrival + std::chrono::milliseconds{22}),
                "oversize allData frame was accepted");
        REQUIRE(monitor.rejectedFrameCount() >= 1U, "oversize rejection was not counted");

        auto additional = baseFrame;
        additional.pdu.values = {mms::MmsDataValue::boolean(true)};
        additional.pdu.state_number = 1U;
        additional.pdu.sequence_number = 0U;
        additional.pdu.time_allowed_to_live_milliseconds = 1000U;
        for (int index = 0; index < monitor.streamCapacity(); ++index) {
            additional.app_id = static_cast<std::uint16_t>(0x2000U + static_cast<unsigned>(index));
            additional.pdu.go_cb_ref = "QA/LLN0$GO$gcb" + std::to_string(index);
            const auto bytes = goose::GooseFrameCodec::encode(additional);
            const bool accepted = monitor.ingestFrameBytes(
                bytes, arrival + std::chrono::milliseconds{30 + index});
            if (index < monitor.streamCapacity() - 1) {
                REQUIRE(accepted, "bounded stream table rejected before reaching capacity");
            } else {
                REQUIRE(!accepted, "bounded stream table accepted a 257th identity");
            }
        }
        REQUIRE(monitor.rowCount() == monitor.streamCapacity(), "stream capacity projection mismatch");
        REQUIRE(monitor.droppedStreamCount() == 1U, "stream-capacity drop counter mismatch");
        REQUIRE(monitor.recentEvents().size() <= monitor.eventCapacity(), "event retention is unbounded");
        REQUIRE(monitor.retainedPacketCount() <= monitor.retainedPacketCapacity(),
                "packet retention is unbounded");

        monitor.stopCapture();
        monitor.setInterfaceName(QStringLiteral("arstack-no-such-interface-61850"));
        REQUIRE(!monitor.startCapture(), "nonexistent monitor interface did not fail closed");
        REQUIRE(!monitor.capturing(), "capture became active after invalid interface request");

        std::cout
            << "GOOSE_WORKSPACE_PASS pcap_packets=3 streams=" << monitor.rowCount()
            << " appid=0x1001 values=decoded duplicate=pass gap=pass regression=pass"
               " state_jump=pass state_regression=pass timeout=pass pcap_export=pass\n";
        std::cout
            << "GOOSE_MONITOR_NEGATIVE_PASS malformed=rejected oversize=rejected capacity=rejected"
               " missing_interface=rejected explicit_binding=required bounded_streams="
            << monitor.streamCapacity() << " bounded_members=" << monitor.memberCapacity()
            << " bounded_events=" << monitor.eventCapacity()
            << " retained_packets=" << monitor.retainedPacketCapacity() << "\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "GOOSE_WORKSPACE_QA_FAIL " << error.what() << '\n';
        return 1;
    }
}
