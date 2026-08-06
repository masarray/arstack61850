// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace ar::iec61850::osi {

class TpktFormatError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct TpktFrame final {
    std::uint8_t version{};
    std::uint16_t declared_length{};
    std::vector<std::uint8_t> payload;

    friend bool operator==(const TpktFrame&, const TpktFrame&) = default;
};

class TpktFrameCodec final {
public:
    static constexpr std::uint8_t supported_version = 0x03U;
    static constexpr std::size_t header_length = 4U;
    static constexpr std::size_t maximum_frame_bytes = 65'535U;
    static constexpr std::size_t maximum_payload_bytes = maximum_frame_bytes - header_length;

    [[nodiscard]] static std::vector<std::uint8_t> encode(
        std::span<const std::uint8_t> payload);

    [[nodiscard]] static bool try_decode(
        std::span<const std::uint8_t> bytes,
        TpktFrame& frame,
        std::string* error = nullptr) noexcept;

    [[nodiscard]] static TpktFrame decode(std::span<const std::uint8_t> bytes);
};

class TpktStreamDecoder final {
public:
    static constexpr std::size_t default_maximum_buffered_bytes = 4U * 65'535U;

    explicit TpktStreamDecoder(
        std::size_t maximum_buffered_bytes = default_maximum_buffered_bytes);

    void append(std::span<const std::uint8_t> bytes);
    [[nodiscard]] bool try_pop(TpktFrame& frame);
    void reset() noexcept;

    [[nodiscard]] std::size_t buffered_bytes() const noexcept;
    [[nodiscard]] std::size_t decoded_frames() const noexcept { return decoded_frames_; }

private:
    void compact();

    std::vector<std::uint8_t> buffer_;
    std::size_t offset_{};
    std::size_t maximum_buffered_bytes_{};
    std::size_t decoded_frames_{};
};

} // namespace ar::iec61850::osi
