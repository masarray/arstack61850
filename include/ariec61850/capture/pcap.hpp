// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <istream>
#include <ostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace ar::iec61850::capture {

class PcapFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct PcapPacket final {
    std::chrono::system_clock::time_point timestamp;
    std::vector<std::uint8_t> frame;
};

class PcapReader final {
public:
    [[nodiscard]] static std::vector<PcapPacket> read_all(const std::filesystem::path& file_path);
    [[nodiscard]] static std::vector<PcapPacket> read_all(std::istream& stream);
};

class PcapWriter final {
public:
    explicit PcapWriter(std::ostream& stream, std::uint32_t snap_length = 65'535U);

    void write_packet(std::chrono::system_clock::time_point timestamp,
                      std::span<const std::uint8_t> frame);

    static void write_all(const std::filesystem::path& file_path,
                          std::span<const PcapPacket> packets,
                          std::uint32_t snap_length = 65'535U);

private:
    void write_global_header(std::uint32_t snap_length);

    std::ostream* stream_{};
};

} // namespace ar::iec61850::capture
