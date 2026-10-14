// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/data_value.hpp"

#include <QDateTime>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <chrono>
#include <cstdint>
#include <variant>

// Typed IEC 61850 evidence, shared by Browser Data Model/DataSets.
// Never parse hex display text or infer quality flags from missing reads.
namespace arstack::iedsim {

inline QVariantMap decodeMmsSemanticAttribute(
    const ar::iec61850::mms::MmsDataValue& data, const QString& mmsItem) {
    namespace mms = ar::iec61850::mms;
    const QString leaf = mmsItem.mid(mmsItem.lastIndexOf(QLatin1Char('$')) + 1);
    const auto append = [](QVariantList& fields, const QString& name, const QVariant& value) {
        fields.append(QVariantMap{{QStringLiteral("label"), name},
                                  {QStringLiteral("value"), value}});
    };
    if (leaf == QStringLiteral("q") && data.kind() == mms::MmsDataKind::bit_string) {
        const auto& raw = data.raw_value(); // BER content: unused-bit count + payload
        if (raw.size() != 3U || raw[0] != 3U || (raw[2] & 0x07U) != 0U)
            return {}; // quality is exactly 13 meaningful bits; invalid evidence stays unknown
        const std::uint16_t bits = (std::uint16_t(raw[1]) << 8U) | raw[2];
        const auto active = [bits](unsigned shift) -> bool {
            return (bits & (std::uint16_t(0x8000U) >> shift)) != 0U;
        };
        const unsigned validity = (active(0) ? 2U : 0U) | (active(1) ? 1U : 0U);
        const QString validityName = validity == 0U ? QStringLiteral("good")
            : validity == 1U ? QStringLiteral("invalid")
            : validity == 2U ? QStringLiteral("reserved")
            : QStringLiteral("questionable");
        QVariantList fields;
        append(fields, QStringLiteral("Validity"), validityName);
        constexpr const char* labels[] = {
            "Overflow", "OutOfRange", "BadReference", "Oscillatory",
            "Failure", "OldData", "Inconsistent", "Inaccurate"
        };
        for (unsigned offset = 0U; offset < 8U; ++offset)
            append(fields, QString::fromLatin1(labels[offset]), active(offset + 2U));
        append(fields, QStringLiteral("Source"),
               active(10U) ? QStringLiteral("substituted") : QStringLiteral("process"));
        append(fields, QStringLiteral("Test"), active(11U));
        append(fields, QStringLiteral("OperatorBlocked"), active(12U));
        return {{QStringLiteral("kind"), QStringLiteral("quality")},
                {QStringLiteral("summary"), validityName},
                {QStringLiteral("evidence"), QStringLiteral("MMS Read")},
                {QStringLiteral("fields"), fields}};
    }
    if (leaf == QStringLiteral("t") && data.kind() == mms::MmsDataKind::utc_time) {
        const auto* utc = std::get_if<mms::Iec61850UtcTime>(&data.value());
        if (!utc) return {};
        const auto epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            utc->value.time_since_epoch()).count();
        const QString timeIso = QDateTime::fromMSecsSinceEpoch(epochMs, Qt::UTC)
                                    .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz 'UTC'"));
        const unsigned accuracy = utc->quality & 0x1FU;
        QVariantList fields;
        append(fields, QStringLiteral("UTC time"), timeIso);
        append(fields, QStringLiteral("LeapSecondsKnown"), (utc->quality & 0x80U) != 0U);
        append(fields, QStringLiteral("ClockFailure"), (utc->quality & 0x40U) != 0U);
        append(fields, QStringLiteral("ClockNotSynchronized"), (utc->quality & 0x20U) != 0U);
        append(fields, QStringLiteral("TimeAccuracy"),
               accuracy == 31U ? QStringLiteral("unspecified")
                   : QString::number(accuracy));
        return {{QStringLiteral("kind"), QStringLiteral("timestamp")},
                {QStringLiteral("summary"), timeIso},
                {QStringLiteral("evidence"), QStringLiteral("MMS Read")},
                {QStringLiteral("fields"), fields}};
    }
    return {};
}

} // namespace arstack::iedsim
