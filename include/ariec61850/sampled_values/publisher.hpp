// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/embedded/io.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/sample_counter.hpp"
#include "ariec61850/wire/encode_result.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>

namespace ar::iec61850::sampled_values {

// Deterministic, allocation-bounded publisher runtime for host and MCU use.
//
// The caller owns the SampledValuesFrame configuration and Ethernet buffer.
// Configuration-time std::string/std::vector storage inside SampledValuesFrame is
// intentionally retained for compatibility, but poll()/publish_now() do not grow
// or replace those containers. The steady-state path mutates only smpCnt, encodes
// into caller-owned storage, and hands the resulting bytes to RawEthernetPort.
//
// This class deliberately does not sleep or create a thread. A host event loop,
// FreeRTOS task, esp_timer callback, or bare-metal scheduler owns wake-up policy.
struct SampledValuesPublisherConfig final {
    std::uint32_t sample_rate_hz{4'000U};
    std::optional<std::uint16_t> sample_count_wrap{std::uint16_t{4'000U}};
    std::uint16_t initial_sample_count{};
    bool send_immediately{true};
};

enum class SampledValuesPublishStatus : std::uint8_t {
    sent,
    not_due,
    invalid_configuration,
    encode_failed,
    transmit_failed,
};

struct SampledValuesPublisherStatistics final {
    std::uint64_t polls{};
    std::uint64_t frames_sent{};
    std::uint64_t encode_failures{};
    std::uint64_t transmit_failures{};
    std::uint64_t late_polls{};
    std::uint64_t maximum_lateness_us{};
};

struct SampledValuesPublishResult final {
    SampledValuesPublishStatus status{SampledValuesPublishStatus::not_due};
    wire::EncodeStatus encode_status{wire::EncodeStatus::ok};
    embedded::IoStatus io_status{embedded::IoStatus::ok};
    std::size_t frame_bytes{};
    std::uint16_t sample_count{};
    std::uint64_t next_due_us{};
    std::uint64_t lateness_us{};

    [[nodiscard]] constexpr bool sent() const noexcept {
        return status == SampledValuesPublishStatus::sent;
    }
};

class SampledValuesPublisher final {
public:
    static constexpr std::uint32_t maximum_supported_sample_rate_hz = 1'000'000U;

    SampledValuesPublisher(
        SampledValuesFrame& frame,
        const std::span<std::uint8_t> frame_buffer,
        const embedded::RawEthernetPort ethernet_port,
        const SampledValuesPublisherConfig config = {}) noexcept
        : frame_(&frame),
          frame_buffer_(frame_buffer),
          ethernet_port_(ethernet_port),
          config_(config),
          sample_count_(config.initial_sample_count) {
        valid_ =
            config_.sample_rate_hz > 0U &&
            config_.sample_rate_hz <= maximum_supported_sample_rate_hz &&
            !frame_buffer_.empty() &&
            ethernet_port_.transmit != nullptr &&
            !frame_->pdu.asdus.empty();
        apply_sample_count();
    }

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] std::uint16_t next_sample_count() const noexcept { return sample_count_; }
    [[nodiscard]] std::uint64_t next_due_us() const noexcept { return next_due_us_; }
    [[nodiscard]] const SampledValuesPublisherStatistics& statistics() const noexcept {
        return statistics_;
    }

    // Reset pacing and optionally the counter. The first later poll() starts a new
    // publisher session. This is the intended way to apply a deliberate rate or
    // stream-session change rather than feeding scheduler lateness back into pacing.
    void reset(const std::uint16_t sample_count = 0U) noexcept {
        started_ = false;
        next_due_us_ = 0U;
        interval_remainder_ = 0U;
        sample_count_ = sample_count;
        apply_sample_count();
    }

    // Poll the deterministic pacer at a monotonic timestamp. At most one frame is
    // attempted per call. After scheduler lateness, the next deadline is anchored
    // to the actual attempt time, preventing catch-up bursts.
    [[nodiscard]] SampledValuesPublishResult poll(const std::uint64_t now_us) noexcept {
        ++statistics_.polls;

        if (!valid_) {
            return make_result(
                SampledValuesPublishStatus::invalid_configuration,
                wire::EncodeStatus::value_out_of_range,
                embedded::IoStatus::invalid_argument,
                0U,
                sample_count_,
                0U);
        }

        if (!started_) {
            started_ = true;
            next_due_us_ = config_.send_immediately
                ? now_us
                : saturating_add(now_us, next_interval_us());
        }

        if (now_us < next_due_us_) {
            return make_result(
                SampledValuesPublishStatus::not_due,
                wire::EncodeStatus::ok,
                embedded::IoStatus::ok,
                0U,
                sample_count_,
                0U);
        }

        const auto lateness = now_us - next_due_us_;
        if (lateness > 0U) {
            ++statistics_.late_polls;
            statistics_.maximum_lateness_us =
                std::max(statistics_.maximum_lateness_us, lateness);
        }

        return transmit_current(now_us, lateness);
    }

    // Bypass due-time checking but keep the same no-catch-up scheduling rule.
    // Useful when an external high-resolution timer already provides the 4 kHz tick.
    [[nodiscard]] SampledValuesPublishResult publish_now(
        const std::uint64_t now_us) noexcept {
        ++statistics_.polls;

        if (!valid_) {
            return make_result(
                SampledValuesPublishStatus::invalid_configuration,
                wire::EncodeStatus::value_out_of_range,
                embedded::IoStatus::invalid_argument,
                0U,
                sample_count_,
                0U);
        }

        if (!started_) {
            started_ = true;
        }
        return transmit_current(now_us, 0U);
    }

private:
    [[nodiscard]] static std::uint64_t saturating_add(
        const std::uint64_t left,
        const std::uint64_t right) noexcept {
        if (right > std::numeric_limits<std::uint64_t>::max() - left) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        return left + right;
    }

    [[nodiscard]] std::uint64_t next_interval_us() noexcept {
        const auto rate = static_cast<std::uint64_t>(config_.sample_rate_hz);
        auto interval = 1'000'000ULL / rate;
        interval_remainder_ += 1'000'000ULL % rate;
        if (interval_remainder_ >= rate) {
            ++interval;
            interval_remainder_ -= rate;
        }
        return interval;
    }

    void schedule_after(const std::uint64_t now_us) noexcept {
        next_due_us_ = saturating_add(now_us, next_interval_us());
    }

    void apply_sample_count() noexcept {
        if (frame_ == nullptr) {
            return;
        }
        for (auto& asdu : frame_->pdu.asdus) {
            asdu.sample_count = sample_count_;
        }
    }

    [[nodiscard]] SampledValuesPublishResult make_result(
        const SampledValuesPublishStatus status,
        const wire::EncodeStatus encode_status,
        const embedded::IoStatus io_status,
        const std::size_t frame_bytes,
        const std::uint16_t sample_count,
        const std::uint64_t lateness_us) const noexcept {
        return {
            status,
            encode_status,
            io_status,
            frame_bytes,
            sample_count,
            next_due_us_,
            lateness_us};
    }

    [[nodiscard]] SampledValuesPublishResult transmit_current(
        const std::uint64_t now_us,
        const std::uint64_t lateness_us) noexcept {
        const auto transmitted_sample_count = sample_count_;
        apply_sample_count();

        const auto encoded = SampledValuesFrameCodec::encode_into(
            *frame_, frame_buffer_);
        if (!encoded.success()) {
            ++statistics_.encode_failures;
            schedule_after(now_us);
            return make_result(
                SampledValuesPublishStatus::encode_failed,
                encoded.status,
                embedded::IoStatus::ok,
                0U,
                transmitted_sample_count,
                lateness_us);
        }

        const auto bytes = frame_buffer_.first(encoded.bytes_written);
        const auto transmitted = ethernet_port_.send(bytes);
        if (!transmitted.success() || transmitted.transferred != encoded.bytes_written) {
            ++statistics_.transmit_failures;
            schedule_after(now_us);
            const auto status = transmitted.success()
                ? embedded::IoStatus::io_error
                : transmitted.status;
            return make_result(
                SampledValuesPublishStatus::transmit_failed,
                wire::EncodeStatus::ok,
                status,
                encoded.bytes_written,
                transmitted_sample_count,
                lateness_us);
        }

        ++statistics_.frames_sent;
        sample_count_ = SampleCounterPolicy::increment(
            sample_count_, config_.sample_count_wrap);
        apply_sample_count();
        schedule_after(now_us);
        return make_result(
            SampledValuesPublishStatus::sent,
            wire::EncodeStatus::ok,
            embedded::IoStatus::ok,
            encoded.bytes_written,
            transmitted_sample_count,
            lateness_us);
    }

    SampledValuesFrame* frame_{};
    std::span<std::uint8_t> frame_buffer_{};
    embedded::RawEthernetPort ethernet_port_{};
    SampledValuesPublisherConfig config_{};
    SampledValuesPublisherStatistics statistics_{};
    std::uint64_t next_due_us_{};
    std::uint64_t interval_remainder_{};
    std::uint16_t sample_count_{};
    bool valid_{};
    bool started_{};
};

} // namespace ar::iec61850::sampled_values
