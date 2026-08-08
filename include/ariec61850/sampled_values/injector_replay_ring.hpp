// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/replay_bundle.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace ar::iec61850::sampled_values {

struct InjectorReplayStatistics final {
    std::uint64_t frames_pushed{};
    std::uint64_t frames_popped{};
    std::uint64_t overflows{};
    std::uint64_t underruns{};
};

template <std::size_t Capacity>
class InjectorReplayRing final {
public:
    static_assert(Capacity >= 2U);

    [[nodiscard]] constexpr std::size_t capacity() const noexcept { return Capacity; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::size_t free_slots() const noexcept { return Capacity - size_; }
    [[nodiscard]] bool empty() const noexcept { return size_ == 0U; }
    [[nodiscard]] bool full() const noexcept { return size_ == Capacity; }
    [[nodiscard]] const InjectorReplayStatistics& statistics() const noexcept {
        return statistics_;
    }

    void reset() noexcept {
        read_index_ = 0U;
        write_index_ = 0U;
        size_ = 0U;
        statistics_ = {};
    }

    [[nodiscard]] bool push(const ReplayPayloadFrame& frame) noexcept {
        if (full()) {
            ++statistics_.overflows;
            return false;
        }
        frames_[write_index_] = frame;
        write_index_ = next(write_index_);
        ++size_;
        ++statistics_.frames_pushed;
        return true;
    }

    [[nodiscard]] bool pop(ReplayPayloadFrame& frame) noexcept {
        if (empty()) {
            ++statistics_.underruns;
            return false;
        }
        frame = frames_[read_index_];
        read_index_ = next(read_index_);
        --size_;
        ++statistics_.frames_popped;
        return true;
    }

private:
    [[nodiscard]] static constexpr std::size_t next(
        const std::size_t index) noexcept {
        return index + 1U == Capacity ? 0U : index + 1U;
    }

    std::array<ReplayPayloadFrame, Capacity> frames_{};
    std::size_t read_index_{};
    std::size_t write_index_{};
    std::size_t size_{};
    InjectorReplayStatistics statistics_{};
};

} // namespace ar::iec61850::sampled_values
