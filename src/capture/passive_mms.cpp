// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/capture/passive_mms.hpp"

#include "ariec61850/mms/pdu.hpp"
#include "ariec61850/mms/services.hpp"
#include "ariec61850/osi/session.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace ar::iec61850::capture {
namespace {
std::uint16_t be16(std::span<const std::uint8_t> b, std::size_t n) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(b[n]) << 8U) | b[n + 1U]);
}
std::uint32_t be32(std::span<const std::uint8_t> b, std::size_t n) {
    return (static_cast<std::uint32_t>(b[n]) << 24U) |
        (static_cast<std::uint32_t>(b[n+1U]) << 16U) |
        (static_cast<std::uint32_t>(b[n+2U]) << 8U) |
        static_cast<std::uint32_t>(b[n+3U]);
}
std::string endpoint(std::span<const std::uint8_t> b, std::size_t ip, std::uint16_t port) {
    return std::to_string(b[ip]) + "." + std::to_string(b[ip+1U]) + "." +
        std::to_string(b[ip+2U]) + "." + std::to_string(b[ip+3U]) +
        ":" + std::to_string(port);
}
std::string service_name(std::int32_t tag) {
    switch (tag) {
    case 0: return "Status";
    case 1: return "GetNameList";
    case 2: return "Identify";
    case 4: return "Read";
    case 5: return "Write";
    case 6: return "GetVariableAccessAttributes";
    case 11: return "DefineNamedVariableList";
    case 12: return "GetNamedVariableListAttributes";
    case 13: return "DeleteNamedVariableList";
    case 72: return "FileOpen";
    case 73: return "FileRead";
    case 74: return "FileClose";
    case 77: return "FileDirectory";
    default: return "MMS service " + std::to_string(tag);
    }
}
std::string kind_name(mms::MmsPduKind kind) {
    switch (kind) {
    case mms::MmsPduKind::confirmed_request: return "Request";
    case mms::MmsPduKind::confirmed_response: return "Response";
    case mms::MmsPduKind::confirmed_error: return "Error";
    case mms::MmsPduKind::unconfirmed: return "Unconfirmed";
    case mms::MmsPduKind::reject: return "Reject";
    case mms::MmsPduKind::initiate_request: return "Initiate request";
    case mms::MmsPduKind::initiate_response: return "Initiate response";
    default: return "MMS";
    }
}
bool control_reference(const std::string& value) {
    return value.find("$CO$") != std::string::npos ||
        value.find("$Oper") != std::string::npos ||
        value.find("$SBO") != std::string::npos ||
        value.find("$Cancel") != std::string::npos;
}
std::string request_detail(std::span<const std::uint8_t> payload, std::int32_t tag,
                           std::string& service) {
    try {
        if (tag == 4) {
            const auto request = mms::MmsServiceCodec::decode_read_request(payload);
            return std::to_string(request.variables.size()) + " variable(s)";
        }
        if (tag == 5) {
            const auto request = mms::MmsServiceCodec::decode_write_request(payload);
            for (const auto& variable : request.variables) {
                if (control_reference(variable.reference())) {
                    service = "Control";
                    break;
                }
            }
            return std::to_string(request.variables.size()) + " variable(s)";
        }
    } catch (const std::exception&) {
        return "Service payload not fully decoded";
    }
    return {};
}
} // namespace

void PassiveMmsDecoder::record(PassiveMmsEvent event) {
    if (events_.size() >= maximum_events) {
        events_.erase(events_.begin());
        ++counters_.dropped;
    }
    events_.push_back(std::move(event));
}

void PassiveMmsDecoder::reset() noexcept {
    flows_.clear();
    events_.clear();
    counters_ = {};
}

void PassiveMmsDecoder::ingest(const PcapPacket& packet) {
    ingest(packet.frame, packet.timestamp);
}

void PassiveMmsDecoder::ingest(
    const std::span<const std::uint8_t> ethernet,
    const std::chrono::system_clock::time_point timestamp) {
    ++counters_.ethernet_packets;
    if (ethernet.size() < 14U || ethernet.size() > maximum_packet_bytes) {
        ++counters_.malformed;
        return;
    }

    std::size_t offset = 14U;
    auto ether_type = be16(ethernet, 12U);
    for (int tag = 0; tag < 2 && (ether_type == 0x8100U || ether_type == 0x88A8U); ++tag) {
        if (ethernet.size() < offset + 4U) {
            ++counters_.malformed;
            return;
        }
        ether_type = be16(ethernet, offset + 2U);
        offset += 4U;
    }
    if (ether_type != 0x0800U) return; // IPv4 only; never infer unsupported traffic.
    if (ethernet.size() < offset + 20U) {
        ++counters_.malformed;
        return;
    }
    const auto ip = ethernet.subspan(offset);
    if ((ip[0] >> 4U) != 4U) return;
    const auto ihl = static_cast<std::size_t>(ip[0] & 0x0FU) * 4U;
    if (ihl < 20U || ip.size() < ihl) {
        ++counters_.malformed;
        return;
    }
    const auto ip_length = static_cast<std::size_t>(be16(ip, 2U));
    if (ip_length < ihl || ip.size() < ip_length) {
        ++counters_.malformed;
        return;
    }
    if (ip[9] != 6U) return;
    if ((be16(ip, 6U) & 0x3FFFU) != 0U) {
        ++counters_.dropped; // Fragmented IPv4 requires IP defragmentation.
        return;
    }
    const auto tcp = ip.subspan(ihl, ip_length - ihl);
    if (tcp.size() < 20U) {
        ++counters_.malformed;
        return;
    }
    const auto src_port = be16(tcp, 0U);
    const auto dst_port = be16(tcp, 2U);
    if (src_port != 102U && dst_port != 102U) return;
    ++counters_.tcp_packets;
    const auto data_offset = static_cast<std::size_t>(tcp[12] >> 4U) * 4U;
    if (data_offset < 20U || data_offset > tcp.size()) {
        ++counters_.malformed;
        return;
    }
    const auto source = endpoint(ip, 12U, src_port);
    const auto destination = endpoint(ip, 16U, dst_port);
    const auto first = std::min(source, destination);
    const auto second = std::max(source, destination);
    const auto key = first + "|" + second;
    const int direction = source == first ? 0 : 1;
    if (!flows_.contains(key) && flows_.size() >= maximum_flows) {
        ++counters_.dropped;
        return;
    }
    auto& flow = flows_[key];
    const auto flags = tcp[13];
    process_segment(flow, direction, be32(tcp, 4U),
                    (flags & 0x02U) != 0U,
                    (flags & 0x01U) != 0U,
                    (flags & 0x04U) != 0U,
                    tcp.subspan(data_offset), timestamp, source, destination);
    if ((flags & 0x05U) != 0U) flows_.erase(key);
}

void PassiveMmsDecoder::process_segment(
    Flow& flow, int direction, std::uint32_t sequence,
    bool syn, bool fin, bool rst,
    std::span<const std::uint8_t> payload,
    std::chrono::system_clock::time_point timestamp,
    const std::string& source, const std::string& destination) {
    auto& stream = flow.directions[direction];
    if (rst) return;
    if (syn) {
        stream = Direction{};
        sequence += 1U;
    }
    if (payload.empty()) return;
    if (!stream.sequence_initialized) {
        stream.sequence_initialized = true;
        stream.next_sequence = sequence;
    }
    if (sequence < stream.next_sequence) {
        const auto overlap = static_cast<std::size_t>(stream.next_sequence - sequence);
        ++counters_.retransmissions;
        if (overlap >= payload.size()) return;
        payload = payload.subspan(overlap);
        sequence = stream.next_sequence;
    }
    if (sequence > stream.next_sequence) {
        ++counters_.out_of_order;
        if (stream.pending.size() >= maximum_pending_segments ||
            payload.size() > maximum_tcp_buffer) {
            stream.pending.clear();
            stream.tpkt.reset();
            stream.cotp.reset();
            ++counters_.gaps;
            ++counters_.dropped;
            return;
        }
        stream.pending.emplace(
            sequence, std::vector<std::uint8_t>{payload.begin(), payload.end()});
        return;
    }
    stream.next_sequence += static_cast<std::uint32_t>(payload.size());
    append_contiguous(flow, direction, payload, timestamp, source, destination);
    while (!stream.pending.empty()) {
        auto it = stream.pending.begin();
        if (it->first > stream.next_sequence) break;
        auto part = std::move(it->second);
        const auto pending_sequence = it->first;
        stream.pending.erase(it);
        const auto overlap = static_cast<std::size_t>(stream.next_sequence - pending_sequence);
        if (overlap >= part.size()) {
            ++counters_.retransmissions;
            continue;
        }
        const auto next = std::span<const std::uint8_t>{part}.subspan(overlap);
        stream.next_sequence += static_cast<std::uint32_t>(next.size());
        append_contiguous(flow, direction, next, timestamp, source, destination);
    }
    (void)fin;
}

void PassiveMmsDecoder::append_contiguous(
    Flow& flow, int direction,
    const std::span<const std::uint8_t> payload,
    const std::chrono::system_clock::time_point timestamp,
    const std::string& source, const std::string& destination) {
    auto& stream = flow.directions[direction];
    try {
        stream.tpkt.append(payload);
        osi::TpktFrame tpkt;
        while (stream.tpkt.try_pop(tpkt)) {
            const auto cotp = osi::CotpFrameCodec::decode(tpkt.payload);
            if (cotp.kind != osi::CotpTpduKind::data) {
                record({timestamp, source, destination, "OSI", "COTP",
                        "Transport handshake/control", std::nullopt, tpkt.declared_length});
                continue;
            }
            stream.cotp.append(cotp);
            if (!stream.cotp.is_complete()) continue;
            const auto session_payload = stream.cotp.complete();
            stream.cotp.reset();

            osi::SessionDataTransfer transfer;
            std::string error;
            if (!osi::SessionCodec::try_decode_data_transfer(
                    session_payload, transfer, &error)) {
                // Association negotiation is observable but not an MMS data-transfer.
                record({timestamp, source, destination, "Association", "ACSE",
                        "Session negotiation/control", std::nullopt, session_payload.size()});
                continue;
            }

            const auto envelope = mms::MmsPduCodec::decode_envelope(
                transfer.presentation_payload);
            std::string service = envelope.service_tag
                ? service_name(*envelope.service_tag)
                : "MMS";
            std::string detail;
            if (envelope.kind == mms::MmsPduKind::confirmed_request &&
                envelope.service_tag) {
                detail = request_detail(
                    transfer.presentation_payload, *envelope.service_tag, service);
                if (envelope.invoke_id) flow.outstanding[*envelope.invoke_id] = service;
                ++counters_.decoded_requests;
            } else if (envelope.kind == mms::MmsPduKind::confirmed_response ||
                       envelope.kind == mms::MmsPduKind::confirmed_error) {
                if (envelope.invoke_id) {
                    if (auto it = flow.outstanding.find(*envelope.invoke_id);
                        it != flow.outstanding.end()) {
                        service = it->second;
                        flow.outstanding.erase(it);
                    }
                }
                ++counters_.decoded_responses;
            }
            if (envelope.information_report) {
                service = "InformationReport";
                ++counters_.decoded_reports;
            }
            if (flow.outstanding.size() > 512U) flow.outstanding.clear();
            ++counters_.mms_messages;
            record({timestamp, source, destination, kind_name(envelope.kind),
                    service, detail, envelope.invoke_id, envelope.mms_payload.size()});
        }
    } catch (const std::exception& ex) {
        ++counters_.malformed;
        record({timestamp, source, destination, "Decode error", "MMS/OSI",
                ex.what(), std::nullopt, payload.size()});
        stream.tpkt.reset();
        stream.cotp.reset();
    }
}

} // namespace ar::iec61850::capture
