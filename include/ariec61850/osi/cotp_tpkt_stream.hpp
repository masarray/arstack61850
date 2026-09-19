// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/osi/cotp_span.hpp"
#include "ariec61850/osi/tpkt_span.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

namespace ar::iec61850::osi {

struct CotpTpktDataStreamPlan final {
    std::size_t maximum_user_data{};
    std::size_t segment_count{};
    std::size_t required_bytes{};
};

class CotpTpktDataStreamSpanCodec final {
public:
    [[nodiscard]] static bool try_plan(
        const std::size_t user_data_bytes,
        const std::size_t negotiated_tpdu_size_bytes,
        CotpTpktDataStreamPlan& plan) noexcept {
        plan = {};
        if (negotiated_tpdu_size_bytes <= 3U ||
            negotiated_tpdu_size_bytes > TpktSpanCodec::maximum_payload_bytes) {
            return false;
        }

        plan.maximum_user_data = negotiated_tpdu_size_bytes - 3U;
        plan.segment_count = user_data_bytes == 0U
            ? 1U
            : 1U + ((user_data_bytes - 1U) / plan.maximum_user_data);
        constexpr std::size_t per_segment_overhead =
            TpktSpanCodec::header_length + 3U;
        if (plan.segment_count >
            (std::numeric_limits<std::size_t>::max() - user_data_bytes) /
                per_segment_overhead) {
            plan = {};
            return false;
        }
        plan.required_bytes = user_data_bytes +
            plan.segment_count * per_segment_overhead;
        return true;
    }

    // Encode one TSDU as one or more complete TPKT frames. Each frame contains
    // one COTP Data TPDU whose total TPDU size never exceeds the negotiated C0
    // value. EOT is clear on intermediate segments and set only on the final
    // segment. No heap allocation or hidden queue is used.
    [[nodiscard]] static wire::EncodeResult encode_into(
        const std::span<const std::uint8_t> user_data,
        const std::size_t negotiated_tpdu_size_bytes,
        const std::span<std::uint8_t> destination) noexcept {
        CotpTpktDataStreamPlan plan;
        if (!try_plan(user_data.size(), negotiated_tpdu_size_bytes, plan)) {
            return {wire::EncodeStatus::value_out_of_range, 0U, 0U};
        }
        if (destination.size() < plan.required_bytes) {
            return {wire::EncodeStatus::buffer_too_small, 0U, plan.required_bytes};
        }

        std::size_t input_offset{};
        std::size_t output_offset{};
        for (std::size_t segment = 0U; segment < plan.segment_count; ++segment) {
            const auto remaining = user_data.size() - input_offset;
            const auto chunk = std::min(remaining, plan.maximum_user_data);
            const bool final_segment = segment + 1U == plan.segment_count;
            const auto frame_bytes = TpktSpanCodec::header_length + 3U + chunk;
            if (frame_bytes > TpktSpanCodec::maximum_frame_bytes) {
                return {wire::EncodeStatus::value_out_of_range, 0U, plan.required_bytes};
            }

            auto frame = destination.subspan(output_offset, frame_bytes);
            const auto cotp = CotpSpanCodec::encode_data_into(
                user_data.subspan(input_offset, chunk),
                frame.subspan(TpktSpanCodec::header_length),
                final_segment,
                0U);
            if (!cotp.success() || cotp.bytes_written != chunk + 3U) {
                return {wire::EncodeStatus::value_out_of_range, 0U, plan.required_bytes};
            }

            frame[0] = TpktSpanCodec::supported_version;
            frame[1] = 0x00U;
            frame[2] = static_cast<std::uint8_t>((frame_bytes >> 8U) & 0xFFU);
            frame[3] = static_cast<std::uint8_t>(frame_bytes & 0xFFU);
            input_offset += chunk;
            output_offset += frame_bytes;
        }

        if (input_offset != user_data.size() || output_offset != plan.required_bytes) {
            return {wire::EncodeStatus::value_out_of_range, 0U, plan.required_bytes};
        }
        return {wire::EncodeStatus::ok, plan.required_bytes, plan.required_bytes};
    }
};

} // namespace ar::iec61850::osi
