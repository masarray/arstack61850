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


        // Browser GOOSE reuses the Sniffer decoder and must not guess
        // identities from APPID alone or swap publishers across IEDs.
        const auto goRef = field(publisherEvidence, "goCbRef");
        const auto dataSet = field(publisherEvidence, "dataSetReference");
        const auto dst = field(publisherEvidence, "destinationMac");
        const auto bound = monitor.observedForStream(
            goRef, dataSet, QStringLiteral("1001"), dst);
        REQUIRE(bound.value(QStringLiteral("matched")).toBool(),
                "configured GOOSE stream did not bind to decoded PCAP evidence");
        REQUIRE(bound.value(QStringLiteral("values")).toStringList().size() ==
                    publisherEvidence.value(QStringLiteral("valueCount")).toInt(),
                "bound allData order or member count not preserved");
        REQUIRE(bound.value(QStringLiteral("stNum")).toULongLong() == 2U,
                "bound stream did not expose last observed state");
        REQUIRE(!monitor.observedForStream(QString{}, dataSet, "1001", dst)
                     .value(QStringLiteral("matched")).toBool(),
                "missing goCBRef must not match");
        REQUIRE(!monitor.observedForStream(goRef.toLower(), dataSet, "1001", dst)
                     .value(QStringLiteral("matched")).toBool(),
                "GOOSE goCBRef lost case-sensitive identity");
        REQUIRE(!monitor.observedForStream(goRef, dataSet + "WRONG", "1001", dst)
                     .value(QStringLiteral("matched")).toBool(),
                "wrong DataSet incorrectly adopted an observed value");
        REQUIRE(!monitor.observedForStream(goRef, dataSet, "1002", dst)
                     .value(QStringLiteral("matched")).toBool(),
                "APPID mismatch leaked another stream's values");
        REQUIRE(!monitor.observedForStream(goRef, dataSet, "not-hex", dst)
                     .value(QStringLiteral("matched")).toBool(),
                "malformed configured APPID was accepted");
        REQUIRE(!monitor.observedForStream(goRef, dataSet, "1001", "XX-00")
                     .value(QStringLiteral("matched")).toBool(),
                "invalid configured multicast MAC was accepted");
        REQUIRE(!monitor.observedForStream(goRef, dataSet, "1001", "01-0C-CD-01-00-FF")
                     .value(QStringLiteral("matched")).toBool(),
                "destination MAC mismatch leaked a stream");


        // Full SCL constraints: goID, VLAN ID/priority must not silently
        // bind a different multicast virtual LAN or publishing control block.
        QVariantMap configured;
        configured.insert(QStringLiteral("reference"), goRef);
        configured.insert(QStringLiteral("dataSet"), dataSet);
        configured.insert(QStringLiteral("appId"), QStringLiteral("1001"));
        configured.insert(QStringLiteral("destinationMac"), dst);
        configured.insert(QStringLiteral("goId"), field(publisherEvidence, "goId"));
        if (publisherEvidence.value(QStringLiteral("vlanId")).isValid()) {
            configured.insert(QStringLiteral("vlanId"),
                              publisherEvidence.value(QStringLiteral("vlanId")));
        }
        if (publisherEvidence.value(QStringLiteral("vlanPriority")).isValid()) {
            configured.insert(QStringLiteral("vlanPriority"),
                              publisherEvidence.value(QStringLiteral("vlanPriority")));
        }
        REQUIRE(monitor.observedForConfiguredStream(configured)
                    .value(QStringLiteral("matched")).toBool(),
                "canonical SCL identity did not bind to observed GOOSE");

        const auto wireToCanonicalGo = [](QString ref) {
            const auto first = ref.indexOf(QLatin1Char('$'));
            const auto second = ref.indexOf(QLatin1Char('$'), first + 1);
            REQUIRE(first >= 0 && second >= 0, "fixture requires typed wire GO reference");
            ref[first] = QLatin1Char('.');
            ref[second] = QLatin1Char('.');
            return ref;
        };
        const auto wireToCanonicalSet = [](QString ref) {
            const auto slash = ref.indexOf(QLatin1Char('/'));
            const auto dollar = ref.indexOf(QLatin1Char('$'), slash + 1);
            REQUIRE(dollar >= 0, "fixture requires canonical DataSet translation");
            ref[dollar] = QLatin1Char('.');
            return ref;
        };
        auto discovered = configured;
        discovered.insert(QStringLiteral("reference"), wireToCanonicalGo(goRef));
        discovered.insert(QStringLiteral("dataSet"), wireToCanonicalSet(dataSet));
        REQUIRE(monitor.observedForConfiguredStream(discovered)
                     .value(QStringLiteral("matched")).toBool(),
                "typed live-model GO/DataSet identifiers failed wire normalization");
        discovered.insert(QStringLiteral("reference"),
                          QStringLiteral("OtherIED/LLN0.GO.gcb01"));
        REQUIRE(!monitor.observedForConfiguredStream(discovered)
                     .value(QStringLiteral("matched")).toBool(),
                "normalization crossed IED model identity");
        auto conflicting = configured;
        conflicting.insert(QStringLiteral("goId"), QStringLiteral("wrong-publisher"));
        REQUIRE(!monitor.observedForConfiguredStream(conflicting)
                     .value(QStringLiteral("matched")).toBool(),
                "wrong goID leaked stream from a different publisher");
        conflicting = configured;
        conflicting.insert(QStringLiteral("vlanId"), 4094);
        if (configured.value(QStringLiteral("vlanId")).toInt() == 4094)
            conflicting.insert(QStringLiteral("vlanId"), 4093);
        REQUIRE(!monitor.observedForConfiguredStream(conflicting)
                     .value(QStringLiteral("matched")).toBool(),
                "wrong VLAN identity adopted an observed GOOSE frame");
        conflicting = configured;
        conflicting.insert(QStringLiteral("vlanPriority"), 99);
        REQUIRE(!monitor.observedForConfiguredStream(conflicting)
                     .value(QStringLiteral("matched")).toBool(),
                "invalid configured VLAN priority was accepted");
        GooseMonitorController ambiguousMonitor;
        REQUIRE(ambiguousMonitor.ingestFrameBytes(
                    packets.back().frame, base + std::chrono::seconds{1}),
                "secondary identity test failed first import");
        goose::GooseFrame alternate;
        REQUIRE(goose::GooseFrameCodec::try_decode(packets.back().frame, alternate),
                "could not decode alternate publisher identity");
        alternate.source = ethernet::MacAddress::parse("02-11-22-33-44-55");
        alternate.destination = ethernet::MacAddress::parse("01-0C-CD-01-00-FE");
        alternate.pdu.go_id = "alternate-publisher";
        if (alternate.vlan) {
            alternate.vlan->vlan_id =
                static_cast<std::uint16_t>(
                    (alternate.vlan->vlan_id + 1U) % 4095U);
        }
        REQUIRE(ambiguousMonitor.ingestFrameBytes(
                    goose::GooseFrameCodec::encode(alternate),
                    base + std::chrono::seconds{2}),
                "secondary identity test rejected valid other publisher");
        REQUIRE(!ambiguousMonitor.observedForStream(goRef, dataSet, "1001", "")
                     .value(QStringLiteral("matched")).toBool(),
                "ambiguous publisher identity must fail closed");
        REQUIRE(ambiguousMonitor.observedForStream(goRef, dataSet, "1001", dst)
                     .value(QStringLiteral("matched")).toBool(),
                "exact multicast MAC failed to disambiguate publishers");
        REQUIRE(ambiguousMonitor.observedForStream(
                    goRef, dataSet, "1001", "01-0C-CD-01-00-FE")
                     .value(QStringLiteral("matched")).toBool(),
                "alternate exact publisher could not be bound");

        auto byGoId = configured;
        byGoId.remove(QStringLiteral("destinationMac"));
        byGoId.remove(QStringLiteral("vlanId"));
        byGoId.remove(QStringLiteral("vlanPriority"));
        REQUIRE(ambiguousMonitor.observedForConfiguredStream(byGoId)
                    .value(QStringLiteral("matched")).toBool(),
                "goID must disambiguate publishers before ambiguity rejection");
        byGoId.insert(QStringLiteral("goId"), QStringLiteral("alternate-publisher"));
        REQUIRE(ambiguousMonitor.observedForConfiguredStream(byGoId)
                    .value(QStringLiteral("matched")).toBool(),
                "alternate goID did not disambiguate two otherwise identical publishers");
        byGoId.remove(QStringLiteral("goId"));
        REQUIRE(!ambiguousMonitor.observedForConfiguredStream(byGoId)
                     .value(QStringLiteral("matched")).toBool(),
                "underspecified multiple publishers must remain fail-closed");

        if (publisherEvidence.value(QStringLiteral("vlanId")).isValid()) {
            auto byVlan = byGoId;
            byVlan.insert(QStringLiteral("vlanId"),
                          publisherEvidence.value(QStringLiteral("vlanId")));
            REQUIRE(ambiguousMonitor.observedForConfiguredStream(byVlan)
                        .value(QStringLiteral("matched")).toBool(),
                    "VLAN ID must disambiguate publisher before ambiguity rejection");
        }
        // Same physical source / APPID / goCBRef can still expose
        // independent publisher streams on distinct goID/VLAN interfaces.
        // A coarse (source, APPID, goCBRef) key incorrectly overwrote them.
        goose::GooseFrame baseFrame;
        REQUIRE(goose::GooseFrameCodec::try_decode(packets.back().frame, baseFrame),
                "could not decode Stage M evidence for sequence QA");
        GooseMonitorController sameSourceMonitor;
        REQUIRE(sameSourceMonitor.ingestFrameBytes(
                    goose::GooseFrameCodec::encode(baseFrame), base),
                "same-source fixture did not ingest primary frame");
        auto otherGoId = baseFrame;
        otherGoId.pdu.go_id = "alternate-publisher";
        REQUIRE(sameSourceMonitor.ingestFrameBytes(
                    goose::GooseFrameCodec::encode(otherGoId),
                    base + std::chrono::milliseconds{1}),
                "same-source alternate goID failed ingest");
        REQUIRE(sameSourceMonitor.rowCount() == 2,
                "same-source alternate goID overwrote a distinct publisher stream");
        REQUIRE(!sameSourceMonitor.observedForStream(goRef, dataSet, "1001", dst)
                     .value(QStringLiteral("matched")).toBool(),
                "same-source distinct goID should be ambiguous without goID selector");
        auto keyedGoId = configured;
        keyedGoId.remove(QStringLiteral("vlanId"));
        keyedGoId.remove(QStringLiteral("vlanPriority"));
        REQUIRE(sameSourceMonitor.observedForConfiguredStream(keyedGoId)
                    .value(QStringLiteral("matched")).toBool(),
                "primary same-source goID not uniquely bound");
        keyedGoId.insert(QStringLiteral("goId"), QStringLiteral("alternate-publisher"));
        REQUIRE(sameSourceMonitor.observedForConfiguredStream(keyedGoId)
                    .value(QStringLiteral("matched")).toBool(),
                "alternate same-source goID not uniquely bound");

        if (baseFrame.vlan) {
            GooseMonitorController sameSourceVlanMonitor;
            REQUIRE(sameSourceVlanMonitor.ingestFrameBytes(
                        goose::GooseFrameCodec::encode(baseFrame), base),
                    "tagged publisher first frame not retained");
            auto differentVlan = baseFrame;
            differentVlan.vlan->vlan_id = static_cast<std::uint16_t>(
                (differentVlan.vlan->vlan_id + 1U) % 4095U);
            REQUIRE(sameSourceVlanMonitor.ingestFrameBytes(
                        goose::GooseFrameCodec::encode(differentVlan),
                        base + std::chrono::milliseconds{1}),
                    "alternate VLAN GOOSE frame rejected");
            REQUIRE(sameSourceVlanMonitor.rowCount() == 2,
                    "same-source tagged publishers collapsed across VLAN ID");
            auto vlanSelector = configured;
            REQUIRE(sameSourceVlanMonitor.observedForConfiguredStream(vlanSelector)
                        .value(QStringLiteral("matched")).toBool(),
                    "configured VLAN ID did not select the original publisher");
            vlanSelector.insert(QStringLiteral("vlanId"),
                                static_cast<int>(differentVlan.vlan->vlan_id));
            REQUIRE(sameSourceVlanMonitor.observedForConfiguredStream(vlanSelector)
                        .value(QStringLiteral("matched")).toBool(),
                    "configured VLAN ID did not select alternate publisher");
            vlanSelector.remove(QStringLiteral("vlanId"));
            REQUIRE(!sameSourceVlanMonitor.observedForConfiguredStream(vlanSelector)
                         .value(QStringLiteral("matched")).toBool(),
                    "underspecified same-source VLAN identity must fail closed");
        }
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
               " state_jump=pass state_regression=pass timeout=pass pcap_export=pass"
               " browser_subscribe_binding=pass publisher_isolation=pass\n";
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
