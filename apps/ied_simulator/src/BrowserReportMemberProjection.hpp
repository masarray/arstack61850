// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MmsEngineeringEvidence.hpp"
#include "BrowserBoundedMmsPreview.hpp"
#include "ariec61850/mms/reporting.hpp"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

// Bounded, read-only projection of already-decoded MMS report members.
// Report runtime keeps full authoritative frame. The GUI only needs a preview.
namespace arstack::iedsim {
namespace mms = ar::iec61850::mms;

struct BrowserReportMemberProjection final {
    QStringList values;
    QVariantList evidence;
    bool rowsTruncated{};
    bool textTruncated{};
    bool evidenceTruncated{};
};

inline constexpr int kBrowserReportVisibleMemberLimit = 256;
inline constexpr int kBrowserReportTextLimit = 1024;

// Bound untrusted IEC member reference and reason strings BEFORE conversion to
// QString. No full-sized temporary allocation for oversized MMS metadata.
[[nodiscard]] inline QString boundedReportMetadata(
    const std::string_view source, const std::size_t maximumBytes, bool& truncated) {
    std::size_t count = std::min(maximumBytes, source.size());
    if (count < source.size()) {
        // Never split a UTF-8 continuation byte at the preview boundary.
        while (count > 0 && (static_cast<unsigned char>(source[count]) & 0xC0U) == 0x80U)
            --count;
        truncated = true;
    }
    return QString::fromUtf8(source.data(), static_cast<qsizetype>(count));
}

[[nodiscard]] inline bool hasReportTypedLeaf(
    const std::string_view reference, const mms::MmsDataKind kind) noexcept {
    if (reference.size() < 2U) return false;
    const char separator = reference[reference.size() - 2U];
    if (separator != '$' && separator != '.') return false;
    return (reference.back() == 'q' && kind == mms::MmsDataKind::bit_string) ||
           (reference.back() == 't' && kind == mms::MmsDataKind::utc_time);
}

[[nodiscard]] inline BrowserReportMemberProjection projectBrowserReportMembers(
    const std::vector<mms::MmsReportValue>& members,
    int& remainingTypedEvidence,
    const int requestedVisibleMembers = kBrowserReportVisibleMemberLimit) {
    BrowserReportMemberProjection projection;
    const int visibleLimit = std::clamp(requestedVisibleMembers, 0, kBrowserReportVisibleMemberLimit);
    const auto visibleCount = std::min(members.size(), static_cast<std::size_t>(visibleLimit));
    projection.rowsTruncated = members.size() > visibleCount;
    projection.values.reserve(static_cast<qsizetype>(visibleCount));

    for (std::size_t ordinal = 0; ordinal < visibleCount; ++ordinal) {
        const auto& member = members[ordinal];
        const std::string* rawReference = &member.data_reference;
        if (rawReference->empty() && member.member)
            rawReference = &member.member->user_reference;
        if (rawReference->empty() && member.member)
            rawReference = &member.member->mms_reference;
        const std::string_view fullReference(*rawReference);
        bool referenceTruncated = false;
        const QString reference = boundedReportMetadata(
            fullReference, kBrowserReportTextLimit, referenceTruncated);

        QString displayReference = reference;
        if (displayReference.isEmpty())
            displayReference = QStringLiteral("member[%1]").arg(
                static_cast<qulonglong>(member.data_set_index));
        QString displayValue = QStringLiteral("<failed>");
        bool valueTruncated = false;
        if (member.value) {
            // Never allocate a full string/hex/array representation and then
            // truncate it. Serialize directly into a 1024-character budget.
            const auto preview = previewMmsValue(*member.value,
                                                  kBrowserReportTextLimit);
            displayValue = QString::fromStdString(preview.text);
            valueTruncated = preview.truncated;
        }
        QString reasons;
        bool reasonsTruncated = false;
        for (const auto& reason : member.reason_for_inclusion.names) {
            if (reasons.size() >= kBrowserReportTextLimit) {
                reasonsTruncated = true;
                break;
            }
            if (!reasons.isEmpty()) reasons += QStringLiteral(", ");
            reasons += boundedReportMetadata(
                reason, static_cast<std::size_t>(
                    std::max(0, kBrowserReportTextLimit - int(reasons.size()))),
                reasonsTruncated);
            if (reasonsTruncated) break;
        }
        QString text = QStringLiteral("%1 = %2%3").arg(
            displayReference, displayValue,
            reasons.isEmpty() ? QString{} : QStringLiteral("  · ") + reasons);
        const bool rowTruncated = referenceTruncated || reasonsTruncated ||
                                  (member.value && valueTruncated) ||
                                  text.size() > kBrowserReportTextLimit;
        if (rowTruncated) {
            projection.textTruncated = true;
            const QString marker = QStringLiteral(" … [preview truncated]");
            text = text.left(kBrowserReportTextLimit - marker.size()) + marker;
        }
        projection.values.push_back(std::move(text));

        // Only read successful typed q/t values, and only when the retained
        // presentation row matches the member ordinal (never DataSet position).
        if (!member.value || fullReference.empty()) continue;
        if (referenceTruncated) {
            // A shortened IEC reference must never be treated as the identity
            // of a report q/t DA. The runtime retains the complete reference.
            if (hasReportTypedLeaf(fullReference, member.value->kind()))
                projection.evidenceTruncated = true;
            continue;
        }
        if (remainingTypedEvidence > 0) {
            const auto decoded = decodeEngineeringEvidence(reference, *member.value);
            if (!decoded.isEmpty()) {
                projection.evidence.push_back(QVariantMap{
                    {QStringLiteral("index"), static_cast<int>(ordinal)},
                    {QStringLiteral("evidence"), decoded}});
                --remainingTypedEvidence;
            }
        } else {
            // Do not decode (allocate rows) once the shared budget is empty.
            if (hasReportTypedLeaf(fullReference, member.value->kind()))
                projection.evidenceTruncated = true;
        }
    }
    return projection;
}

// A bounded, ready-to-render hierarchy. The native report frame remains the
// source of truth; QML must never parse an MMS structure's preview string.
// Positional MMS children deliberately say "Field N": without a verified SCL
// type they must NOT be misidentified as stVal, q, t, origin, etc.
inline constexpr int kBrowserReportTreeChildLimit = 192;
inline constexpr unsigned kBrowserReportTreeDepthLimit = 4U;
inline constexpr int kBrowserReportTreeScalarTextLimit = 160;

[[nodiscard]] inline QString browserReportTreeKind(const mms::MmsDataKind kind) {
    using K = mms::MmsDataKind;
    switch (kind) {
    case K::structure: return QStringLiteral("STRUCT");
    case K::array: return QStringLiteral("ARRAY");
    case K::bit_string: return QStringLiteral("BIT STRING");
    case K::utc_time: return QStringLiteral("UTC");
    case K::boolean: return QStringLiteral("BOOL");
    case K::integer: return QStringLiteral("INT");
    case K::unsigned_integer: return QStringLiteral("UINT");
    case K::floating_point: return QStringLiteral("FLOAT");
    case K::visible_string: return QStringLiteral("STRING");
    case K::octet_string: return QStringLiteral("OCTETS");
    default: return QStringLiteral("MMS");
    }
}

[[nodiscard]] inline QVariantList projectBrowserReportTree(
    const std::vector<mms::MmsReportValue>& members) {
    QVariantList rows;
    const auto visibleCount = std::min(
        members.size(), static_cast<std::size_t>(kBrowserReportVisibleMemberLimit));
    rows.reserve(static_cast<qsizetype>(visibleCount) + kBrowserReportTreeChildLimit);
    int remainingChildren = kBrowserReportTreeChildLimit;

    // Each nested child is serialized once, with bounded depth and text; all
    // labels are explicitly positional unless canonical identity is present.
    const auto appendValue = [&](const auto& visit, const mms::MmsDataValue& value,
                                 const int ordinal, const unsigned depth,
                                 const QString& title, const QString& fullReference,
                                 const bool fullReferenceShortened) -> void {
        const bool isContainer = value.kind() == mms::MmsDataKind::structure ||
                                 value.kind() == mms::MmsDataKind::array;
        QString displayed;
        if (isContainer) {
            displayed = QStringLiteral("%1 %2").arg(
                static_cast<qulonglong>(value.children().size())).arg(
                value.kind() == mms::MmsDataKind::array
                    ? QStringLiteral("item(s)") : QStringLiteral("field(s)"));
        } else {
            const auto preview = previewMmsValue(
                value, kBrowserReportTreeScalarTextLimit);
            displayed = QString::fromStdString(preview.text);
            if (preview.truncated) displayed += QStringLiteral(" …");
        }
        // Only a direct, uniquely identified q/t attribute can carry typed
        // Quality/TimeQuality meaning. Unknown positional children are raw MMS.
        if (depth == 0U && !isContainer && !fullReferenceShortened) {
            const auto evidence = decodeEngineeringEvidence(fullReference, value);
            if (!evidence.isEmpty())
                displayed = evidence.value(QStringLiteral("summary")).toString();
        }
        rows.push_back(QVariantMap{
            {QStringLiteral("memberIndex"), ordinal},
            {QStringLiteral("depth"), static_cast<int>(depth)},
            {QStringLiteral("name"), title},
            {QStringLiteral("reference"), fullReference},
            {QStringLiteral("value"), displayed},
            {QStringLiteral("kind"), browserReportTreeKind(value.kind())},
            {QStringLiteral("container"), isContainer},
            {QStringLiteral("received"), true}
        });
        if (!isContainer || depth >= kBrowserReportTreeDepthLimit)
            return;
        int index = 0;
        for (const auto& child : value.children()) {
            if (remainingChildren <= 0) break;
            --remainingChildren;
            visit(visit, child, ordinal, depth + 1U,
                  QStringLiteral("Field %1").arg(++index), fullReference,
                  fullReferenceShortened);
        }
    };

    for (std::size_t i = 0U; i < visibleCount; ++i) {
        const auto& member = members[i];
        const std::string* source = &member.data_reference;
        if (source->empty() && member.member) source = &member.member->user_reference;
        if (source->empty() && member.member) source = &member.member->mms_reference;
        bool shortened = false;
        const QString fullReference = boundedReportMetadata(
            *source, kBrowserReportTextLimit, shortened);
        QString name = fullReference;
        // Prefer compact canonical IEC paths while keeping full reference in
        // the per-row tooltip and never forging a functional constraint.
        const int slash = name.indexOf(QLatin1Char('/'));
        if (slash >= 0) name = name.mid(slash + 1);
        if (name.isEmpty()) name = QStringLiteral("Member %1").arg(
            static_cast<qulonglong>(member.data_set_index + 1U));

        if (member.value) {
            appendValue(appendValue, *member.value, static_cast<int>(i),
                        0U, name, fullReference, shortened);
        } else {
            rows.push_back(QVariantMap{
                {QStringLiteral("memberIndex"), static_cast<int>(i)},
                {QStringLiteral("depth"), 0},
                {QStringLiteral("name"), name},
                {QStringLiteral("reference"), fullReference},
                {QStringLiteral("value"), QStringLiteral("Read failed")},
                {QStringLiteral("kind"), QStringLiteral("MMS")},
                {QStringLiteral("container"), false},
                {QStringLiteral("received"), false}
            });
        }
    }
    return rows;
}
} // namespace arstack::iedsim
