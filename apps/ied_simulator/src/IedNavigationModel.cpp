// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedNavigationModel.hpp"

#include <QHash>
#include <QVariantMap>
#include <QVector>

namespace {
struct DeviceGroup final {
    QString name;
    QStringList logicalNodes;
};
}

IedNavigationModel::IedNavigationModel(QObject* parent)
    : QAbstractListModel(parent) {
    rebuildTimer_.setSingleShot(true);
    rebuildTimer_.setInterval(16);
    connect(&rebuildTimer_, &QTimer::timeout, this, &IedNavigationModel::rebuild);
}

IedFleetController* IedNavigationModel::backend() const noexcept { return backend_; }

void IedNavigationModel::setBackend(IedFleetController* backend) {
    if (backend_ == backend) return;
    if (backend_ != nullptr) disconnect(backend_, nullptr, this, nullptr);
    backend_ = backend;
    observedIedIndex_ = -1;

    if (backend_ != nullptr) {
        // Value edits do not change LD/LN topology. Rebuilding navigation for
        // every live valuesChanged burst used to rescan tens of thousands of
        // point maps on the GUI thread for no structural benefit.
        connect(backend_, &IedFleetController::modelChanged,
                this, &IedNavigationModel::scheduleRebuild);
        connect(backend_, &IedFleetController::selectionChanged, this, [this] {
            if (backend_ != nullptr && backend_->selectedIedIndex() != observedIedIndex_) {
                scheduleRebuild();
            }
        });
    }

    emit backendChanged();
    scheduleRebuild();
}

QString IedNavigationModel::selectedLogicalDevice() const { return selectedLogicalDevice_; }
QString IedNavigationModel::selectedLogicalNode() const { return selectedLogicalNode_; }

int IedNavigationModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant IedNavigationModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    switch (role) {
    case KindRole: return row.kind;
    case NameRole: return row.name;
    case DepthRole: return row.depth;
    case ExpandedRole: return row.expanded;
    case ExpandableRole: return row.expandable;
    case SelectedRole:
        return row.kind == QStringLiteral("LN") &&
            row.logicalDevice == selectedLogicalDevice_ &&
            row.logicalNode == selectedLogicalNode_;
    case LogicalDeviceRole: return row.logicalDevice;
    case LogicalNodeRole: return row.logicalNode;
    default: return {};
    }
}

QHash<int, QByteArray> IedNavigationModel::roleNames() const {
    return {
        {KindRole, "kind"},
        {NameRole, "name"},
        {DepthRole, "depth"},
        {ExpandedRole, "expanded"},
        {ExpandableRole, "expandable"},
        {SelectedRole, "selected"},
        {LogicalDeviceRole, "logicalDevice"},
        {LogicalNodeRole, "logicalNode"},
    };
}

void IedNavigationModel::activate(const int rowIndex) {
    if (rowIndex < 0 || rowIndex >= rowCount()) return;
    const auto row = rows_[static_cast<std::size_t>(rowIndex)];
    if (row.kind == QStringLiteral("LD")) {
        if (collapsedDevices_.contains(row.logicalDevice)) {
            collapsedDevices_.remove(row.logicalDevice);
        } else {
            collapsedDevices_.insert(row.logicalDevice);
        }
        scheduleRebuild();
        return;
    }
    if (row.kind != QStringLiteral("LN")) return;
    if (selectedLogicalDevice_ == row.logicalDevice && selectedLogicalNode_ == row.logicalNode) return;

    selectedLogicalDevice_ = row.logicalDevice;
    selectedLogicalNode_ = row.logicalNode;
    if (!rows_.empty()) {
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {SelectedRole});
    }
    emit selectionChanged();
}

void IedNavigationModel::expandAll() {
    if (collapsedDevices_.isEmpty()) return;
    collapsedDevices_.clear();
    scheduleRebuild();
}

void IedNavigationModel::collapseAll() {
    QSet<QString> next;
    for (const auto& row : rows_) {
        if (row.kind == QStringLiteral("LD")) next.insert(row.logicalDevice);
    }
    if (next == collapsedDevices_) return;
    collapsedDevices_ = std::move(next);
    scheduleRebuild();
}

void IedNavigationModel::scheduleRebuild() {
    if (!rebuildTimer_.isActive()) rebuildTimer_.start();
}

bool IedNavigationModel::containsSelection(
    const QString& logicalDevice,
    const QString& logicalNode) const {
    for (const auto& row : rows_) {
        if (row.kind == QStringLiteral("LN") && row.logicalDevice == logicalDevice &&
            row.logicalNode == logicalNode) {
            return true;
        }
    }
    return false;
}

void IedNavigationModel::rebuild() {
    const auto previousDevice = selectedLogicalDevice_;
    const auto previousNode = selectedLogicalNode_;
    observedIedIndex_ = backend_ != nullptr ? backend_->selectedIedIndex() : -1;

    QVector<DeviceGroup> groups;
    QHash<QString, int> deviceIndex;
    const auto appendScope = [&groups, &deviceIndex](
        const QString& logicalDevice,
        const QString& logicalNode) {
        if (logicalDevice.isEmpty() || logicalNode.isEmpty()) return;
        auto found = deviceIndex.constFind(logicalDevice);
        int index{};
        if (found == deviceIndex.cend()) {
            index = groups.size();
            deviceIndex.insert(logicalDevice, index);
            groups.push_back(DeviceGroup{logicalDevice, {}});
        } else {
            index = found.value();
        }
        auto& nodes = groups[index].logicalNodes;
        if (nodes.isEmpty() || nodes.constLast() != logicalNode) {
            if (!nodes.contains(logicalNode)) nodes.push_back(logicalNode);
        }
    };

    if (backend_ != nullptr) {
        if (backend_->hasPreparedValueIndex()) {
            // Interactive Open SCL has already reduced tens of thousands of
            // points to this tiny ordered LD/LN index on the bounded worker.
            const auto& navigation = backend_->navigationIndexView();
            groups.reserve(navigation.size() > 0 ? 16 : 0);
            for (const auto& value : navigation) {
                const auto item = value.toMap();
                appendScope(
                    item.value(QStringLiteral("logicalDevice")).toString(),
                    item.value(QStringLiteral("logicalNode")).toString());
            }
        } else {
            // Synchronous/legacy automation keeps the compatibility path, but
            // read it by const reference so there is no duplicate full list.
            const auto& values = backend_->valuesView();
            groups.reserve(values.size() > 0 ? 16 : 0);
            for (const auto& value : values) {
                const auto item = value.toMap();
                appendScope(
                    item.value(QStringLiteral("logicalDevice")).toString(),
                    item.value(QStringLiteral("logicalNode")).toString());
            }
        }
    }

    beginResetModel();
    rows_.clear();
    if (!groups.isEmpty()) {
        rows_.push_back(Row{
            QStringLiteral("SECTION"), QStringLiteral("Data Model"), {}, {}, 0, false, true});
        for (const auto& group : groups) {
            const bool expanded = !collapsedDevices_.contains(group.name);
            rows_.push_back(Row{
                QStringLiteral("LD"), group.name, group.name, {}, 1, true, expanded});
            if (!expanded) continue;
            for (const auto& logicalNode : group.logicalNodes) {
                rows_.push_back(Row{
                    QStringLiteral("LN"), logicalNode, group.name, logicalNode, 2, false, true});
            }
        }
    }
    endResetModel();

    if (!containsSelection(selectedLogicalDevice_, selectedLogicalNode_)) {
        selectedLogicalDevice_.clear();
        selectedLogicalNode_.clear();
        for (const auto& row : rows_) {
            if (row.kind != QStringLiteral("LN")) continue;
            selectedLogicalDevice_ = row.logicalDevice;
            selectedLogicalNode_ = row.logicalNode;
            break;
        }
    }

    if (previousDevice != selectedLogicalDevice_ || previousNode != selectedLogicalNode_) {
        emit selectionChanged();
    }
}
