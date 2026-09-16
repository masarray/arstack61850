// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/scl/model.hpp"

#include <QByteArray>
#include <QLatin1Char>
#include <QSet>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <cstdint>

namespace arstack::iedsim {

inline QByteArray reportManifestField(QString value) {
    value.replace(QLatin1Char('\t'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return value.toUtf8();
}

inline QStringList concreteReportControlNames(
    const QString& baseName,
    const bool indexed,
    const std::uint32_t maxClients) {
    if (baseName.isEmpty()) return {};
    if (!indexed) return {baseName};

    // IEC 61850 SCL keeps one ReportControl definition while RptEnabled@max
    // describes the number of concrete MMS RCB instances. Engineering clients
    // such as IEDScout address those instances with a two-digit suffix
    // (Buffer01, Buffer02, Unbuffer01, ...). Keep the SCL model definition-level
    // and expand only at the runtime-manifest boundary.
    const auto count = std::max<std::uint32_t>(1U, maxClients);
    QStringList names;
    names.reserve(static_cast<qsizetype>(count));
    for (std::uint32_t index = 1U; index <= count; ++index) {
        names.push_back(
            baseName + QStringLiteral("%1").arg(index, 2, 10, QLatin1Char('0')));
    }
    return names;
}

inline QByteArray reportControlManifestLines(
    const ar::iec61850::scl::SclReportControl& report,
    const QString& activeIedName,
    QSet<QString>& emittedReportControls) {
    const auto iedName = QString::fromStdString(report.ied_name);
    if (iedName != activeIedName) return {};

    const auto domain = iedName + QString::fromStdString(report.ld_inst);
    auto logicalNode = QString::fromStdString(report.logical_node_path);
    logicalNode.replace(QLatin1Char('.'), QLatin1Char('$'));
    const auto baseName = QString::fromStdString(report.name);
    const auto dataSetName = QString::fromStdString(report.data_set_name);
    QString dataSetDomain;
    QString dataSetItem;
    if (!dataSetName.isEmpty()) {
        dataSetDomain = domain;
        dataSetItem = logicalNode + QLatin1Char('$') + dataSetName;
        dataSetItem.replace(QLatin1Char('.'), QLatin1Char('$'));
    }
    if (domain.isEmpty() || logicalNode.isEmpty() || baseName.isEmpty()) {
        return {};
    }

    const auto triggerOptions = report.buffered ? 0x6CU : 0x64U;
    const auto optionalFields0 = report.buffered ? 0x79U : 0x78U;
    constexpr auto optionalFields1 = 0x80U;

    QByteArray lines;
    for (const auto& concreteName :
         concreteReportControlNames(baseName, report.indexed, report.max_clients)) {
        const auto item = logicalNode +
            (report.buffered ? QStringLiteral("$BR$") : QStringLiteral("$RP$")) +
            concreteName;
        const auto key = domain + QLatin1Char('\n') + item;
        if (emittedReportControls.contains(key)) continue;
        emittedReportControls.insert(key);

        auto reportId = QString::fromStdString(report.report_id);
        if (reportId.isEmpty()) reportId = domain + QLatin1Char('/') + item;

        lines += "RCB\t" + reportManifestField(domain) + "\t" +
            reportManifestField(item) + "\t" +
            QByteArray::number(report.buffered ? 1 : 0) + "\t" +
            reportManifestField(reportId) + "\t" + reportManifestField(dataSetDomain) + "\t" +
            reportManifestField(dataSetItem) + "\t" +
            QByteArray::number(report.configuration_revision) + "\t" +
            QByteArray::number(report.buffer_time_milliseconds) + "\t" +
            QByteArray::number(report.integrity_period_milliseconds) + "\t" +
            QByteArray::number(triggerOptions) + "\t" +
            QByteArray::number(optionalFields0) + "\t" +
            QByteArray::number(optionalFields1) + "\n";
    }
    return lines;
}

} // namespace arstack::iedsim
