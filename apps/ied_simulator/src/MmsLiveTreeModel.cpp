// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsLiveTreeModel.hpp"

#include <QSet>

#include <algorithm>

namespace {
QString q(const std::string& value) { return QString::fromStdString(value); }

QString leafName(const QString& path) {
    const auto dot = path.lastIndexOf(QLatin1Char('.'));
    const auto dollar = path.lastIndexOf(QLatin1Char('$'));
    const auto split = std::max(dot, dollar);
    return split >= 0 ? path.mid(split + 1) : path;
}
} // namespace

MmsLiveTreeModel::MmsLiveTreeModel(QObject* parent) : QAbstractListModel(parent) {}

int MmsLiveTreeModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : visible_.size();
}

QVariant MmsLiveTreeModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= visible_.size()) return {};
    const auto nodeIndex = visible_.at(index.row());
    const auto& node = nodes_.at(nodeIndex);
    switch (role) {
    case LabelRole: return node.label;
    case KindRole: return kindName(node.kind);
    case ReferenceRole: return node.reference;
    case DepthRole: return node.depth;
    case HasChildrenRole: return !node.children.isEmpty();
    case ExpandedRole: return node.expanded;
    case SelectedRole: return nodeIndex == selectedNode_;
    case FunctionalConstraintRole: return node.functionalConstraint;
    case MmsTypeRole: return node.mmsType;
    case SclTypeRole: return node.sclType;
    case TypeStatusRole: return node.typeStatus;
    case ValueRole: return node.value;
    case WritableRole: return node.writable;
    case MmsDomainRole: return node.domain;
    case MmsItemRole: return node.item;
    default: return {};
    }
}

QHash<int, QByteArray> MmsLiveTreeModel::roleNames() const {
    return {
        {LabelRole, "label"}, {KindRole, "kind"}, {ReferenceRole, "reference"},
        {DepthRole, "depth"}, {HasChildrenRole, "hasChildren"}, {ExpandedRole, "expanded"},
        {SelectedRole, "selected"}, {FunctionalConstraintRole, "functionalConstraint"},
        {MmsTypeRole, "mmsType"}, {SclTypeRole, "sclType"}, {TypeStatusRole, "typeStatus"},
        {ValueRole, "value"}, {WritableRole, "writable"}, {MmsDomainRole, "mmsDomain"},
        {MmsItemRole, "mmsItem"},
    };
}

QString MmsLiveTreeModel::kindName(const NodeKind kind) {
    switch (kind) {
    case NodeKind::ied: return QStringLiteral("IED");
    case NodeKind::logicalDevice: return QStringLiteral("LD");
    case NodeKind::logicalNode: return QStringLiteral("LN");
    case NodeKind::dataObject: return QStringLiteral("DO");
    case NodeKind::dataAttribute: return QStringLiteral("DA");
    }
    return {};
}

QString MmsLiveTreeModel::nodeKey(const QString& domain, const QString& item) {
    return domain + QLatin1Char('\x1f') + item;
}

bool MmsLiveTreeModel::isWritableAttribute(
    const QString& functionalConstraint,
    const QString& typeStatus,
    const QString& mmsType) {
    static const QSet<QString> writableFc{
        QStringLiteral("SP"), QStringLiteral("CF"), QStringLiteral("DC"), QStringLiteral("SE")};
    static const QSet<QString> scalarTypes{
        QStringLiteral("boolean"), QStringLiteral("integer"), QStringLiteral("unsigned"),
        QStringLiteral("floating-point"), QStringLiteral("visible-string"), QStringLiteral("mms-string")};
    return typeStatus == QStringLiteral("Exact") &&
           writableFc.contains(functionalConstraint.toUpper()) &&
           scalarTypes.contains(mmsType.toLower());
}

int MmsLiveTreeModel::appendNode(Node node) {
    const auto index = nodes_.size();
    if (node.parent >= 0 && node.parent < nodes_.size()) nodes_[node.parent].children.push_back(index);
    if (!node.domain.isEmpty() && !node.item.isEmpty()) mmsIndex_.insert(nodeKey(node.domain, node.item), index);
    nodes_.push_back(std::move(node));
    return index;
}

void MmsLiveTreeModel::applyDocument(const ar::iec61850::mms::MmsLiveModelDocument& document) {
    beginResetModel();
    nodes_.clear();
    visible_.clear();
    mmsIndex_.clear();
    selectedNode_ = -1;

    Node root;
    root.kind = NodeKind::ied;
    root.depth = 0;
    root.expanded = true;
    root.label = q(document.identity.ied_name.empty() ? document.endpoint.host : document.identity.ied_name);
    root.reference = root.label;
    const auto rootIndex = appendNode(std::move(root));

    for (const auto& logicalDevice : document.logical_devices) {
        Node ld;
        ld.kind = NodeKind::logicalDevice;
        ld.parent = rootIndex;
        ld.depth = 1;
        ld.expanded = true;
        ld.label = q(logicalDevice.instance.empty() ? logicalDevice.mms_domain : logicalDevice.instance);
        ld.reference = q(logicalDevice.mms_domain);
        ld.domain = q(logicalDevice.mms_domain);
        const auto ldIndex = appendNode(std::move(ld));

        for (const auto& logicalNode : logicalDevice.logical_nodes) {
            Node ln;
            ln.kind = NodeKind::logicalNode;
            ln.parent = ldIndex;
            ln.depth = 2;
            ln.expanded = false;
            ln.label = q(logicalNode.name);
            ln.reference = q(logicalDevice.mms_domain + "/" + logicalNode.name);
            ln.domain = q(logicalDevice.mms_domain);
            const auto lnIndex = appendNode(std::move(ln));

            for (const auto& dataObject : logicalNode.data_objects) {
                Node object;
                object.kind = NodeKind::dataObject;
                object.parent = lnIndex;
                object.depth = 3;
                object.expanded = false;
                object.label = q(dataObject.name);
                object.reference = q(dataObject.reference);
                object.domain = q(logicalDevice.mms_domain);
                const auto objectIndex = appendNode(std::move(object));

                for (const auto& attribute : dataObject.attributes) {
                    Node da;
                    da.kind = NodeKind::dataAttribute;
                    da.parent = objectIndex;
                    da.depth = 4;
                    da.label = q(attribute.attribute_path.empty()
                                     ? attribute.mms_item_name
                                     : attribute.attribute_path);
                    da.label = leafName(da.label);
                    da.reference = q(attribute.mms_reference.empty()
                                         ? attribute.object_reference
                                         : attribute.mms_reference);
                    da.domain = q(logicalDevice.mms_domain);
                    da.item = q(attribute.mms_item_name);
                    da.attributePath = q(attribute.attribute_path);
                    da.functionalConstraint = q(attribute.functional_constraint);
                    da.mmsType = q(attribute.mms_type);
                    da.sclType = q(attribute.scl_basic_type);
                    da.typeStatus = q(attribute.type_discovery_status);
                    da.writable = isWritableAttribute(
                        da.functionalConstraint, da.typeStatus, da.mmsType);
                    (void)appendNode(std::move(da));
                }
            }
        }
    }

    endResetModel();
    rebuildVisible();
    emit countsChanged();
    emit selectionChanged();
}

void MmsLiveTreeModel::clear() {
    beginResetModel();
    nodes_.clear();
    visible_.clear();
    mmsIndex_.clear();
    selectedNode_ = -1;
    endResetModel();
    emit countsChanged();
    emit selectionChanged();
}

bool MmsLiveTreeModel::matchesFilter(const Node& node, const QString& needle) const {
    if (needle.isEmpty()) return true;
    const auto contains = [&needle](const QString& value) {
        return value.contains(needle, Qt::CaseInsensitive);
    };
    return contains(node.label) || contains(node.reference) || contains(node.domain) ||
           contains(node.item) || contains(node.functionalConstraint) || contains(node.mmsType) ||
           contains(node.sclType) || contains(node.value);
}

void MmsLiveTreeModel::appendVisibleSubtree(const int nodeIndex, const QVector<bool>* included) {
    if (nodeIndex < 0 || nodeIndex >= nodes_.size()) return;
    if (included != nullptr && !included->at(nodeIndex)) return;
    visible_.push_back(nodeIndex);
    const auto& node = nodes_.at(nodeIndex);
    const bool traverseChildren = included != nullptr || node.expanded;
    if (!traverseChildren) return;
    for (const auto child : node.children) appendVisibleSubtree(child, included);
}

void MmsLiveTreeModel::rebuildVisible() {
    beginResetModel();
    visible_.clear();
    if (!nodes_.isEmpty()) {
        if (filterText_.trimmed().isEmpty()) {
            appendVisibleSubtree(0, nullptr);
        } else {
            QVector<bool> included(nodes_.size(), false);
            const auto needle = filterText_.trimmed();
            for (int index = 0; index < nodes_.size(); ++index) {
                if (!matchesFilter(nodes_.at(index), needle)) continue;
                for (int cursor = index; cursor >= 0 && !included.at(cursor); cursor = nodes_.at(cursor).parent) {
                    included[cursor] = true;
                }
            }
            appendVisibleSubtree(0, &included);
        }
    }
    endResetModel();
    emit countsChanged();
    emit selectionChanged();
}

void MmsLiveTreeModel::setFilterText(const QString& value) {
    if (filterText_ == value) return;
    filterText_ = value;
    emit filterTextChanged();
    rebuildVisible();
}

int MmsLiveTreeModel::visibleRowForNode(const int nodeIndex) const noexcept {
    for (int row = 0; row < visible_.size(); ++row) {
        if (visible_.at(row) == nodeIndex) return row;
    }
    return -1;
}

int MmsLiveTreeModel::selectedRow() const noexcept { return visibleRowForNode(selectedNode_); }

QString MmsLiveTreeModel::siblingValue(const int nodeIndex, const QString& wantedLeaf) const {
    if (nodeIndex < 0 || nodeIndex >= nodes_.size()) return {};
    const auto& selected = nodes_.at(nodeIndex);
    if (selected.kind != NodeKind::dataAttribute) return {};
    if (leafName(selected.attributePath).compare(wantedLeaf, Qt::CaseInsensitive) == 0 ||
        selected.label.compare(wantedLeaf, Qt::CaseInsensitive) == 0) {
        return selected.value;
    }
    if (selected.parent < 0) return {};
    for (const auto siblingIndex : nodes_.at(selected.parent).children) {
        const auto& sibling = nodes_.at(siblingIndex);
        if (leafName(sibling.attributePath).compare(wantedLeaf, Qt::CaseInsensitive) == 0 ||
            sibling.label.compare(wantedLeaf, Qt::CaseInsensitive) == 0) {
            return sibling.value;
        }
    }
    return {};
}

QVariantMap MmsLiveTreeModel::nodeMap(const int nodeIndex) const {
    QVariantMap result;
    if (nodeIndex < 0 || nodeIndex >= nodes_.size()) return result;
    const auto& node = nodes_.at(nodeIndex);
    result.insert(QStringLiteral("kind"), kindName(node.kind));
    result.insert(QStringLiteral("label"), node.label);
    result.insert(QStringLiteral("reference"), node.reference);
    result.insert(QStringLiteral("mmsDomain"), node.domain);
    result.insert(QStringLiteral("mmsItem"), node.item);
    result.insert(QStringLiteral("functionalConstraint"), node.functionalConstraint);
    result.insert(QStringLiteral("mmsType"), node.mmsType);
    result.insert(QStringLiteral("sclType"), node.sclType);
    result.insert(QStringLiteral("typeStatus"), node.typeStatus);
    result.insert(QStringLiteral("value"), node.value);
    result.insert(QStringLiteral("quality"), siblingValue(nodeIndex, QStringLiteral("q")));
    result.insert(QStringLiteral("timestamp"), siblingValue(nodeIndex, QStringLiteral("t")));
    result.insert(QStringLiteral("writable"), node.writable);
    result.insert(QStringLiteral("readable"), node.kind == NodeKind::dataAttribute && !node.item.isEmpty());
    return result;
}

QVariantMap MmsLiveTreeModel::selectedNode() const {
    return nodeMap(selectedNode_);
}

QVariantMap MmsLiveTreeModel::nodeForReference(const QString& reference) const {
    const auto wanted = reference.trimmed();
    if (wanted.isEmpty()) return {};
    for (int index = 0; index < nodes_.size(); ++index) {
        const auto& node = nodes_.at(index);
        if (node.kind != NodeKind::dataAttribute) continue;
        if (node.reference == wanted) return nodeMap(index);
    }
    return {};
}

void MmsLiveTreeModel::selectRow(const int row) {
    if (row < 0 || row >= visible_.size()) return;
    const auto next = visible_.at(row);
    if (next == selectedNode_) return;
    const auto previousRow = selectedRow();
    selectedNode_ = next;
    const auto currentRow = selectedRow();
    if (previousRow >= 0) emit dataChanged(index(previousRow), index(previousRow), {SelectedRole});
    if (currentRow >= 0) emit dataChanged(index(currentRow), index(currentRow), {SelectedRole});
    emit selectionChanged();
}

void MmsLiveTreeModel::toggle(const int row) {
    if (row < 0 || row >= visible_.size()) return;
    const auto nodeIndex = visible_.at(row);
    auto& node = nodes_[nodeIndex];
    if (node.children.isEmpty() || !filterText_.trimmed().isEmpty()) return;
    node.expanded = !node.expanded;
    rebuildVisible();
}

bool MmsLiveTreeModel::selectMmsItem(const QString& domain, const QString& item) {
    const auto found = mmsIndex_.constFind(nodeKey(domain, item));
    if (found == mmsIndex_.cend()) return false;
    selectedNode_ = found.value();
    for (int cursor = nodes_.at(selectedNode_).parent; cursor >= 0; cursor = nodes_.at(cursor).parent) {
        nodes_[cursor].expanded = true;
    }
    rebuildVisible();
    emit selectionChanged();
    return true;
}

std::optional<MmsLiveTreeModel::Selection> MmsLiveTreeModel::selectionSnapshot() const {
    if (selectedNode_ < 0 || selectedNode_ >= nodes_.size()) return std::nullopt;
    const auto& node = nodes_.at(selectedNode_);
    if (node.kind != NodeKind::dataAttribute || node.domain.isEmpty() || node.item.isEmpty()) {
        return std::nullopt;
    }
    Selection result;
    result.key = nodeKey(node.domain, node.item);
    result.domain = node.domain;
    result.item = node.item;
    result.reference = node.reference;
    result.functionalConstraint = node.functionalConstraint;
    result.mmsType = node.mmsType;
    result.sclType = node.sclType;
    result.writable = node.writable;
    return result;
}

QVector<MmsLiveTreeModel::ReadTarget> MmsLiveTreeModel::readTargetsForNode(const int nodeIndex) const {
    QVector<ReadTarget> result;
    if (nodeIndex < 0 || nodeIndex >= nodes_.size()) return result;
    const auto& selected = nodes_.at(nodeIndex);
    if (selected.kind != NodeKind::dataAttribute || selected.domain.isEmpty() || selected.item.isEmpty()) return result;

    const auto append = [&result](const Node& node) {
        const auto key = nodeKey(node.domain, node.item);
        if (std::none_of(result.cbegin(), result.cend(), [&key](const ReadTarget& target) { return target.key == key; })) {
            result.push_back({key, node.domain, node.item});
        }
    };
    append(selected);
    if (selected.parent >= 0) {
        for (const auto siblingIndex : nodes_.at(selected.parent).children) {
            const auto& sibling = nodes_.at(siblingIndex);
            const auto name = leafName(sibling.attributePath).isEmpty() ? sibling.label : leafName(sibling.attributePath);
            if (name.compare(QStringLiteral("q"), Qt::CaseInsensitive) == 0 ||
                name.compare(QStringLiteral("t"), Qt::CaseInsensitive) == 0) {
                append(sibling);
            }
        }
    }
    return result;
}

QVector<MmsLiveTreeModel::ReadTarget> MmsLiveTreeModel::selectedReadTargets() const {
    return readTargetsForNode(selectedNode_);
}

QVector<MmsLiveTreeModel::ReadTarget> MmsLiveTreeModel::readTargetsForVisibleRange(
    int firstRow, int lastRow, const int maximumTargets) const {
    QVector<ReadTarget> result;
    if (visible_.isEmpty() || maximumTargets <= 0) return result;
    const int lastVisibleRow = static_cast<int>(visible_.size() - 1);
    firstRow = std::clamp(firstRow, 0, lastVisibleRow);
    lastRow = std::clamp(lastRow, firstRow, lastVisibleRow);
    QSet<QString> seen;
    for (int row = firstRow; row <= lastRow && result.size() < maximumTargets; ++row) {
        const auto& node = nodes_.at(visible_.at(row));
        if (node.kind != NodeKind::dataAttribute || node.domain.isEmpty() || node.item.isEmpty()) continue;
        const auto key = nodeKey(node.domain, node.item);
        if (seen.contains(key)) continue;
        seen.insert(key);
        result.push_back({key, node.domain, node.item});
    }
    return result;
}

QVector<MmsLiveTreeModel::ReadTarget> MmsLiveTreeModel::readTargetsForReferences(
    const QStringList& references, const int maximumTargets) const {
    QVector<ReadTarget> result;
    if (references.isEmpty() || maximumTargets <= 0) return result;

    QStringList wanted;
    QSet<QString> seenReferences;
    for (const auto& raw : references) {
        const auto reference = raw.trimmed();
        if (reference.isEmpty() || seenReferences.contains(reference)) continue;
        seenReferences.insert(reference);
        wanted.push_back(reference);
    }

    QSet<QString> seenTargets;
    const auto matchesRequestedReference = [&wanted](const QString& nodeReference) {
        for (const auto& reference : wanted) {
            if (nodeReference == reference) return true;
            if (nodeReference.startsWith(reference + QLatin1Char('.'))) return true;
            if (nodeReference.startsWith(reference + QLatin1Char('$'))) return true;
        }
        return false;
    };

    for (const auto& node : nodes_) {
        if (result.size() >= maximumTargets) break;
        if (node.kind != NodeKind::dataAttribute || node.domain.isEmpty() || node.item.isEmpty()) continue;
        if (!matchesRequestedReference(node.reference)) continue;
        const auto key = nodeKey(node.domain, node.item);
        if (seenTargets.contains(key)) continue;
        seenTargets.insert(key);
        result.push_back({key, node.domain, node.item});
    }
    return result;
}

void MmsLiveTreeModel::applyReadValue(const QString& key, const QString& displayValue) {
    const auto found = mmsIndex_.constFind(key);
    if (found == mmsIndex_.cend()) return;
    const auto nodeIndex = found.value();
    auto& node = nodes_[nodeIndex];
    if (node.value == displayValue) return;
    node.value = displayValue;
    const auto row = visibleRowForNode(nodeIndex);
    if (row >= 0) emit dataChanged(index(row), index(row), {ValueRole});
    if (!filterText_.trimmed().isEmpty()) rebuildVisible();
    if (nodeIndex == selectedNode_ || (selectedNode_ >= 0 && node.parent == nodes_.at(selectedNode_).parent)) {
        emit selectionChanged();
    }
}
