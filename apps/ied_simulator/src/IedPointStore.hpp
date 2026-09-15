// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/simulation/ied_simulator_profile.hpp"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class IedPointStore final {
public:
    struct PointRecord final {
        QString name;
        QString reference;
        QString logicalDevice;
        QString logicalNode;
        QString dataObject;
        QString dataAttribute;
        QString functionalConstraint;
        QString cdc;
        QString type;
        QString rawType;
        QString iedName;
        QString mmsDomain;
        QString mmsItem;
        QString value;
        QString quality{QStringLiteral("Good")};
        QString origin{QStringLiteral("Simulator")};
        QString updated{QStringLiteral("—")};
        QStringList options;
        bool writable{true};
        bool changed{};
    };

    [[nodiscard]] qsizetype size() const noexcept { return points_.size(); }
    [[nodiscard]] bool isEmpty() const noexcept { return points_.isEmpty(); }
    void clear();
    void reserve(qsizetype size);

    [[nodiscard]] const PointRecord* at(qsizetype index) const noexcept;
    [[nodiscard]] PointRecord* atMutable(qsizetype index) noexcept;
    [[nodiscard]] int indexOf(const QString& iedName, const QString& reference) const noexcept;

    int upsert(PointRecord point);
    int insertIfMissing(PointRecord point);

    [[nodiscard]] const QVector<PointRecord>& records() const noexcept { return points_; }
    [[nodiscard]] QVariantMap toVariantMap(qsizetype index) const;
    [[nodiscard]] QVariantList toVariantList(const QVector<int>& pointIndices) const;

    [[nodiscard]] static PointRecord fromSimulatorPoint(
        const ar::iec61850::simulation::IedSimulatorPoint& point);
    [[nodiscard]] static PointRecord fromVariantMap(const QVariantMap& value);
    [[nodiscard]] static QVariantMap toVariantMap(const PointRecord& point);
    [[nodiscard]] static QString keyFor(const QString& iedName, const QString& reference);

private:
    QVector<PointRecord> points_;
    QHash<QString, int> keyIndex_;
};
