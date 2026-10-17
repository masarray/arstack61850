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
    QVariantList details;
    bool rowsTruncated{};
    bool textTruncated{};
    bool evidenceTruncated{};
};

inline constexpr int kBrowserReportVisibleMemberLimit = 256;
inline constexpr int kBrowserReportTextLimit = 1024;
inline constexpr int kBrowserReportDetailMembers = 32;
inline constexpr int kBrowserReportDetailChildren = 16;

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
        if (member.value &&
            member.value->kind() != mms::MmsDataKind::structure &&
            member.value->kind() != mms::MmsDataKind::array) {
            // Scalar values use the shared bounded formatter. Never construct
            // an expensive flattened aggregate just to discard that preview.
            const auto preview = previewMmsValue(*member.value,
                                                  kBrowserReportTextLimit);
            displayValue = QString::fromStdString(preview.text);
            valueTruncated = preview.truncated;
        }
        QVariantList detailRows;
        if (member.value) {
            const auto kind = member.value->kind();
            if (kind == mms::MmsDataKind::structure || kind == mms::MmsDataKind::array) {
                const auto& children = member.value->children();
                // Wire structure members are positional; without an SCL/type
                // binding never invent ST/q/t names or IEC enum meanings.
                displayValue = QStringLiteral("%1 · %2 field(s)%3")
                    .arg(kind == mms::MmsDataKind::structure
                             ? QStringLiteral("Structure") : QStringLiteral("Array"))
                    .arg(static_cast<qulonglong>(children.size()))
                    .arg(ordinal < kBrowserReportDetailMembers
                             ? QStringLiteral(" — select for details")
                             : QStringLiteral(" — details outside preview limit"));
                if (ordinal < kBrowserReportDetailMembers) {
                    const auto count = std::min(children.size(),
                        static_cast<std::size_t>(kBrowserReportDetailChildren));
                    for (std::size_t child = 0; child < count; ++child) {
                        const auto preview = previewMmsValue(
                            children[child], kBrowserReportTextLimit);
                        const auto label = QStringLiteral("Field %1 (wire order)")
                            .arg(static_cast<qulonglong>(child + 1U));
                        detailRows.push_back(QVariantMap{
                            {QStringLiteral("label"), label},
                            {QStringLiteral("value"), QString::fromStdString(preview.text)
                                + (preview.truncated ? QStringLiteral(" … [truncated]") : QString{})}});
                        if (preview.truncated) projection.textTruncated = true;
                    }
                    if (children.size() > count)
                        detailRows.push_back(QVariantMap{
                            {QStringLiteral("label"), QStringLiteral("Further fields")},
                            {QStringLiteral("value"), QStringLiteral("Not shown · bounded preview")}});
                }
            }
        }
        if (!detailRows.isEmpty())
            projection.details.push_back(QVariantMap{
                {QStringLiteral("index"), static_cast<int>(ordinal)},
                {QStringLiteral("rows"), detailRows}});
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
} // namespace arstack::iedsim
