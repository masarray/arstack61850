// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/osi/tpkt.hpp"

#include <algorithm>
#include <limits>

namespace ar::iec61850::osi {
namespace {

void set_error(std::string* error, std::string message) noexcept {
    if (error == nullptr) {
        return;
    }
    try {
        *error = std::move(message);
    } catch (...) {
    }
}

std::uint16_t read_be_u16(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[offset]) << 8U) |
        static_cast<std::uint16_t>(bytes[offset + 1U]));
}

} // namespace

std::vector<std::uint8_t> TpktFrameCodec::encode(
    const std::span<const std::uint8_t> payload) {
    if (payload.size() > maximum_payload_bytes) {
        throw std::length_error("TPKT payload exceeds the 65,531-byte protocol limit.");
    }

    const auto total_length = payload.size() + header_length;
    const auto declared = static_cast<std::uint16_t>(total_length);
    std::vector<std::uint8_t> encoded(total_length);
    encoded[0] = supported_version;
    encoded[1] = 0x00U;
    encoded[2] = static_cast<std::uint8_t>(declared >> 8U);
    encoded[3] = static_cast<std::uint8_t>(declared & 0xFFU);
    std::copy(payload.begin(), payload.end(), encoded.begin() +
        static_cast<std::ptrdiff_t>(header_length));
    return encoded;
}

bool TpktFrameCodec::try_decode(
    const std::span<const std::uint8_t> bytes,
    TpktFrame& frame,
    std::string* error) noexcept {
    try {
        frame = {};
        if (bytes.size() < header_length) {
            set_error(error, "TPKT frame is shorter than the four-byte header.");
            return false;
        }
        if (bytes[0] != supported_version) {
            set_error(error, "Unsupported TPKT version.");
            return false;
        }
        if (bytes[1] != 0x00U) {
            set_error(error, "TPKT reserved octet must be zero.");
            return false;
        }

        const auto declared = read_be_u16(bytes, 2U);
        if (declared < header_length) {
            set_error(error, "TPKT declared length is smaller than the header.");
            return false;
        }
        if (static_cast<std::size_t>(declared) != bytes.size()) {
            set_error(error, "TPKT declared length does not match the supplied frame length.");
            return false;
        }

        frame.version = bytes[0];
        frame.declared_length = declared;
        frame.payload.assign(bytes.begin() + static_cast<std::ptrdiff_t>(header_length), bytes.end());
        if (error != nullptr) {
            error->clear();
        }
        return true;
    } catch (...) {
        frame = {};
        set_error(error, "TPKT decode failed while allocating frame storage.");
        return false;
    }
}

TpktFrame TpktFrameCodec::decode(const std::span<const std::uint8_t> bytes) {
    TpktFrame frame;
    std::string error;
    if (!try_decode(bytes, frame, &error)) {
        throw TpktFormatError(error);
    }
    return frame;
}

TpktStreamDecoder::TpktStreamDecoder(const std::size_t maximum_buffered_bytes)
    : maximum_buffered_bytes_(maximum_buffered_bytes) {
    if (maximum_buffered_bytes_ < TpktFrameCodec::header_length ||
        maximum_buffered_bytes_ > 64U * 1024U * 1024U) {
        throw std::invalid_argument("TPKT stream buffer limit is outside the supported range.");
    }
}

void TpktStreamDecoder::append(const std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) {
        return;
    }
    compact();
    if (bytes.size() > maximum_buffered_bytes_ - buffer_.size()) {
        throw TpktFormatError("TPKT stream exceeded the bounded buffer limit.");
    }
    buffer_.insert(buffer_.end(), bytes.begin(), bytes.end());
}

bool TpktStreamDecoder::try_pop(TpktFrame& frame) {
    const auto available = buffered_bytes();
    if (available < TpktFrameCodec::header_length) {
        return false;
    }

    const std::span<const std::uint8_t> unread{buffer_.data() + offset_, available};
    if (unread[0] != TpktFrameCodec::supported_version) {
        throw TpktFormatError("Unsupported TPKT version in stream.");
    }
    if (unread[1] != 0x00U) {
        throw TpktFormatError("TPKT reserved octet in stream must be zero.");
    }
    const auto declared = read_be_u16(unread, 2U);
    if (declared < TpktFrameCodec::header_length) {
        throw TpktFormatError("TPKT stream declared an invalid frame length.");
    }
    if (static_cast<std::size_t>(declared) > maximum_buffered_bytes_) {
        throw TpktFormatError("TPKT frame exceeds the configured stream buffer limit.");
    }
    if (available < static_cast<std::size_t>(declared)) {
        return false;
    }

    const auto exact = unread.first(static_cast<std::size_t>(declared));
    frame = TpktFrameCodec::decode(exact);
    offset_ += static_cast<std::size_t>(declared);
    ++decoded_frames_;
    compact();
    return true;
}

void TpktStreamDecoder::reset() noexcept {
    buffer_.clear();
    offset_ = 0U;
    decoded_frames_ = 0U;
}

std::size_t TpktStreamDecoder::buffered_bytes() const noexcept {
    return buffer_.size() - offset_;
}

void TpktStreamDecoder::compact() {
    if (offset_ == 0U) {
        return;
    }
    if (offset_ == buffer_.size()) {
        buffer_.clear();
        offset_ = 0U;
        return;
    }
    if (offset_ >= buffer_.size() / 2U || offset_ >= 65'535U) {
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset_));
        offset_ = 0U;
    }
}

} // namespace ar::iec61850::osi
