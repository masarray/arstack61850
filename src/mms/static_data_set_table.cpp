// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/static_data_set_table.hpp"

#include <cstddef>
#include <span>
#include <string_view>

namespace ar::iec61850::mms {
namespace {

[[nodiscard]] bool valid_name(const std::string_view value) noexcept {
    if (value.empty() || value.size() > MmsServiceSpanCodec::maximum_identifier_bytes) {
        return false;
    }
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte == 0U || byte > 0x7FU) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool equals(
    const std::span<const std::uint8_t> bytes,
    const std::string_view text) noexcept {
    if (bytes.size() != text.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        if (bytes[index] != static_cast<std::uint8_t>(
                static_cast<unsigned char>(text[index]))) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool same_member(
    const MmsStaticDataSetMember& left,
    const MmsStaticDataSetMember& right) noexcept {
    return left.domain == right.domain && left.item == right.item;
}

} // namespace

bool MmsStaticDataSetTable::valid() const noexcept {
    if (data_sets_.size() > maximum_data_sets) {
        return false;
    }
    for (std::size_t index = 0U; index < data_sets_.size(); ++index) {
        const auto& data_set = data_sets_[index];
        if (!valid_name(data_set.domain) || !valid_name(data_set.item) ||
            data_set.members.empty() ||
            data_set.members.size() > MmsDataSetSpanCodec::maximum_members) {
            return false;
        }
        for (std::size_t earlier = 0U; earlier < index; ++earlier) {
            if (data_sets_[earlier].domain == data_set.domain &&
                data_sets_[earlier].item == data_set.item) {
                return false;
            }
        }
        for (std::size_t member = 0U; member < data_set.members.size(); ++member) {
            const auto& current = data_set.members[member];
            if (!valid_name(current.domain) || !valid_name(current.item)) {
                return false;
            }
            for (std::size_t earlier = 0U; earlier < member; ++earlier) {
                if (same_member(data_set.members[earlier], current)) {
                    return false;
                }
            }
        }
    }
    return true;
}

bool MmsStaticDataSetTable::valid_against(
    const MmsStaticObjectTable& objects) const noexcept {
    if (!valid() || !objects.valid()) {
        return false;
    }
    for (const auto& data_set : data_sets_) {
        for (const auto& member : data_set.members) {
            const MmsObjectNameView name{
                MmsObjectNameViewKind::domain_specific,
                std::span<const std::uint8_t>{
                    reinterpret_cast<const std::uint8_t*>(member.domain.data()),
                    member.domain.size()},
                std::span<const std::uint8_t>{
                    reinterpret_cast<const std::uint8_t*>(member.item.data()),
                    member.item.size()}};
            if (objects.find(name) == nullptr) {
                return false;
            }
        }
    }
    return true;
}

const MmsStaticDataSetEntry* MmsStaticDataSetTable::find(
    const MmsObjectNameView& name) const noexcept {
    if (name.kind != MmsObjectNameViewKind::domain_specific) {
        return nullptr;
    }
    for (const auto& data_set : data_sets_) {
        if (equals(name.domain, data_set.domain) && equals(name.item, data_set.item)) {
            return &data_set;
        }
    }
    return nullptr;
}

} // namespace ar::iec61850::mms
