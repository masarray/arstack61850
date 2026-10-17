// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "BrowserBoundedMmsPreview.hpp"
#include "MmsEngineeringEvidence.hpp"
#include "ariec61850/mms/live_discovery.hpp"

#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cstddef>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace arstack::iedsim {
namespace verified_report_detail {
namespace mms = ar::iec61850::mms;

// Only association-scoped, successful GVAA TypeSpecification responses can
// supply positional wire names. SCL guesses and inferred CDCs are not proof
// of field order. Keep pointers into the worker-owned discovery snapshot.
using WireTypeIndex = std::map<std::string, const mms::MmsTypeSpecification*, std::less<>>;
inline constexpr std::size_t kMaximumTypedLeaves = 64U;
inline constexpr std::size_t kMaximumTypeChildren = 64U;
inline constexpr unsigned kMaximumTypeDepth = 6U;

[[nodiscard]] inline std::string key(
    const std::string_view domain, const std::string_view item) {
    std::string result;
    result.reserve(domain.size() + 1U + item.size());
    result.append(domain);
    result.push_back('\x1f');
    result.append(item);
    return result;
}

[[nodiscard]] inline WireTypeIndex indexTypes(
    const std::vector<mms::MmsVariableTypeEvidence>& evidence) {
    WireTypeIndex result;
    for (const auto& item : evidence) {
        if (!item.success() || item.variable.kind != mms::MmsObjectNameKind::domain_specific ||
            item.variable.domain.empty() || item.variable.item.empty())
            continue;
        const auto identifier = key(item.variable.domain, item.variable.item);
        const auto [slot, inserted] = result.emplace(identifier, &item.attributes->type);
        if (!inserted) slot->second = nullptr; // conflicting/duplicate authority
    }
    return result;
}

[[nodiscard]] inline const mms::MmsTypeSpecification* findSchema(
    const WireTypeIndex& types, const mms::MmsDataSetDirectoryMember& member) {
    const auto& object = member.object_name;
    if (object.kind != mms::MmsObjectNameKind::domain_specific ||
        object.domain.empty() || object.item.empty())
        return nullptr;
    // Longest available exact path, then walk the actual named GVAA tree.
    // Prefix boundary is '$', never arbitrary string-prefix matching.
    std::string current = object.item;
    for (;;) {
        const auto found = types.find(key(object.domain, current));
        if (found != types.end()) {
            auto* node = found->second;
            if (!node) return nullptr;
            auto offset = current.size();
            while (offset < object.item.size()) {
                if (object.item[offset] != '$') return nullptr;
                const auto end = object.item.find('$', offset + 1U);
                const auto name = object.item.substr(offset + 1U,
                    end == std::string::npos ? std::string::npos : end - offset - 1U);
                if (name.empty()) return nullptr;
                const mms::MmsTypeSpecification* match{};
                for (const auto& child : node->children) {
                    if (child.name == name) {
                        if (match) return nullptr;
                        match = &child;
                    }
                }
                if (!match) return nullptr;
                node = match;
                offset = end == std::string::npos ? object.item.size() : end;
            }
            return node;
        }
        const auto cut = current.rfind('$');
        if (cut == std::string::npos) return nullptr;
        current.resize(cut);
    }
}

[[nodiscard]] inline bool matchingKind(
    const mms::MmsTypeKind type, const mms::MmsDataKind value) noexcept {
    using T = mms::MmsTypeKind;
    using V = mms::MmsDataKind;
    switch (type) {
    case T::structure: return value == V::structure;
    case T::boolean: return value == V::boolean;
    case T::bit_string: return value == V::bit_string;
    case T::integer: return value == V::integer;
    case T::unsigned_integer: return value == V::unsigned_integer;
    case T::floating_point: return value == V::floating_point;
    case T::octet_string: return value == V::octet_string;
    case T::visible_string: return value == V::visible_string;
    case T::binary_time: return value == V::binary_time;
    case T::bcd: return value == V::bcd;
    case T::boolean_array: return value == V::boolean_array;
    case T::object_id: return value == V::object_id;
    case T::mms_string: return value == V::mms_string;
    case T::utc_time: return value == V::utc_time;
    // MMS arrays require an explicit element/length contract not stored here.
    case T::array: case T::unknown: return false;
    }
    return false;
}

[[nodiscard]] inline bool validate(
    const mms::MmsTypeSpecification& type,
    const mms::MmsDataValue& value,
    std::size_t& leafCount,
    const unsigned depth = 0U) {
    if (depth > kMaximumTypeDepth || !matchingKind(type.kind, value.kind()))
        return false;
    if (type.kind != mms::MmsTypeKind::structure) {
        return ++leafCount <= kMaximumTypedLeaves;
    }
    if (type.children.empty() || type.children.size() != value.children().size() ||
        type.children.size() > kMaximumTypeChildren)
        return false;
    // Both order and uniqueness must be evidenced by the GVAA tree.
    for (std::size_t i = 0; i < type.children.size(); ++i) {
        const auto& child = type.children[i];
        if (child.name.empty() || child.name.size() > 128U) return false;
        for (std::size_t previous = 0; previous < i; ++previous)
            if (type.children[previous].name == child.name) return false;
        if (!validate(child, value.children()[i], leafCount, depth + 1U))
            return false;
    }
    return true;
}

inline void appendRows(
    QVariantList& rows,
    const mms::MmsTypeSpecification& type,
    const mms::MmsDataValue& value,
    const QString& relative,
    const unsigned depth = 0U) {
    if (type.kind == mms::MmsTypeKind::structure) {
        for (std::size_t i = 0; i < type.children.size(); ++i) {
            const auto childName = QString::fromStdString(type.children[i].name);
            const auto path = relative.isEmpty() ? childName
                : relative + QLatin1Char('.') + childName;
            appendRows(rows, type.children[i], value.children()[i], path, depth + 1U);
        }
        return;
    }
    const auto preview = previewMmsValue(value, 1024);
    const auto evidence = decodeEngineeringEvidence(relative, value);
    const QString display = evidence.value(QStringLiteral("summary")).toString();
    rows.push_back(QVariantMap{
        {QStringLiteral("label"), relative},
        {QStringLiteral("value"), display.isEmpty()
            ? QString::fromStdString(preview.text) : display},
        {QStringLiteral("engineeringEvidence"), evidence},
        {QStringLiteral("verified"), true},
        {QStringLiteral("depth"), static_cast<int>(depth)},
        {QStringLiteral("truncated"), preview.truncated}
    });
}

[[nodiscard]] inline QVariantList project(
    const mms::MmsReportValue& member,
    const WireTypeIndex& verifiedTypes) {
    if (!member.member || !member.value ||
        member.value->kind() != mms::MmsDataKind::structure)
        return {};
    const auto* type = findSchema(verifiedTypes, *member.member);
    if (!type) return {};
    std::size_t leaves{};
    if (!validate(*type, *member.value, leaves) || leaves == 0U)
        return {};
    QVariantList rows;
    rows.reserve(static_cast<qsizetype>(leaves));
    appendRows(rows, *type, *member.value, QString{});
    return rows;
}
} // namespace verified_report_detail
} // namespace arstack::iedsim
