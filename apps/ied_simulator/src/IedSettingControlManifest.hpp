// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/scl/model.hpp"

#include <QByteArray>
#include <QLatin1Char>
#include <QSet>
#include <QString>

namespace arstack::iedsim {

inline QByteArray settingManifestField(QString value) {
    value.replace(QLatin1Char('\t'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return value.toUtf8();
}

inline QByteArray settingControlManifestLine(
    const ar::iec61850::scl::SclSettingControl& setting,
    const QString& activeIedName,
    QSet<QString>& emittedSettingControls) {
    const auto iedName = QString::fromStdString(setting.ied_name);
    if (iedName != activeIedName || !setting.valid()) return {};

    const auto domain = iedName + QString::fromStdString(setting.ld_inst);
    auto logicalNode = QString::fromStdString(setting.logical_node_path);
    logicalNode.replace(QLatin1Char('.'), QLatin1Char('$'));
    const auto item = logicalNode + QStringLiteral("$SP$SGCB");
    if (domain.isEmpty() || logicalNode.isEmpty()) return {};

    const auto key = domain + QLatin1Char('\n') + item;
    if (emittedSettingControls.contains(key)) return {};
    emittedSettingControls.insert(key);

    return "SGCB\t" + settingManifestField(domain) + "\t" +
        settingManifestField(item) + "\t" +
        QByteArray::number(*setting.number_of_setting_groups) + "\t" +
        QByteArray::number(*setting.active_setting_group) + "\n";
}

} // namespace arstack::iedsim
