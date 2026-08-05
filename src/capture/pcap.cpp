// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/capture/pcap.hpp"

#include <array>
#include <fstream>
#include <limits>
#include <string>

namespace ar::iec61850::capture {
namespace {

constexpr std::uint32_t magic_number = 0xA1B2C3D4U;
constexpr std::uint32_t magic_number_nanoseconds = 0xA1B23C4DU;
constexpr std::uint32_t link_type_ethernet = 1U;
constexpr std::size_t global_header_length = 24U;
constexpr std::size_t packet_header_length = 16U;

enum class ByteOrder { little_endian, big_endian };
enum class TimestampResolution { microseconds, nanoseconds };

std::uint16_t read_u16(const std::span<const std::uint8_t> source, const ByteOrder order) noexcept {
    if (order == ByteOrder::little_endian) {
        return static_cast<std::uint16_t>(source[0] |
            (static_cast<std::uint16_t>(source[1]) << 8U));
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(source[0]) << 8U) | source[1]);
}

std::uint32_t read_u32(const std::span<const std::uint8_t> source, const ByteOrder order) noexcept {
    if (order == ByteOrder::little_endian) {
        return static_cast<std::uint32_t>(source[0]) |
               (static_cast<std::uint32_t>(source[1]) << 8U) |
               (static_cast<std::uint32_t>(source[2]) << 16U) |
               (static_cast<std::uint32_t>(source[3]) << 24U);
    }
    return (static_cast<std::uint32_t>(source[0]) << 24U) |
           (static_cast<std::uint32_t>(source[1]) << 16U) |
           (static_cast<std::uint32_t>(source[2]) << 8U) |
           static_cast<std::uint32_t>(source[3]);
}

void write_u16_le(std::span<std::uint8_t> destination, const std::size_t offset,
                  const std::uint16_t value) noexcept {
    destination[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
}

void write_u32_le(std::span<std::uint8_t> destination, const std::size_t offset,
                  const std::uint32_t value) noexcept {
    destination[offset] = static_cast<std::uint8_t>(value & 0xFFU);
    destination[offset + 1U] = static_cast<std::uint8_t>((value >> 8U) & 0xFFU);
    destination[offset + 2U] = static_cast<std::uint8_t>((value >> 16U) & 0xFFU);
    destination[offset + 3U] = static_cast<std::uint8_t>((value >> 24U) & 0xFFU);
}

bool read_exactly(std::istream& stream, const std::span<std::uint8_t> destination) {
    stream.read(reinterpret_cast<char*>(destination.data()), static_cast<std::streamsize>(destination.size()));
    return stream.gcount() == static_cast<std::streamsize>(destination.size());
}

std::pair<ByteOrder, TimestampResolution> resolve_byte_order(
    const std::span<const std::uint8_t, 4> magic_bytes) {
    const auto little = read_u32(magic_bytes, ByteOrder::little_endian);
    if (little == magic_number) {
        return {ByteOrder::little_endian, TimestampResolution::microseconds};
    }
    if (little == magic_number_nanoseconds) {
        return {ByteOrder::little_endian, TimestampResolution::nanoseconds};
    }

    const auto big = read_u32(magic_bytes, ByteOrder::big_endian);
    if (big == magic_number) {
        return {ByteOrder::big_endian, TimestampResolution::microseconds};
    }
    if (big == magic_number_nanoseconds) {
        return {ByteOrder::big_endian, TimestampResolution::nanoseconds};
    }
    throw PcapFormatError("Unsupported PCAP magic number.");
}

std::chrono::system_clock::time_point to_timestamp(
    const std::uint32_t seconds,
    const std::uint32_t fractional,
    const TimestampResolution resolution) {
    auto timestamp = std::chrono::system_clock::time_point{std::chrono::seconds{seconds}};
    if (resolution == TimestampResolution::nanoseconds) {
        timestamp += std::chrono::nanoseconds{fractional};
    } else {
        timestamp += std::chrono::microseconds{fractional};
    }
    return timestamp;
}

} // namespace

std::vector<PcapPacket> PcapReader::read_all(const std::filesystem::path& file_path) {
    std::ifstream stream(file_path, std::ios::binary);
    if (!stream) {
        throw PcapFormatError("Unable to open PCAP file: " + file_path.string());
    }
    return read_all(stream);
}

std::vector<PcapPacket> PcapReader::read_all(std::istream& stream) {
    std::array<std::uint8_t, global_header_length> global_header{};
    if (!read_exactly(stream, global_header)) {
        throw PcapFormatError("PCAP file is shorter than the global header.");
    }

    const std::span<const std::uint8_t, 4> magic_span{global_header.data(), 4U};
    const auto [byte_order, resolution] = resolve_byte_order(magic_span);
    const auto version_major = read_u16(std::span<const std::uint8_t>{global_header}.subspan(4U, 2U), byte_order);
    const auto version_minor = read_u16(std::span<const std::uint8_t>{global_header}.subspan(6U, 2U), byte_order);
    const auto link_type = read_u32(std::span<const std::uint8_t>{global_header}.subspan(20U, 4U), byte_order);

    if (version_major != 2U || version_minor != 4U) {
        throw PcapFormatError("Unsupported PCAP version " + std::to_string(version_major) + "." +
                              std::to_string(version_minor) + ".");
    }
    if (link_type != link_type_ethernet) {
        throw PcapFormatError("Unsupported PCAP link type " + std::to_string(link_type) +
                              "; only Ethernet is supported.");
    }

    std::vector<PcapPacket> packets;
    while (true) {
        std::array<std::uint8_t, packet_header_length> packet_header{};
        stream.read(reinterpret_cast<char*>(packet_header.data()),
                    static_cast<std::streamsize>(packet_header.size()));
        const auto read_count = stream.gcount();
        if (read_count == 0) {
            break;
        }
        if (read_count != static_cast<std::streamsize>(packet_header.size())) {
            throw PcapFormatError("Truncated PCAP packet header.");
        }

        const auto header_span = std::span<const std::uint8_t>{packet_header};
        const auto seconds = read_u32(header_span.subspan(0U, 4U), byte_order);
        const auto fractional = read_u32(header_span.subspan(4U, 4U), byte_order);
        const auto included_length = read_u32(header_span.subspan(8U, 4U), byte_order);
        if (included_length > static_cast<std::uint32_t>(std::numeric_limits<std::streamsize>::max())) {
            throw PcapFormatError("PCAP packet is too large: " + std::to_string(included_length) + " bytes.");
        }

        std::vector<std::uint8_t> frame(included_length);
        if (!read_exactly(stream, frame)) {
            throw PcapFormatError("Truncated PCAP packet payload.");
        }
        packets.push_back({to_timestamp(seconds, fractional, resolution), std::move(frame)});
    }

    return packets;
}

PcapWriter::PcapWriter(std::ostream& stream, const std::uint32_t snap_length)
    : stream_(&stream) {
    write_global_header(snap_length);
}

void PcapWriter::write_packet(const std::chrono::system_clock::time_point timestamp,
                              const std::span<const std::uint8_t> frame) {
    if (stream_ == nullptr || !*stream_) {
        throw std::runtime_error("PCAP output stream is not writable.");
    }
    if (frame.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range("PCAP frame exceeds the 32-bit length field.");
    }

    const auto whole_seconds = std::chrono::floor<std::chrono::seconds>(timestamp);
    const auto unix_seconds = whole_seconds.time_since_epoch().count();
    if (unix_seconds < 0 || static_cast<std::uint64_t>(unix_seconds) > std::numeric_limits<std::uint32_t>::max()) {
        throw std::out_of_range("PCAP timestamp is outside the supported 32-bit Unix range.");
    }
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(timestamp - whole_seconds).count();

    std::array<std::uint8_t, packet_header_length> header{};
    const std::span<std::uint8_t> header_span{header};
    write_u32_le(header_span, 0U, static_cast<std::uint32_t>(unix_seconds));
    write_u32_le(header_span, 4U, static_cast<std::uint32_t>(micros));
    write_u32_le(header_span, 8U, static_cast<std::uint32_t>(frame.size()));
    write_u32_le(header_span, 12U, static_cast<std::uint32_t>(frame.size()));

    stream_->write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    stream_->write(reinterpret_cast<const char*>(frame.data()), static_cast<std::streamsize>(frame.size()));
    if (!*stream_) {
        throw std::runtime_error("Failed to write PCAP packet.");
    }
}

void PcapWriter::write_all(const std::filesystem::path& file_path,
                           const std::span<const PcapPacket> packets,
                           const std::uint32_t snap_length) {
    if (const auto parent = file_path.parent_path(); !parent.empty()) {
        std::filesystem::create_directories(parent);
    }
    std::ofstream stream(file_path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("Unable to create PCAP file: " + file_path.string());
    }

    PcapWriter writer(stream, snap_length);
    for (const auto& packet : packets) {
        writer.write_packet(packet.timestamp, packet.frame);
    }
}

void PcapWriter::write_global_header(const std::uint32_t snap_length) {
    std::array<std::uint8_t, global_header_length> header{};
    const std::span<std::uint8_t> span{header};
    write_u32_le(span, 0U, magic_number);
    write_u16_le(span, 4U, 2U);
    write_u16_le(span, 6U, 4U);
    write_u32_le(span, 8U, 0U);
    write_u32_le(span, 12U, 0U);
    write_u32_le(span, 16U, snap_length);
    write_u32_le(span, 20U, link_type_ethernet);

    stream_->write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!*stream_) {
        throw std::runtime_error("Failed to write PCAP global header.");
    }
}

} // namespace ar::iec61850::capture
