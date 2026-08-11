// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/data_set_span.hpp"
#include "ariec61850/mms/static_object_table.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {

struct MmsStaticDataSetMember final {
    std::string_view domain;
    std::string_view item;
};

struct MmsStaticDataSetEntry final {
    std::string_view domain;
    std::string_view item;
    std::span<const MmsStaticDataSetMember> members;
    bool mms_deletable{};
};

class MmsStaticDataSetTable final {
public:
    static constexpr std::size_t maximum_data_sets = 64U;

    constexpr MmsStaticDataSetTable() noexcept = default;

    explicit constexpr MmsStaticDataSetTable(
        const std::span<const MmsStaticDataSetEntry> data_sets) noexcept
        : data_sets_{data_sets} {}

    [[nodiscard]] bool valid() const noexcept;

    [[nodiscard]] bool valid_against(
        const MmsStaticObjectTable& objects) const noexcept;

    [[nodiscard]] const MmsStaticDataSetEntry* find(
        const MmsObjectNameView& name) const noexcept;

    [[nodiscard]] constexpr std::span<const MmsStaticDataSetEntry> data_sets() const noexcept {
        return data_sets_;
    }

private:
    std::span<const MmsStaticDataSetEntry> data_sets_{};
};

} // namespace ar::iec61850::mms
