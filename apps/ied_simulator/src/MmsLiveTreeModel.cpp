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

QString logicalNodeDescription(const QString& logicalNodeClass) {
    static const QHash<QString, QString> descriptions{
        {QStringLiteral("LLN0"), QStringLiteral("Logical node zero")},
        {QStringLiteral("LPHD"), QStringLiteral("Physical device information")},
        {QStringLiteral("MMXU"), QStringLiteral("Measurement")},
        {QStringLiteral("MSQI"), QStringLiteral("Sequence and imbalance")},
        {QStringLiteral("MMTR"), QStringLiteral("Metering")},
        {QStringLiteral("PIOC"), QStringLiteral("Instantaneous overcurrent")},
        {QStringLiteral("PTOC"), QStringLiteral("Time overcurrent")},
        {QStringLiteral("XCBR"), QStringLiteral("Circuit breaker")},
        {QStringLiteral("CSWI"), QStringLiteral("Switch controller")},
    };
    const auto normalized = logicalNodeClass.trimmed().toUpper();
    if (const auto found = descriptions.constFind(normalized); found != descriptions.cend()) {
        return found.value();
    }
    return normalized.isEmpty() ? QString{} : QStringLiteral("Logical node %1").arg(normalized);
}
} // namespace

MmsLiveTreeModel::MmsLiveTreeModel(QObject* parent) : QAbstractListModel(parent) {
    detailModel_ = new MmsLiveDetailModel(this);
}

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
    if (!node.reference.isEmpty() &&
        (node.kind == NodeKind::dataObject || node.kind == NodeKind::dataAttribute))
        referenceIndex_[node.reference].push_back(index);
    // The same canonical DA can be addressed as an MMS item or an IEC
    // FCD/FCDA reference. Index both; no duplicate node or second authority.
    if (node.kind == NodeKind::dataAttribute &&
        node.parent >= 0 && node.parent < nodes_.size()) {
        const auto& object = nodes_.at(node.parent);
        QString suffix = node.attributePath.trimmed();
        const QString objectPrefix = object.label + QLatin1Char('.');
        const int prefixStart = suffix.indexOf(objectPrefix);
        if (prefixStart >= 0) suffix = suffix.mid(prefixStart + objectPrefix.size());
        if (suffix.isEmpty()) suffix = node.label;
        if (!object.reference.isEmpty() && !suffix.isEmpty()) {
            const QString iecReference = object.reference + QLatin1Char('.') + suffix;
            if (iecReference != node.reference) referenceIndex_[iecReference].push_back(index);
        }
    }
    nodes_.push_back(std::move(node));
    return index;
}

void MmsLiveTreeModel::applyDocument(const ar::iec61850::mms::MmsLiveModelDocument& document) {
    detailModel_->resetForDocument();
    beginResetModel();
    nodes_.clear();
    visible_.clear();
    mmsIndex_.clear();
    referenceIndex_.clear();
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
        ld.description = QStringLiteral("Logical device");
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
            ln.description = logicalNodeDescription(q(logicalNode.logical_node_class));
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
                object.description = q(dataObject.inferred_cdc);
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
                    da.description = q(attribute.scl_basic_type);
                    da.writable = isWritableAttribute(
                        da.functionalConstraint, da.typeStatus, da.mmsType);
                    (void)appendNode(std::move(da));
                }
            }
        }
    }

    endResetModel();
    detailModel_->setRoot(0);
    rebuildVisible();
    emit countsChanged();
    emit selectionChanged();
}

void MmsLiveTreeModel::clear() {
    detailModel_->resetForDocument();
    beginResetModel();
    nodes_.clear();
    visible_.clear();
    mmsIndex_.clear();
    referenceIndex_.clear();
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
    result.insert(QStringLiteral("description"), node.description);
    QString displayValue = node.value;
    if (displayValue.isEmpty() && node.kind == NodeKind::dataObject) {
        static const QStringList preferredLeaves{
            QStringLiteral("stVal"), QStringLiteral("mag"), QStringLiteral("setVal"),
            QStringLiteral("instMag"), QStringLiteral("general")};
        for (const auto& preferred : preferredLeaves) {
            for (const auto childIndex : node.children) {
                const auto& child = nodes_.at(childIndex);
                if (child.label.compare(preferred, Qt::CaseInsensitive) == 0 &&
                    !child.value.isEmpty()) {
                    displayValue = child.value;
                    break;
                }
            }
            if (!displayValue.isEmpty()) break;
        }
    }
    result.insert(QStringLiteral("value"), displayValue);
    result.insert(QStringLiteral("hasChildren"), !node.children.isEmpty());
    result.insert(QStringLiteral("childCount"), node.children.size());
    result.insert(QStringLiteral("quality"), siblingValue(nodeIndex, QStringLiteral("q")));
    result.insert(QStringLiteral("timestamp"), siblingValue(nodeIndex, QStringLiteral("t")));
    result.insert(QStringLiteral("writable"), node.writable);
    result.insert(QStringLiteral("readable"), node.kind == NodeKind::dataAttribute && !node.item.isEmpty());
    QString objectReference;
    if (node.kind == NodeKind::dataAttribute && node.parent >= 0 && node.parent < nodes_.size()) {
        objectReference = nodes_.at(node.parent).reference;
    }
    result.insert(QStringLiteral("objectReference"), objectReference);
    result.insert(
        QStringLiteral("controlCandidate"),
        node.kind == NodeKind::dataAttribute &&
            node.functionalConstraint.compare(QStringLiteral("CO"), Qt::CaseInsensitive) == 0 &&
            !objectReference.isEmpty());
    return result;
}

QVariantMap MmsLiveTreeModel::selectedNode() const {
    return nodeMap(selectedNode_);
}

int MmsLiveTreeModel::presentationNodeIndex() const noexcept {
    if (selectedNode_ >= 0 && selectedNode_ < nodes_.size()) return selectedNode_;
    return nodes_.isEmpty() ? -1 : 0;
}

QVariantMap MmsLiveTreeModel::presentationNode() const {
    return nodeMap(presentationNodeIndex());
}

int MmsLiveTreeModel::selectedChildCount() const noexcept {
    const auto nodeIndex = presentationNodeIndex();
    return nodeIndex >= 0 ? nodes_.at(nodeIndex).children.size() : 0;
}

bool MmsLiveTreeModel::selectedChildrenTruncated() const noexcept {
    return selectedChildCount() > maximumProjectedChildren_;
}

QVariantList MmsLiveTreeModel::selectedChildren() const {
    QVariantList result;
    const auto nodeIndex = presentationNodeIndex();
    if (nodeIndex < 0) return result;
    const auto& children = nodes_.at(nodeIndex).children;
    const auto count = std::min(children.size(), qsizetype{maximumProjectedChildren_});
    result.reserve(count);
    for (int index = 0; index < count; ++index) result.push_back(nodeMap(children.at(index)));
    return result;
}

bool MmsLiveTreeModel::selectNode(const int nodeIndex) {
    if (nodeIndex < 0 || nodeIndex >= nodes_.size() || nodeIndex == selectedNode_) return false;
    const auto previousRow = selectedRow();
    selectedNode_ = nodeIndex;
    detailModel_->setRoot(nodeIndex);
    for (int cursor = nodes_.at(selectedNode_).parent; cursor >= 0; cursor = nodes_.at(cursor).parent) {
        nodes_[cursor].expanded = true;
    }
    rebuildVisible();
    const auto currentRow = selectedRow();
    if (previousRow >= 0 && previousRow < rowCount())
        emit dataChanged(index(previousRow), index(previousRow), {SelectedRole});
    if (currentRow >= 0)
        emit dataChanged(index(currentRow), index(currentRow), {SelectedRole});
    emit selectionChanged();
    return true;
}

bool MmsLiveTreeModel::selectChild(const int row) {
    const auto nodeIndex = presentationNodeIndex();
    if (nodeIndex < 0 || row < 0 || row >= nodes_.at(nodeIndex).children.size()) return false;
    return selectNode(nodes_.at(nodeIndex).children.at(row));
}

bool MmsLiveTreeModel::selectParent() {
    const auto nodeIndex = presentationNodeIndex();
    if (nodeIndex < 0 || nodes_.at(nodeIndex).parent < 0) return false;
    return selectNode(nodes_.at(nodeIndex).parent);
}

bool MmsLiveTreeModel::selectRoot() {
    return nodes_.isEmpty() ? false : selectNode(0);
}

namespace {
// Dataset descriptors carry an optional human-readable FC suffix. Strip only
// that suffix, never modify IEC object identifiers or infer an unknown FC.
QPair<QString, QString> splitDataSetMemberReference(const QString& raw) {
    const QString reference = raw.trimmed();
    const int split = reference.lastIndexOf(QStringLiteral(" ["));
    if (split < 0 || !reference.endsWith(QLatin1Char(']'))) return {reference, {}};
    const QString fc = reference.mid(split + 2, reference.size() - split - 3);
    if (fc.isEmpty() || fc.size() > 4) return {reference, {}};
    for (const auto character : fc)
        if (!character.isLetterOrNumber()) return {reference, {}};
    return {reference.left(split).trimmed(), fc.toUpper()};
}
} // namespace

QVariantMap MmsLiveTreeModel::nodeForReference(const QString& reference) const {
    const auto [wanted, fc] = splitDataSetMemberReference(reference);
    const auto found = referenceIndex_.constFind(wanted);
    if (wanted.isEmpty() || found == referenceIndex_.cend()) return {};
    for (const auto nodeIndex : found.value()) {
        const auto& node = nodes_.at(nodeIndex);
        if (fc.isEmpty() || node.kind == NodeKind::dataObject ||
            node.functionalConstraint.compare(fc, Qt::CaseInsensitive) == 0)
            return nodeMap(nodeIndex);
    }
    return {};
}

QVariantList MmsLiveTreeModel::dataSetChildrenForReference(
    const QString& reference, const QString& functionalConstraint,
    const int maximumChildren) const {
    QVariantList result;
    if (maximumChildren <= 0) return result;
    const auto [wanted, suffixFc] = splitDataSetMemberReference(reference);
    const QString fc = functionalConstraint.trimmed().isEmpty()
        ? suffixFc : functionalConstraint.trimmed().toUpper();
    const auto found = referenceIndex_.constFind(wanted);
    if (wanted.isEmpty() || found == referenceIndex_.cend()) return result;
    const int limit = std::min(64, maximumChildren);
    for (const int nodeIndex : found.value()) {
        const auto& object = nodes_.at(nodeIndex);
        if (object.kind != NodeKind::dataObject) continue;
        result.reserve(std::min(limit, int(object.children.size())));
        for (const int child : object.children) {
            const auto& attribute = nodes_.at(child);
            if (!fc.isEmpty() &&
                attribute.functionalConstraint.compare(fc, Qt::CaseInsensitive) != 0)
                continue;
            result.push_back(nodeMap(child));
            if (result.size() >= limit) return result;
        }
        break;
    }
    return result;
}

void MmsLiveTreeModel::selectRow(const int row) {
    if (row < 0 || row >= visible_.size()) return;
    const auto next = visible_.at(row);
    if (next == selectedNode_) return;
    const auto previousRow = selectedRow();
    selectedNode_ = next;
    detailModel_->setRoot(next);
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
    detailModel_->setRoot(selectedNode_);
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
    if (maximumTargets <= 0 || references.isEmpty()) return result;
    result.reserve(std::min(maximumTargets, 64));
    QSet<QString> uniqueTargets;

    const auto append = [this, &result, &uniqueTargets, maximumTargets](
                            const int nodeIndex, const QString& fc) {
        if (result.size() >= maximumTargets || nodeIndex < 0 ||
            nodeIndex >= nodes_.size()) return;
        const auto& node = nodes_.at(nodeIndex);
        if (node.kind != NodeKind::dataAttribute ||
            node.domain.isEmpty() || node.item.isEmpty()) return;
        if (!fc.isEmpty() && node.functionalConstraint.compare(fc, Qt::CaseInsensitive) != 0)
            return;
        const QString key = nodeKey(node.domain, node.item);
        if (uniqueTargets.contains(key)) return;
        uniqueTargets.insert(key);
        result.push_back({key, node.domain, node.item});
    };

    // Each user-selected IEC FCDA or MMS member resolves through the canonical
    // hash index. A matching DO is expanded only to its actual FC-matching DA
    // children; no broad prefix sweep, guessed target or unintended network read.
    for (const auto& raw : references) {
        if (result.size() >= maximumTargets) break;
        const auto [reference, fc] = splitDataSetMemberReference(raw);
        if (reference.isEmpty()) continue;
        QString effectiveFc = fc;
        auto found = referenceIndex_.constFind(reference);
        if (found == referenceIndex_.cend()) {
            // Also preserve the standard MMS FCD prefix LN$FC$DO: its
            // canonical structural owner is the same indexed LD/LN.DO.
            // Accept only a complete prefix, never a broad string match.
            const int slash = reference.indexOf(QLatin1Char('/'));
            const int first = slash >= 0
                ? reference.indexOf(QLatin1Char('$'), slash + 1) : -1;
            const int second = first >= 0
                ? reference.indexOf(QLatin1Char('$'), first + 1) : -1;
            if (second <= first) continue;
            const QString prefixFc = reference.mid(first + 1, second - first - 1).toUpper();
            const QString objectName = reference.mid(second + 1);
            if (prefixFc.size() < 2 || prefixFc.size() > 4 ||
                objectName.isEmpty() || objectName.contains(QLatin1Char('$')))
                continue;
            bool validFc = true;
            for (const auto ch : prefixFc)
                if (!ch.isLetterOrNumber()) { validFc = false; break; }
            if (!validFc || (!fc.isEmpty() && fc != prefixFc)) continue;
            effectiveFc = prefixFc;
            const QString iecDo = reference.left(first) + QLatin1Char('.') + objectName;
            found = referenceIndex_.constFind(iecDo);
            if (found == referenceIndex_.cend()) continue;
        }
        for (const int nodeIndex : found.value()) {
            if (result.size() >= maximumTargets) break;
            const auto& node = nodes_.at(nodeIndex);
            if (node.kind == NodeKind::dataAttribute) {
                append(nodeIndex, effectiveFc);
            } else if (node.kind == NodeKind::dataObject) {
                for (const int child : node.children) {
                    append(child, effectiveFc);
                    if (result.size() >= maximumTargets) break;
                }
            }
        }
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
    detailModel_->nodeValueChanged(nodeIndex);
    emit valuesChanged();
    const auto row = visibleRowForNode(nodeIndex);
    if (row >= 0) emit dataChanged(index(row), index(row), {ValueRole});
    if (!filterText_.trimmed().isEmpty()) rebuildVisible();
    if (nodeIndex == selectedNode_ || (selectedNode_ >= 0 && node.parent == nodes_.at(selectedNode_).parent)) {
        emit selectionChanged();
    }
}


// Detail projection contains only visible row indices into the canonical model.
// Expanding a branch is a local UI operation, never an MMS read.
MmsLiveDetailModel::MmsLiveDetailModel(MmsLiveTreeModel* owner)
    : QAbstractListModel(owner), owner_(owner) {}

int MmsLiveDetailModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : rows_.size();
}

QHash<int, QByteArray> MmsLiveDetailModel::roleNames() const {
    return {{LabelRole, "label"}, {KindRole, "kind"}, {ReferenceRole, "reference"},
            {DepthRole, "depth"}, {HasChildrenRole, "hasChildren"},
            {ExpandedRole, "expanded"}, {SelectedRole, "selected"},
            {FunctionalConstraintRole, "functionalConstraint"},
            {DescriptionRole, "description"}, {ValueRole, "value"}};
}

QVariant MmsLiveDetailModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) return {};
    const auto row = rows_.at(index.row());
    const auto& n = owner_->nodes_.at(row.node);
    switch (role) {
    case LabelRole: return n.label;
    case KindRole: return MmsLiveTreeModel::kindName(n.kind);
    case ReferenceRole: return n.reference;
    case DepthRole: return row.depth;
    case HasChildrenRole: return !n.children.isEmpty();
    case ExpandedRole: return expanded_.contains(row.node);
    case SelectedRole: return selected_ == row.node;
    case FunctionalConstraintRole: return n.functionalConstraint;
    case DescriptionRole: return n.description;
    case ValueRole: {
        if (!n.value.isEmpty()) return n.value;
        if (n.kind != MmsLiveTreeModel::NodeKind::dataObject) return QString{};
        static const QStringList preferred{QStringLiteral("stVal"), QStringLiteral("mag"),
                                           QStringLiteral("setVal"), QStringLiteral("instMag"),
                                           QStringLiteral("general")};
        for (const auto& label : preferred)
            for (const auto child : n.children) {
                const auto& leaf = owner_->nodes_.at(child);
                if (leaf.label.compare(label, Qt::CaseInsensitive) == 0 &&
                    !leaf.value.isEmpty()) return leaf.value;
            }
        return QString{};
    }
    default: return {};
    }
}

QString MmsLiveDetailModel::rootLabel() const {
    return root_ >= 0 && root_ < owner_->nodes_.size() ? owner_->nodes_.at(root_).label : QString{};
}
QString MmsLiveDetailModel::rootKind() const {
    return root_ >= 0 && root_ < owner_->nodes_.size()
        ? MmsLiveTreeModel::kindName(owner_->nodes_.at(root_).kind) : QString{};
}
int MmsLiveDetailModel::rootChildCount() const {
    return root_ >= 0 && root_ < owner_->nodes_.size()
        ? owner_->nodes_.at(root_).children.size() : 0;
}
void MmsLiveDetailModel::append(int node, int depth) {
    if (rows_.size() >= maximumRows_) { truncated_ = true; return; }
    rows_.push_back({node, depth});
    if (!expanded_.contains(node)) return;
    for (const auto child : owner_->nodes_.at(node).children) {
        append(child, depth + 1);
        if (truncated_) return;
    }
}
void MmsLiveDetailModel::rebuild() {
    beginResetModel();
    rows_.clear();
    truncated_ = false;
    if (root_ >= 0 && root_ < owner_->nodes_.size())
        for (const auto child : owner_->nodes_.at(root_).children) {
            append(child, 0);
            if (truncated_) break;
        }
    endResetModel();
    emit projectionChanged();
}
void MmsLiveDetailModel::setRoot(int node) {
    const int valid = node >= 0 && node < owner_->nodes_.size() ? node : -1;
    if (root_ == valid) return;
    root_ = valid;
    selected_ = -1;
    rebuild();
    emit rootChanged();
}
void MmsLiveDetailModel::resetForDocument() {
    expanded_.clear();
    root_ = -1;
    selected_ = -1;
    rebuild();
    emit rootChanged();
}
int MmsLiveDetailModel::rowOf(int node) const {
    for (int row = 0; row < rows_.size(); ++row)
        if (rows_.at(row).node == node) return row;
    return -1;
}
void MmsLiveDetailModel::toggle(int row) {
    if (row < 0 || row >= rows_.size()) return;
    const int node = rows_.at(row).node;
    const int depth = rows_.at(row).depth;
    const auto& children = owner_->nodes_.at(node).children;
    if (children.isEmpty()) return;

    // An earlier bounded projection might have withheld later siblings.
    // Rebuild in that rare case to restore complete ordering after collapse.
    if (truncated_) {
        if (expanded_.contains(node)) expanded_.remove(node);
        else expanded_.insert(node);
        rebuild();
        return;
    }

    if (expanded_.contains(node)) {
        int last = row + 1;
        while (last < rows_.size() && rows_.at(last).depth > depth) ++last;
        // A selected descendant must not silently disappear from view.
        const int selectedRow = rowOf(selected_);
        if (selectedRow > row && selectedRow < last) select(row);
        expanded_.remove(node);
        if (last > row + 1) {
            beginRemoveRows({}, row + 1, last - 1);
            rows_.remove(row + 1, last - row - 1);
            endRemoveRows();
        }
    } else {
        expanded_.insert(node);
        QVector<Row> added;
        const int capacity = maximumRows_ - int(rows_.size());
        added.reserve(std::min(capacity, int(children.size())));
        bool clipped = false;
        const auto expand = [&](const auto& self, int child, int level) -> void {
            if (added.size() >= capacity) { clipped = true; return; }
            added.push_back({child, level});
            if (expanded_.contains(child))
                for (const auto descendant : owner_->nodes_.at(child).children) {
                    self(self, descendant, level + 1);
                    if (clipped) return;
                }
        };
        for (const auto child : children) {
            expand(expand, child, depth + 1);
            if (clipped) break;
        }
        if (!added.isEmpty()) {
            const int offset = row + 1;
            const int oldSize = rows_.size();
            beginInsertRows({}, offset, offset + added.size() - 1);
            rows_.resize(oldSize + added.size());
            for (int pos = oldSize - 1; pos >= offset; --pos)
                rows_[pos + added.size()] = rows_.at(pos);
            for (int pos = 0; pos < added.size(); ++pos)
                rows_[offset + pos] = added.at(pos);
            endInsertRows();
        }
        if (clipped) {
            truncated_ = true;
            emit projectionChanged();
        }
    }
    emit dataChanged(index(row), index(row), {ExpandedRole});
}
void MmsLiveDetailModel::select(int row) {
    if (row < 0 || row >= rows_.size()) return;
    const int node = rows_.at(row).node;
    if (selected_ == node && owner_->selectedNode_ == node) return;
    const int oldDetail = rowOf(selected_);
    const int oldTree = owner_->selectedRow();
    selected_ = node;
    owner_->selectedNode_ = node; // keep panel root stable; read/control target canonical node
    const int newTree = owner_->selectedRow();
    if (oldDetail >= 0) emit dataChanged(index(oldDetail), index(oldDetail), {SelectedRole});
    emit dataChanged(index(row), index(row), {SelectedRole});
    if (oldTree >= 0)
        emit owner_->dataChanged(owner_->index(oldTree), owner_->index(oldTree),
                                 {MmsLiveTreeModel::SelectedRole});
    if (newTree >= 0)
        emit owner_->dataChanged(owner_->index(newTree), owner_->index(newTree),
                                 {MmsLiveTreeModel::SelectedRole});
    emit owner_->selectionChanged();
}
void MmsLiveDetailModel::drillInto(int row) {
    if (row < 0 || row >= rows_.size()) return;
    select(row);
    const int node = rows_.at(row).node;
    if (!owner_->nodes_.at(node).children.isEmpty()) toggle(row);
}
void MmsLiveDetailModel::selectParent() {
    if (root_ < 0 || root_ >= owner_->nodes_.size()) return;
    const int parent = owner_->nodes_.at(root_).parent;
    if (parent >= 0) owner_->selectNode(parent);
}
void MmsLiveDetailModel::nodeValueChanged(int node) {
    const int row = rowOf(node);
    if (row >= 0) emit dataChanged(index(row), index(row), {ValueRole});
    const int parent = owner_->nodes_.at(node).parent;
    const int parentRow = rowOf(parent);
    if (parentRow >= 0) emit dataChanged(index(parentRow), index(parentRow), {ValueRole});
}
