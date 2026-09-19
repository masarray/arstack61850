// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedPointStore.hpp"

#include <utility>

namespace {
QString qstring(const std::string& value) {
    return QString::fromStdString(value);
}
}

void IedPointStore::clear() {
    points_.clear();
    keyIndex_.clear();
}

void IedPointStore::reserve(const qsizetype size) {
    if (size <= 0) return;
    points_.reserve(size);
    keyIndex_.reserve(size);
}

const IedPointStore::PointRecord* IedPointStore::at(const qsizetype index) const noexcept {
    if (index < 0 || index >= points_.size()) return nullptr;
    return &points_.at(index);
}

IedPointStore::PointRecord* IedPointStore::atMutable(const qsizetype index) noexcept {
    if (index < 0 || index >= points_.size()) return nullptr;
    return &points_[index];
}

int IedPointStore::indexOf(const QString& iedName, const QString& reference) const noexcept {
    const auto found = keyIndex_.constFind(keyFor(iedName, reference));
    return found == keyIndex_.cend() ? -1 : found.value();
}

int IedPointStore::upsert(PointRecord point) {
    const auto key = keyFor(point.iedName, point.reference);
    const auto found = keyIndex_.constFind(key);
    if (found != keyIndex_.cend()) {
        points_[found.value()] = std::move(point);
        return found.value();
    }
    const int index = points_.size();
    points_.push_back(std::move(point));
    keyIndex_.insert(key, index);
    return index;
}

int IedPointStore::insertIfMissing(PointRecord point) {
    const auto key = keyFor(point.iedName, point.reference);
    const auto found = keyIndex_.constFind(key);
    if (found != keyIndex_.cend()) return found.value();
    const int index = points_.size();
    points_.push_back(std::move(point));
    keyIndex_.insert(key, index);
    return index;
}

QVariantMap IedPointStore::toVariantMap(const qsizetype index) const {
    const auto* point = at(index);
    return point == nullptr ? QVariantMap{} : toVariantMap(*point);
}

QVariantList IedPointStore::toVariantList(const QVector<int>& pointIndices) const {
    QVariantList result;
    result.reserve(pointIndices.size());
    for (const auto pointIndex : pointIndices) {
        const auto* point = at(pointIndex);
        if (point != nullptr) result.push_back(toVariantMap(*point));
    }
    return result;
}

IedPointStore::PointRecord IedPointStore::fromSimulatorPoint(
    const ar::iec61850::simulation::IedSimulatorPoint& point) {
    PointRecord record;
    record.dataObject = qstring(point.data_object);
    record.dataAttribute = qstring(point.data_attribute);
    record.name = record.dataAttribute.isEmpty()
        ? record.dataObject
        : record.dataObject + QLatin1Char('.') + record.dataAttribute;
    record.reference = qstring(point.reference);
    record.logicalDevice = qstring(point.logical_device);
    record.logicalNode = qstring(point.logical_node);
    record.functionalConstraint = qstring(point.functional_constraint);
    record.cdc = qstring(point.cdc);
    record.type = qstring(point.display_type);
    record.rawType = qstring(point.basic_type);
    record.iedName = qstring(point.ied_name);
    record.mmsDomain = qstring(point.mms_domain);
    record.mmsItem = qstring(point.mms_item);
    record.value = qstring(point.initial_value);
    if (point.display_type == "Enumeration" && point.cdc == "DPC") {
        record.options = {
            QStringLiteral("intermediate-state"),
            QStringLiteral("off"),
            QStringLiteral("on"),
            QStringLiteral("bad-state")};
    } else if (point.display_type == "Boolean") {
        record.options = {QStringLiteral("false"), QStringLiteral("true")};
    }
    return record;
}

IedPointStore::PointRecord IedPointStore::fromVariantMap(const QVariantMap& value) {
    PointRecord record;
    record.name = value.value(QStringLiteral("name")).toString();
    record.reference = value.value(QStringLiteral("reference")).toString();
    record.logicalDevice = value.value(QStringLiteral("logicalDevice")).toString();
    record.logicalNode = value.value(QStringLiteral("logicalNode")).toString();
    record.dataObject = value.value(QStringLiteral("dataObject")).toString();
    record.dataAttribute = value.value(QStringLiteral("dataAttribute")).toString();
    record.functionalConstraint = value.value(QStringLiteral("fc")).toString();
    record.cdc = value.value(QStringLiteral("cdc")).toString();
    record.type = value.value(QStringLiteral("type")).toString();
    record.rawType = value.value(QStringLiteral("rawType")).toString();
    record.iedName = value.value(QStringLiteral("iedName")).toString();
    record.mmsDomain = value.value(QStringLiteral("mmsDomain")).toString();
    record.mmsItem = value.value(QStringLiteral("mmsItem")).toString();
    record.value = value.value(QStringLiteral("value")).toString();
    record.quality = value.value(QStringLiteral("quality"), QStringLiteral("Good")).toString();
    record.origin = value.value(QStringLiteral("origin"), QStringLiteral("Simulator")).toString();
    record.updated = value.value(QStringLiteral("updated"), QStringLiteral("—")).toString();
    record.options = value.value(QStringLiteral("options")).toStringList();
    record.writable = value.value(QStringLiteral("writable"), true).toBool();
    record.changed = value.value(QStringLiteral("changed"), false).toBool();
    return record;
}

QVariantMap IedPointStore::toVariantMap(const PointRecord& point) {
    QVariantMap item;
    item.insert(QStringLiteral("name"), point.name);
    item.insert(QStringLiteral("reference"), point.reference);
    item.insert(QStringLiteral("logicalDevice"), point.logicalDevice);
    item.insert(QStringLiteral("logicalNode"), point.logicalNode);
    item.insert(QStringLiteral("dataObject"), point.dataObject);
    item.insert(QStringLiteral("dataAttribute"), point.dataAttribute);
    item.insert(QStringLiteral("fc"), point.functionalConstraint);
    item.insert(QStringLiteral("cdc"), point.cdc);
    item.insert(QStringLiteral("type"), point.type);
    item.insert(QStringLiteral("rawType"), point.rawType);
    item.insert(QStringLiteral("iedName"), point.iedName);
    item.insert(QStringLiteral("mmsDomain"), point.mmsDomain);
    item.insert(QStringLiteral("mmsItem"), point.mmsItem);
    item.insert(QStringLiteral("value"), point.value);
    item.insert(QStringLiteral("quality"), point.quality);
    item.insert(QStringLiteral("origin"), point.origin);
    item.insert(QStringLiteral("writable"), point.writable);
    item.insert(QStringLiteral("changed"), point.changed);
    item.insert(QStringLiteral("updated"), point.updated);
    if (!point.options.isEmpty()) item.insert(QStringLiteral("options"), point.options);
    return item;
}

QString IedPointStore::keyFor(const QString& iedName, const QString& reference) {
    return iedName + QLatin1Char('\x1f') + reference;
}
