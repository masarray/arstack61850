// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/data_value.hpp"

#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <variant>

// One typed, fail-closed engineering projection for native Browser q/t values.
// The underlying MMS data remains authoritative; GUI never guesses hex semantics.
namespace arstack::iedsim {
namespace detail {
inline void addEvidenceRow(QVariantList& rows, const QString& label, const QVariant& value) {
    rows.push_back(QVariantMap{{QStringLiteral("label"), label},
                               {QStringLiteral("value"), value}});
}
} // namespace detail

[[nodiscard]] inline QVariantMap decodeEngineeringEvidence(
    const QString& mmsItem, const ar::iec61850::mms::MmsDataValue& value) {
    using namespace ar::iec61850::mms;
    // Accept both typed MMS $FC$DO$q and IEC LN.DO.q report-member paths.
    const int leafSeparator = std::max(mmsItem.lastIndexOf(QLatin1Char('$')),
                                       mmsItem.lastIndexOf(QLatin1Char('.')));
    const QString leaf = mmsItem.mid(leafSeparator + 1);
    QVariantList rows;

    if (leaf == QStringLiteral("q") && value.kind() == MmsDataKind::bit_string) {
        const auto& bits = value.raw_value();  // [unused bits, octet 1, octet 2]
        // IEC 61850 Quality is 13 bits. A malformed/short wire value is NOT
        // translated to false flags (unknown != false).
        if (bits.size() != 3U || bits[0] != 3U || (bits[2] & 0x07U) != 0U)
            return {};
        const auto at = [&bits](const unsigned index) {
            return ((bits[1U + index / 8U] >> (7U - index % 8U)) & 1U) != 0U;
        };
        const unsigned validity = (at(0) ? 2U : 0U) | (at(1) ? 1U : 0U);
        const QStringList meanings{
            QStringLiteral("good"), QStringLiteral("invalid"),
            QStringLiteral("reserved"), QStringLiteral("questionable")};
        detail::addEvidenceRow(rows, QStringLiteral("Validity"), meanings.at(int(validity)));
        static constexpr const char* flags[]{
            "Overflow", "OutOfRange", "BadReference", "Oscillatory",
            "Failure", "OldData", "Inconsistent", "Inaccurate"};
        for (unsigned i = 0; i < 8U; ++i)
            detail::addEvidenceRow(rows, QString::fromLatin1(flags[i]), at(i + 2U));
        const QString source = at(10U) ? QStringLiteral("substituted") : QStringLiteral("process");
        detail::addEvidenceRow(rows, QStringLiteral("Source"), source);
        detail::addEvidenceRow(rows, QStringLiteral("Test"), at(11U));
        detail::addEvidenceRow(rows, QStringLiteral("OperatorBlocked"), at(12U));
        return {{QStringLiteral("kind"), QStringLiteral("quality")},
                {QStringLiteral("summary"), meanings.at(int(validity))},
                {QStringLiteral("rows"), rows}};
    }

    if (leaf == QStringLiteral("t") && value.kind() == MmsDataKind::utc_time) {
        const auto& utc = std::get<Iec61850UtcTime>(value.value());
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
            utc.value.time_since_epoch()).count();
        const QString timestamp = QDateTime::fromMSecsSinceEpoch(
            static_cast<qint64>(milliseconds), Qt::UTC)
            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
        const std::uint8_t timeQuality = utc.quality;
        const unsigned accuracy = timeQuality & 0x1FU;
        detail::addEvidenceRow(rows, QStringLiteral("UTC time"), timestamp);
        detail::addEvidenceRow(rows, QStringLiteral("LeapSecondsKnown"),
                               (timeQuality & 0x80U) != 0U);
        detail::addEvidenceRow(rows, QStringLiteral("ClockFailure"),
                               (timeQuality & 0x40U) != 0U);
        detail::addEvidenceRow(rows, QStringLiteral("ClockNotSynchronized"),
                               (timeQuality & 0x20U) != 0U);
        detail::addEvidenceRow(rows, QStringLiteral("TimeAccuracy"),
                               accuracy == 31U ? QVariant{QStringLiteral("unspecified")}
                                               : QVariant{int(accuracy)});
        return {{QStringLiteral("kind"), QStringLiteral("timestamp")},
                {QStringLiteral("summary"), timestamp},
                {QStringLiteral("rows"), rows}};
    }
    return {};
}
} // namespace arstack::iedsim
