// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedSignalModel.hpp"

#include <QHash>
#include <QVariantMap>
#include <QVector>

#include <algorithm>

namespace {
struct ObjectGroup final {
    QString name;
    QVector<int> members;
    int preferred{-1};
    bool objectMatches{};
};

QString referenceAt(const QVariantList& values, const int index) {
    if (index < 0 || index >= values.size()) return {};
    return values.at(index).toMap().value(QStringLiteral("reference")).toString();
}
}

IedSignalModel::IedSignalModel(QObject* parent)
    : QAbstractListModel(parent) {
    rebuildTimer_.setSingleShot(true);
    rebuildTimer_.setInterval(80);
    connect(&rebuildTimer_, &QTimer::timeout, this, &IedSignalModel::rebuild);

    refreshTimer_.setSingleShot(true);
    refreshTimer_.setInterval(16);
    connect(&refreshTimer_, &QTimer::timeout, this, &IedSignalModel::refreshSnapshot);
}

IedFleetController* IedSignalModel::backend() const noexcept { return backend_; }

void IedSignalModel::setBackend(IedFleetController* backend) {
    if (backend_ == backend) return;
    if (backend_ != nullptr) disconnect(backend_, nullptr, this, nullptr);
    backend_ = backend;
    observedIedIndex_ = -1;

    if (backend_ != nullptr) {
        connect(backend_, &IedFleetController::valuesChanged,
                this, &IedSignalModel::scheduleRefresh);
        connect(backend_, &IedFleetController::modelChanged,
                this, &IedSignalModel::scheduleRebuild);
        connect(backend_, &IedFleetController::selectionChanged, this, [this] {
            if (backend_ == nullptr) return;
            if (backend_->selectedIedIndex() != observedIedIndex_) {
                scheduleRebuild();
            } else {
                refreshSelectedRole();
            }
        });
    }

    emit backendChanged();
    scheduleRebuild();
}

QString IedSignalModel::logicalDevice() const { return logicalDevice_; }

void IedSignalModel::setLogicalDevice(const QString& value) {
    if (logicalDevice_ == value) return;
    logicalDevice_ = value;
    emit scopeChanged();
    scheduleRebuild();
}

QString IedSignalModel::logicalNode() const { return logicalNode_; }

void IedSignalModel::setLogicalNode(const QString& value) {
    if (logicalNode_ == value) return;
    logicalNode_ = value;
    emit scopeChanged();
    scheduleRebuild();
}

QString IedSignalModel::filterText() const { return filterText_; }

void IedSignalModel::setFilterText(const QString& value) {
    if (filterText_ == value) return;
    filterText_ = value;
    emit filterTextChanged();
    scheduleRebuild();
}

int IedSignalModel::visibleRowCount() const noexcept {
    return static_cast<int>(rows_.size());
}

int IedSignalModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariantMap IedSignalModel::sourceItem(const int sourceIndex) const {
    if (sourceIndex < 0 || sourceIndex >= sourceValues_.size()) return {};
    return sourceValues_.at(sourceIndex).toMap();
}

QVariant IedSignalModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    const auto item = sourceItem(row.sourceIndex);
    const bool objectRow = row.kind == RowKind::dataObject;

    switch (role) {
    case KindRole: return objectRow ? QStringLiteral("DO") : QStringLiteral("DA");
    case NameRole: return row.name;
    case DepthRole: return objectRow ? 0 : 1;
    case ValueRole: return item.value(QStringLiteral("value"));
    case FunctionalConstraintRole:
        return objectRow ? QVariant{} : item.value(QStringLiteral("fc"));
    case TypeRole:
        return objectRow ? item.value(QStringLiteral("cdc")) : item.value(QStringLiteral("type"));
    case QualityRole: return item.value(QStringLiteral("quality"));
    case SourceIndexRole: return row.sourceIndex;
    case WritableRole: return item.value(QStringLiteral("writable"));
    case ChangedRole: return item.value(QStringLiteral("changed"));
    case ReferenceRole: return item.value(QStringLiteral("reference"));
    case SelectedRole:
        return backend_ != nullptr && backend_->selectedValueIndex() == row.sourceIndex;
    default: return {};
    }
}

QHash<int, QByteArray> IedSignalModel::roleNames() const {
    return {
        {KindRole, "kind"},
        {NameRole, "name"},
        {DepthRole, "depth"},
        {ValueRole, "value"},
        {FunctionalConstraintRole, "fc"},
        {TypeRole, "type"},
        {QualityRole, "quality"},
        {SourceIndexRole, "sourceIndex"},
        {WritableRole, "writable"},
        {ChangedRole, "changed"},
        {ReferenceRole, "reference"},
        {SelectedRole, "selected"},
    };
}

void IedSignalModel::activate(const int rowIndex) {
    if (backend_ == nullptr || rowIndex < 0 || rowIndex >= rowCount()) return;
    const auto sourceIndex = rows_[static_cast<std::size_t>(rowIndex)].sourceIndex;
    if (sourceIndex >= 0) backend_->selectValue(sourceIndex);
}

void IedSignalModel::scheduleRebuild() {
    refreshTimer_.stop();
    rebuildTimer_.start();
}

void IedSignalModel::scheduleRefresh() {
    if (!rebuildTimer_.isActive() && !refreshTimer_.isActive()) refreshTimer_.start();
}

bool IedSignalModel::rowMatches(const QVariantMap& item, const QString& query) const {
    if (query.isEmpty()) return true;
    const QStringList fields{
        item.value(QStringLiteral("dataObject")).toString(),
        item.value(QStringLiteral("dataAttribute")).toString(),
        item.value(QStringLiteral("value")).toString(),
        item.value(QStringLiteral("fc")).toString(),
        item.value(QStringLiteral("type")).toString(),
        item.value(QStringLiteral("reference")).toString(),
    };
    for (const auto& field : fields) {
        if (field.contains(query, Qt::CaseInsensitive)) return true;
    }
    return false;
}

void IedSignalModel::rebuild() {
    const int previousCount = rowCount();
    observedIedIndex_ = backend_ != nullptr ? backend_->selectedIedIndex() : -1;
    sourceValues_ = backend_ != nullptr ? backend_->values() : QVariantList{};
    observedFirstReference_ = referenceAt(sourceValues_, 0);
    observedLastReference_ = referenceAt(sourceValues_, sourceValues_.size() - 1);

    QVector<ObjectGroup> groups;
    QHash<QString, int> groupIndex;
    const auto query = filterText_.trimmed();

    for (int sourceIndex = 0; sourceIndex < sourceValues_.size(); ++sourceIndex) {
        const auto item = sourceValues_.at(sourceIndex).toMap();
        if (item.value(QStringLiteral("logicalDevice")).toString() != logicalDevice_ ||
            item.value(QStringLiteral("logicalNode")).toString() != logicalNode_) {
            continue;
        }

        const auto objectName = item.value(QStringLiteral("dataObject")).toString();
        if (objectName.isEmpty()) continue;
        auto found = groupIndex.constFind(objectName);
        int index{};
        if (found == groupIndex.cend()) {
            index = groups.size();
            groupIndex.insert(objectName, index);
            groups.push_back(ObjectGroup{objectName, {}, -1,
                query.isEmpty() || objectName.contains(query, Qt::CaseInsensitive)});
        } else {
            index = found.value();
        }

        auto& group = groups[index];
        group.members.push_back(sourceIndex);
        const auto attribute = item.value(QStringLiteral("dataAttribute")).toString();
        if (group.preferred < 0 || attribute == QStringLiteral("stVal") ||
            attribute.endsWith(QStringLiteral(".stVal")) ||
            attribute == QStringLiteral("mag.f")) {
            group.preferred = sourceIndex;
        }
    }

    std::vector<Row> nextRows;
    nextRows.reserve(static_cast<std::size_t>(sourceValues_.size()));
    for (const auto& group : groups) {
        QVector<int> visibleMembers;
        visibleMembers.reserve(group.members.size());
        if (query.isEmpty() || group.objectMatches) {
            visibleMembers = group.members;
        } else {
            for (const auto sourceIndex : group.members) {
                if (rowMatches(sourceValues_.at(sourceIndex).toMap(), query)) {
                    visibleMembers.push_back(sourceIndex);
                }
            }
        }
        if (visibleMembers.isEmpty()) continue;

        int preferred = group.preferred;
        if (preferred < 0 || !group.members.contains(preferred)) preferred = visibleMembers.constFirst();
        nextRows.push_back(Row{RowKind::dataObject, preferred, group.name});
        for (const auto sourceIndex : visibleMembers) {
            const auto item = sourceValues_.at(sourceIndex).toMap();
            auto attribute = item.value(QStringLiteral("dataAttribute")).toString();
            if (attribute.isEmpty()) attribute = group.name;
            nextRows.push_back(Row{RowKind::dataAttribute, sourceIndex, attribute});
        }
    }

    beginResetModel();
    rows_ = std::move(nextRows);
    endResetModel();
    if (previousCount != rowCount()) emit visibleRowCountChanged();
}

void IedSignalModel::refreshSnapshot() {
    if (backend_ == nullptr) {
        scheduleRebuild();
        return;
    }
    const auto next = backend_->values();
    const bool sameStructure = backend_->selectedIedIndex() == observedIedIndex_ &&
        next.size() == sourceValues_.size() &&
        referenceAt(next, 0) == observedFirstReference_ &&
        referenceAt(next, next.size() - 1) == observedLastReference_;

    if (!sameStructure) {
        scheduleRebuild();
        return;
    }

    sourceValues_ = next;
    if (!rows_.empty()) {
        emit dataChanged(
            index(0, 0),
            index(rowCount() - 1, 0),
            {ValueRole, QualityRole, WritableRole, ChangedRole, SelectedRole});
    }
}

void IedSignalModel::refreshSelectedRole() {
    if (!rows_.empty()) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {SelectedRole});
    }
}
