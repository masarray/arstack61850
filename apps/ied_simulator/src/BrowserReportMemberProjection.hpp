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
        QString reference = QString::fromStdString(member.data_reference);
        if (reference.isEmpty() && member.member)
            reference = QString::fromStdString(member.member->user_reference);
        if (reference.isEmpty() && member.member)
            reference = QString::fromStdString(member.member->mms_reference);

        QString displayReference = reference;
        if (displayReference.isEmpty())
            displayReference = QStringLiteral("member[%1]").arg(
                static_cast<qulonglong>(member.data_set_index));
        QString displayValue = QStringLiteral("<failed>");
        if (member.value) {
            // Never allocate a full string/hex/array representation and then
            // truncate it. Serialize directly into a 1024-character budget.
            const auto preview = previewMmsValue(*member.value,
                                                  kBrowserReportTextLimit);
            displayValue = QString::fromStdString(preview.text);
            projection.textTruncated |= preview.truncated;
        }
        QString reasons;
        for (const auto& reason : member.reason_for_inclusion.names) {
            if (!reasons.isEmpty()) reasons += QStringLiteral(", ");
            reasons += QString::fromStdString(reason);
        }
        QString text = QStringLiteral("%1 = %2%3").arg(
            displayReference, displayValue,
            reasons.isEmpty() ? QString{} : QStringLiteral("  · ") + reasons);
        if (text.size() > kBrowserReportTextLimit) {
            projection.textTruncated = true;
            text = text.left(kBrowserReportTextLimit) + QStringLiteral(" … [preview truncated]");
        }
        projection.values.push_back(std::move(text));

        // Only read successful typed q/t values, and only when the retained
        // presentation row matches the member ordinal (never DataSet position).
        if (!member.value || reference.isEmpty()) continue;
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
            const auto separator = std::max(
                reference.lastIndexOf(QLatin1Char('$')),
                reference.lastIndexOf(QLatin1Char('.')));
            const QString leaf = reference.mid(separator + 1);
            const auto kind = member.value->kind();
            if ((leaf == QStringLiteral("q") && kind == mms::MmsDataKind::bit_string) ||
                (leaf == QStringLiteral("t") && kind == mms::MmsDataKind::utc_time))
                projection.evidenceTruncated = true;
        }
    }
    return projection;
}
} // namespace arstack::iedsim
