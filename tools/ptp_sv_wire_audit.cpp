// SPDX-License-Identifier: GPL-3.0-or-later
// Read-only, standards-oriented PTP + Sampled Values PCAP evidence.
// Reuses existing ARIEC61850 codecs and stream supervisor, not a second parser.
#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/stream_supervisor.hpp"
#include "ariec61850/time_sync/ptp.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
namespace capture = ar::iec61850::capture;
namespace ethernet = ar::iec61850::ethernet;
namespace sv = ar::iec61850::sampled_values;
namespace ptp = ar::iec61850::time_sync;

struct AuditOptions final {
    std::string sv_id;
    std::uint16_t app_id{};
    std::uint16_t counter_modulus{};
    std::uint8_t ptp_domain{};
};

struct Evidence final {
    std::uint64_t packets{};
    std::uint64_t malformed{};
    std::uint64_t unrelated{};
    std::uint64_t selected_sv_frames{};
    std::uint64_t selected_asdus{};
    std::uint64_t ignored_sv_frames{};
    std::uint64_t sv_source_conflicts{};
    std::uint64_t ptp_domain_mismatches{};
    std::uint64_t ptp_source_conflicts{};
    std::uint64_t ptp_announce{};
    std::uint64_t ptp_sync{};
    std::uint64_t ptp_two_step_sync{};
    std::uint64_t ptp_follow_up{};
    std::uint64_t ptp_follow_up_matched{};
    std::uint64_t ptp_follow_up_orphan{};
    std::uint64_t ptp_follow_up_missing{};
    std::uint64_t ptp_pdelay_response{};
    std::array<std::uint64_t, 4> smp_synch_counts{};
    std::uint64_t counter_zero_observations{};
    std::int64_t counter_zero_capture_phase_min_us{1000000};
    std::int64_t counter_zero_capture_phase_max_us{-1000000};
};

class CaptureAudit final {
public:
    explicit CaptureAudit(const AuditOptions& options)
        : options_(options),
          supervisor_(sv::StreamExpectation{
              options.sv_id, std::nullopt, std::nullopt, options.counter_modulus}) {}

    void observe(const capture::PcapPacket& packet) {
        ++results_.packets;
        ethernet::EthernetFrame link;
        if (!ethernet::EthernetFrameCodec::try_decode(packet.frame, link)) {
            ++results_.malformed;
            return;
        }
        if (link.ether_type == ethernet::ptp_ethertype) {
            ptp::PtpFrame frame;
            if (!ptp::PtpCodec::try_parse_ethernet_frame(packet.frame, frame)) {
                ++results_.malformed;
                return;
            }
            if (frame.header.domain_number != options_.ptp_domain) {
                ++results_.ptp_domain_mismatches;
                return;
            }
            // Announce/Sync/FollowUp must belong to ONE selected clock
            // identity. A rogue GM never completes the current Sync pair.
            const auto message_type = frame.header.message_type;
            if (message_type == ptp::PtpMessageType::announce ||
                message_type == ptp::PtpMessageType::sync ||
                message_type == ptp::PtpMessageType::follow_up) {
                if (!ptp_source_.has_value()) {
                    ptp_source_ = frame.header.source_port_identity;
                } else if (*ptp_source_ != frame.header.source_port_identity) {
                    ++results_.ptp_source_conflicts;
                    return;
                }
            }
            switch (message_type) {
            case ptp::PtpMessageType::announce:
                ++results_.ptp_announce;
                break;
            case ptp::PtpMessageType::sync:
                ++results_.ptp_sync;
                if (frame.header.is_two_step()) {
                    ++results_.ptp_two_step_sync;
                    if (pending_sync_sequence_.has_value()) {
                        ++results_.ptp_follow_up_missing;
                    }
                    pending_sync_sequence_ = frame.header.sequence_id;
                }
                break;
            case ptp::PtpMessageType::follow_up:
                ++results_.ptp_follow_up;
                if (pending_sync_sequence_ == frame.header.sequence_id) {
                    ++results_.ptp_follow_up_matched;
                    pending_sync_sequence_.reset();
                } else {
                    ++results_.ptp_follow_up_orphan;
                }
                break;
            case ptp::PtpMessageType::pdelay_resp:
                ++results_.ptp_pdelay_response;
                break;
            default: break;
            }
            return;
        }
        if (link.ether_type != ethernet::sampled_values_ethertype) {
            ++results_.unrelated;
            return;
        }

        sv::SampledValuesFrame frame;
        if (!sv::SampledValuesFrameCodec::try_decode(packet.frame, frame)) {
            ++results_.malformed;
            return;
        }
        bool selected = false;
        for (const auto& asdu : frame.pdu.asdus) {
            if (frame.app_id != options_.app_id ||
                asdu.sv_id != options_.sv_id) {
                continue;
            }
            selected = true;
            if (!sv_source_.has_value()) {
                sv_source_ = frame.source;
            } else if (*sv_source_ != frame.source) {
                ++results_.sv_source_conflicts;
                continue; // Never mix counter histories from different publishers.
            }
            const auto observation = supervisor_.observe(asdu);
            (void)observation;
            ++results_.selected_asdus;
            const auto synch = asdu.sample_synchronization;
            ++results_.smp_synch_counts[synch <= 2U ? synch : 3U];

            // A host-captured packet's timestamp is NOT an IEEE1588 MAC
            // egress timestamp. Record the coarse offset for diagnostics only.
            if (asdu.sample_count == 0U) {
                const auto observed_us =
                    std::chrono::duration_cast<std::chrono::microseconds>(
                        packet.timestamp.time_since_epoch()).count();
                std::int64_t fractional_us = static_cast<std::int64_t>(observed_us % 1000000LL);
                if (fractional_us < 0) fractional_us += 1000000LL;
                if (fractional_us >= 500000LL) fractional_us -= 1000000LL;
                ++results_.counter_zero_observations;
                results_.counter_zero_capture_phase_min_us = std::min(
                    results_.counter_zero_capture_phase_min_us, fractional_us);
                results_.counter_zero_capture_phase_max_us = std::max(
                    results_.counter_zero_capture_phase_max_us, fractional_us);
            }
        }
        if (selected) ++results_.selected_sv_frames;
        else ++results_.ignored_sv_frames;
    }

    [[nodiscard]] const Evidence& evidence() const noexcept { return results_; }
    [[nodiscard]] const sv::StreamStatistics& stream() const noexcept {
        return supervisor_.statistics();
    }
    [[nodiscard]] bool ptp_visibility_complete() const noexcept {
        return results_.ptp_announce > 0U && results_.ptp_sync > 0U &&
            results_.ptp_source_conflicts == 0U &&
            results_.ptp_follow_up_orphan == 0U &&
            results_.ptp_follow_up_missing == 0U &&
            !pending_sync_sequence_.has_value() &&
            (results_.ptp_two_step_sync == 0U ||
             results_.ptp_follow_up_matched == results_.ptp_two_step_sync);
    }
    [[nodiscard]] bool wire_consistent() const noexcept {
        const auto& s = stream();
        return results_.selected_asdus > 0U && results_.malformed == 0U &&
            results_.sv_source_conflicts == 0U &&
            results_.smp_synch_counts[3U] == 0U &&
            s.gaps == 0U && s.duplicates == 0U && s.out_of_order == 0U &&
            s.identity_mismatches == 0U && s.configuration_changes == 0U;
    }
private:
    AuditOptions options_;
    sv::SampledValuesStreamSupervisor supervisor_;
    Evidence results_{};
    std::optional<ethernet::MacAddress> sv_source_;
    std::optional<ptp::PtpPortIdentity> ptp_source_;
    std::optional<std::uint16_t> pending_sync_sequence_;
};

[[nodiscard]] unsigned parse_unsigned(
    const std::string_view text, const unsigned maximum, const char* label) {
    if (text.empty()) throw std::invalid_argument(std::string{label} + ": missing value");
    std::size_t consumed = 0U;
    const auto value = std::stoul(std::string{text}, &consumed, 0);
    if (consumed != text.size() || value > maximum) {
        throw std::invalid_argument(std::string{label} + ": out of range");
    }
    return static_cast<unsigned>(value);
}

void print_usage() {
    std::cout <<
        "Usage: ariec61850_ptp_sv_wire_audit <capture.pcap>"
        " --sv-id <id> --app-id <0xNNNN> --counter-modulus <N>"
        " --ptp-domain <N> [--json]\n"
        "       ariec61850_ptp_sv_wire_audit --self-test\n"
        "Wire evidence only. Host PCAP timestamps never establish IEEE1588"
        " clock lock, UTC traceability, MAC TX phase, or IED alarm clearance.\n";
}

void display(const CaptureAudit& audit, const bool json) {
    const auto& a = audit.evidence();
    const auto& s = audit.stream();
    const bool wire = audit.wire_consistent();
    const bool ptp_visible = audit.ptp_visibility_complete();
    if (json) {
        std::cout << "{"
          << "\"evidence\":\"wire_only_unqualified\","
          << "\"packets\":" << a.packets << ","
          << "\"ptpAnnounce\":" << a.ptp_announce << ","
          << "\"ptpSync\":" << a.ptp_sync << ","
          << "\"ptpFollowUp\":" << a.ptp_follow_up << ","
          << "\"ptpFollowUpMatched\":" << a.ptp_follow_up_matched << ","
          << "\"ptpFollowUpOrphan\":" << a.ptp_follow_up_orphan << ","
          << "\"ptpFollowUpMissing\":" << a.ptp_follow_up_missing << ","
          << "\"ptpPdelayResp\":" << a.ptp_pdelay_response << ","
          << "\"ptpDomainMismatches\":" << a.ptp_domain_mismatches << ","
          << "\"ptpSourceConflicts\":" << a.ptp_source_conflicts << ","
          << "\"svFrames\":" << a.selected_sv_frames << ","
          << "\"svAsdus\":" << a.selected_asdus << ","
          << "\"ignoredSvFrames\":" << a.ignored_sv_frames << ","
          << "\"svSourceConflicts\":" << a.sv_source_conflicts << ","
          << "\"smpSynch0\":" << a.smp_synch_counts[0] << ","
          << "\"smpSynch1\":" << a.smp_synch_counts[1] << ","
          << "\"smpSynch2\":" << a.smp_synch_counts[2] << ","
          << "\"smpSynchUnknown\":" << a.smp_synch_counts[3] << ","
          << "\"normalWraps\":" << s.normal_wraps << ","
          << "\"gaps\":" << s.gaps << ","
          << "\"duplicates\":" << s.duplicates << ","
          << "\"outOfOrder\":" << s.out_of_order << ","
          << "\"missingSamples\":" << s.missing_samples << ","
          << "\"configurationChanges\":" << s.configuration_changes << ","
          << "\"malformed\":" << a.malformed << ","
          << "\"counterZeroCaptureObservations\":" << a.counter_zero_observations << ","
          << "\"counterZeroHostCapturePhaseMinUs\":"
          << (a.counter_zero_observations != 0U ?
              a.counter_zero_capture_phase_min_us : 0LL) << ","
          << "\"counterZeroHostCapturePhaseMaxUs\":"
          << (a.counter_zero_observations != 0U ?
              a.counter_zero_capture_phase_max_us : 0LL) << ","
          << "\"ptpPacketsVisible\":" << std::boolalpha << ptp_visible << ","
          << "\"svWireConsistent\":" << wire << ","
          << "\"ptpClockSynchronized\":null,"
          << "\"svPtpPhaseQualified\":null,"
          << "\"iedAlarmFree\":null}"
          << "\n";
    } else {
        std::cout << "PTP Announce/Sync/FollowUp: "
                  << a.ptp_announce << "/" << a.ptp_sync << "/"
                  << a.ptp_follow_up << " (matched="
                  << a.ptp_follow_up_matched << ", orphan="
                  << a.ptp_follow_up_orphan << ", missing="
                  << a.ptp_follow_up_missing << ") | PdelayResp=" << a.ptp_pdelay_response
                  << " | domain mismatch=" << a.ptp_domain_mismatches
                  << " | source conflicts=" << a.ptp_source_conflicts << "\n"
                  << "SV selected frames/asdus: " << a.selected_sv_frames
                  << "/" << a.selected_asdus
                  << " | smpSynch [0,1,2,other]: "
                  << a.smp_synch_counts[0] << ","
                  << a.smp_synch_counts[1] << ","
                  << a.smp_synch_counts[2] << ","
                  << a.smp_synch_counts[3] << "\n"
                  << "smpCnt wraps/gaps/duplicates/out-of-order/missing: "
                  << s.normal_wraps << "/" << s.gaps << "/" << s.duplicates
                  << "/" << s.out_of_order << "/" << s.missing_samples << "\n"
                  << "Observed wire consistency: " << (wire ? "PASS" : "FAIL")
                  << " | PTP source packets visible: "
                  << (ptp_visible ? "YES" : "INCOMPLETE") << "\n"
                  << "PTP lock / SV physical phase / IED alarms: NOT MEASURED\n";
    }
}

void self_test() {
    const AuditOptions opts{"LAB.SV01", 0x4000U, 4U, 0U};
    std::stringstream bytes(std::ios::in | std::ios::out | std::ios::binary);
    capture::PcapWriter writer(bytes);
    auto t = std::chrono::system_clock::time_point{
        std::chrono::seconds{1700000000LL}};
    const std::array<std::uint8_t, 6> ptp_mac{
        0x02U, 0x10U, 0x20U, 0x30U, 0x40U, 0x50U};
    ptp::PtpBuildOptions config;
    config.domain_number = 0U;
    config.source_port_identity.clock_identity =
        {0x02U, 0x10U, 0x20U, 0xFFU, 0xFEU, 0x30U, 0x40U, 0x50U};
    config.source_port_identity.port_number = 1U;
    config.grandmaster_identity = config.source_port_identity.clock_identity;
    config.timestamp = {1700000000ULL, 0U};
    const auto emit_ptp = [&](const std::vector<std::uint8_t>& pdu) {
        const auto ethernet_bytes = ptp::PtpCodec::build_ethernet_frame(
            ptp::ptp_general_multicast_mac, ptp_mac, pdu);
        if (ethernet_bytes.empty()) throw std::runtime_error("self-test PTP encode");
        writer.write_packet(t, ethernet_bytes);
        t += std::chrono::microseconds{100};
    };
    emit_ptp(ptp::PtpCodec::build_announce(config));
    emit_ptp(ptp::PtpCodec::build_sync(config));
    emit_ptp(ptp::PtpCodec::build_follow_up(config));

    sv::SampledValuesFrame frame;
    frame.destination = ethernet::MacAddress::parse("01:0C:CD:04:00:01");
    frame.source = ethernet::MacAddress::parse("02:AA:BB:CC:DD:EE");
    frame.app_id = opts.app_id;
    sv::SampledValueAsdu asdu;
    asdu.sv_id = opts.sv_id;
    asdu.data_set_reference = "IED1/LLN0$DS1";
    asdu.configuration_revision = 1U;
    asdu.sample_payload.assign(64U, 0U);
    frame.pdu.asdus.push_back(asdu);
    const auto emit_sv = [&](const std::uint16_t count) {
        frame.pdu.asdus[0].sample_count = count;
        writer.write_packet(t, sv::SampledValuesFrameCodec::encode(frame));
        t += std::chrono::microseconds{250};
    };
    emit_sv(2U);
    emit_sv(3U);
    emit_sv(0U);
    emit_sv(1U);
    // An unrelated vendor stream cannot contaminate the selected smpCnt.
    frame.pdu.asdus[0].sv_id = "OTHER.SV";
    emit_sv(3U);
    frame.pdu.asdus[0].sv_id = opts.sv_id;
    emit_sv(3U); // missing expected sample 2 -> detect one gap.

    bytes.seekg(0);
    const auto packets = capture::PcapReader::read_all(bytes);
    if (packets.size() < 2U) throw std::runtime_error("PCAP fixture too small");
    // Both APIs MUST return identical observations. Reader remains reusable
    // after a seek, with no retained packet list in the streaming caller.
    bytes.clear();
    bytes.seekg(0);
    CaptureAudit streaming(opts);
    capture::PcapReader::for_each_packet(
        bytes, [&streaming](capture::PcapPacket&& packet) {
            streaming.observe(packet);
        });
    if (streaming.stream().gaps != 1U ||
        streaming.stream().normal_wraps != 1U ||
        streaming.evidence().selected_asdus != 5U) {
        throw std::runtime_error("Streaming PCAP regression diverged from read_all");
    }
    CaptureAudit clean(opts);
    for (std::size_t i = 0U; i + 1U < packets.size(); ++i) {
        clean.observe(packets[i]);
    }
    if (!clean.ptp_visibility_complete() || !clean.wire_consistent() ||
        clean.stream().normal_wraps != 1U ||
        clean.evidence().ignored_sv_frames != 1U) {
        throw std::runtime_error("Clean interleaved PTP/SV PCAP rejected");
    }
    // A Follow_Up with a different sequenceId must NEVER qualify a Sync.
    auto wrong_follow_up = packets;
    auto mismatched_config = config;
    mismatched_config.sequence_id = 0x4321U;
    wrong_follow_up[2U].frame = ptp::PtpCodec::build_ethernet_frame(
        ptp::ptp_general_multicast_mac, ptp_mac,
        ptp::PtpCodec::build_follow_up(mismatched_config));
    CaptureAudit mismatched(opts);
    for (const auto& packet : wrong_follow_up) mismatched.observe(packet);
    if (mismatched.ptp_visibility_complete() ||
        mismatched.evidence().ptp_follow_up_orphan != 1U ||
        mismatched.evidence().ptp_follow_up_matched != 0U) {
        throw std::runtime_error("Mismatched PTP Follow_Up accepted");
    }
    // Missing Follow_Up is not repaired by arbitrary packet visibility.
    CaptureAudit missing_followup(opts);
    for (std::size_t i = 0U; i < packets.size(); ++i) {
        if (i != 2U) missing_followup.observe(packets[i]);
    }
    if (missing_followup.ptp_visibility_complete()) {
        throw std::runtime_error("Missing PTP Follow_Up accepted");
    }
    CaptureAudit audit(opts);
    for (const auto& p : packets) audit.observe(p);
    const auto& e = audit.evidence();
    const auto& s = audit.stream();
    if (e.ptp_announce != 1U || e.ptp_sync != 1U ||
        e.ptp_follow_up != 1U || e.ptp_follow_up_matched != 1U ||
        e.ptp_follow_up_orphan != 0U || e.selected_asdus != 5U ||
        e.ignored_sv_frames != 1U || s.normal_wraps != 1U ||
        s.gaps != 1U || s.missing_samples != 1U ||
        !audit.ptp_visibility_complete() || audit.wire_consistent()) {
        throw std::runtime_error("PTP+SV PCAP self-test failed");
    }
    std::cout << "PTP+SV combined offline PCAP regression PASS\n";
}

} // namespace

int main(const int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--self-test") {
            self_test();
            return 0;
        }
        if (argc < 2) {
            print_usage();
            return 64;
        }
        const std::filesystem::path path{argv[1]};
        AuditOptions options;
        bool json = false;
        bool has_app = false, has_modulus = false, has_domain = false;
        for (int i = 2; i < argc; ++i) {
            const std::string_view key{argv[i]};
            if (key == "--json") { json = true; continue; }
            if (i + 1 >= argc) throw std::invalid_argument("Missing option value");
            const std::string_view value{argv[++i]};
            if (key == "--sv-id") options.sv_id = std::string{value};
            else if (key == "--app-id") {
                options.app_id = static_cast<std::uint16_t>(
                    parse_unsigned(value, 65535U, "--app-id"));
                has_app = true;
            } else if (key == "--counter-modulus") {
                options.counter_modulus = static_cast<std::uint16_t>(
                    parse_unsigned(value, 65535U, "--counter-modulus"));
                has_modulus = true;
            } else if (key == "--ptp-domain") {
                options.ptp_domain = static_cast<std::uint8_t>(
                    parse_unsigned(value, 255U, "--ptp-domain"));
                has_domain = true;
            } else throw std::invalid_argument("Unknown argument: " + std::string{key});
        }
        if (options.sv_id.empty() || !has_app || !has_modulus ||
            options.counter_modulus < 2U || !has_domain) {
            print_usage();
            return 64;
        }
        CaptureAudit audit(options);
        capture::PcapReader::for_each_packet(
            path, [&audit](capture::PcapPacket&& packet) {
                audit.observe(packet);
            });
        display(audit, json);
        // A nonzero exit means a missing/anomalous software *wire* fixture;
        // exit=0 NEVER means an IED's PTP clock has physically synchronized.
        return audit.wire_consistent() && audit.ptp_visibility_complete() ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "PTP+SV wire audit: " << error.what() << "\n";
        return 1;
    }
}
