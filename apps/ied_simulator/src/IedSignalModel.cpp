// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedSignalModel.hpp"

#include <QHash>
#include <QVector>

#include <algorithm>

namespace {
struct ObjectGroup final {
    QString name;
    QVector<int> members;
    int preferred{-1};
    bool objectMatches{};
};

QString referenceAt(const IedFleetController* backend, const int index) {
    if (backend == nullptr) return {};
    const auto* point = backend->valueRecord(index);
    return point == nullptr ? QString{} : point->reference;
}
} // namespace

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
    observedSelectedSourceIndex_ = -1;
    observedSourceCount_ = 0;

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

IedSignalModel::Row IedSignalModel::makeRow(
    const RowKind kind,
    const int sourceIndex,
    const QString& name,
    const IedPointStore::PointRecord& item) const {
    const bool objectRow = kind == RowKind::dataObject;
    Row row;
    row.kind = kind;
    row.sourceIndex = sourceIndex;
    row.name = name;
    row.functionalConstraint = objectRow ? QString{} : item.functionalConstraint;
    row.type = objectRow ? item.cdc : item.type;
    row.reference = item.reference;
    row.value = item.value;
    row.quality = item.quality;
    row.writable = item.writable;
    row.changed = item.changed;
    return row;
}

QVariant IedSignalModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];
    const bool objectRow = row.kind == RowKind::dataObject;

    switch (role) {
    case KindRole: return objectRow ? QStringLiteral("DO") : QStringLiteral("DA");
    case NameRole: return row.name;
    case DepthRole: return objectRow ? 0 : 1;
    case ValueRole: return row.value;
    case FunctionalConstraintRole:
        return objectRow ? QVariant{} : QVariant{row.functionalConstraint};
    case TypeRole: return row.type;
    case QualityRole: return row.quality;
    case SourceIndexRole: return row.sourceIndex;
    case WritableRole: return row.writable;
    case ChangedRole: return row.changed;
    case ReferenceRole: return row.reference;
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
    // Latest-state presentation accumulator: any number of valuesChanged bursts
    // inside one frame collapse into one scan of only the currently projected
    // source rows. Protocol/runtime state remains ordered and authoritative.
    ++refreshRequestCount_;
    if (!rebuildTimer_.isActive() && !refreshTimer_.isActive()) {
        refreshLatencyTimer_.restart();
        refreshTimer_.start();
    }
}

void IedSignalModel::resetPerformanceCounters() {
    refreshRequestCount_ = 0;
    refreshFlushCount_ = 0;
    lastRefreshLatencyMilliseconds_ = 0;
    maxRefreshLatencyMilliseconds_ = 0;
    lastRebuildMilliseconds_ = 0;
    maxRebuildMilliseconds_ = 0;
    refreshLatencyTimer_.invalidate();
    emit performanceCountersChanged();
}

bool IedSignalModel::rowMatches(
    const IedPointStore::PointRecord& item,
    const QString& query) const {
    if (query.isEmpty()) return true;
    const QStringList fields{
        item.dataObject,
        item.dataAttribute,
        item.value,
        item.functionalConstraint,
        item.type,
        item.reference,
    };
    for (const auto& field : fields) {
        if (field.contains(query, Qt::CaseInsensitive)) return true;
    }
    return false;
}

void IedSignalModel::rebuild() {
    QElapsedTimer rebuildElapsed;
    rebuildElapsed.start();
    const int previousCount = rowCount();
    observedIedIndex_ = backend_ != nullptr ? backend_->selectedIedIndex() : -1;
    observedSelectedSourceIndex_ = backend_ != nullptr ? backend_->selectedValueIndex() : -1;
    observedSourceCount_ = backend_ != nullptr ? backend_->valueCount() : 0;
    observedFirstReference_ = referenceAt(backend_, 0);
    observedLastReference_ = referenceAt(backend_, observedSourceCount_ - 1);

    QVector<ObjectGroup> groups;
    QHash<QString, int> groupIndex;
    const auto query = filterText_.trimmed();

    const auto consumeSource = [&](const int sourceIndex) {
        if (backend_ == nullptr || sourceIndex < 0 || sourceIndex >= backend_->valueCount()) return;
        const auto* item = backend_->valueRecord(sourceIndex);
        if (item == nullptr || item->logicalDevice != logicalDevice_ ||
            item->logicalNode != logicalNode_) {
            return;
        }

        const auto& objectName = item->dataObject;
        if (objectName.isEmpty()) return;
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
        const auto& attribute = item->dataAttribute;
        if (group.preferred < 0 || attribute == QStringLiteral("stVal") ||
            attribute.endsWith(QStringLiteral(".stVal")) ||
            attribute == QStringLiteral("mag.f")) {
            group.preferred = sourceIndex;
        }
    };

    if (backend_ != nullptr && backend_->hasPreparedValueIndex()) {
        // Worker-side indexing makes the hot rebuild proportional to the
        // selected LN, not to the complete 20k/50k IED point catalog.
        const auto& scopeIndices = backend_->valueScopeIndices(logicalDevice_, logicalNode_);
        groups.reserve(scopeIndices.size() > 0 ? 16 : 0);
        for (const auto sourceIndex : scopeIndices) consumeSource(sourceIndex);
    } else if (backend_ != nullptr) {
        // Legacy synchronous/multi-IED selection fallback still reads the typed
        // canonical store directly; it never materializes a full QVariant list.
        for (int sourceIndex = 0; sourceIndex < backend_->valueCount(); ++sourceIndex) {
            consumeSource(sourceIndex);
        }
    }

    std::vector<Row> nextRows;
    std::size_t scopedMemberCount{};
    for (const auto& group : groups) scopedMemberCount += static_cast<std::size_t>(group.members.size());
    nextRows.reserve(scopedMemberCount + static_cast<std::size_t>(groups.size()));
    for (const auto& group : groups) {
        QVector<int> visibleMembers;
        visibleMembers.reserve(group.members.size());
        if (query.isEmpty() || group.objectMatches) {
            visibleMembers = group.members;
        } else {
            for (const auto sourceIndex : group.members) {
                const auto* item = backend_ != nullptr ? backend_->valueRecord(sourceIndex) : nullptr;
                if (item != nullptr && rowMatches(*item, query)) visibleMembers.push_back(sourceIndex);
            }
        }
        if (visibleMembers.isEmpty()) continue;

        int preferred = group.preferred;
        if (preferred < 0 || !group.members.contains(preferred)) preferred = visibleMembers.constFirst();
        const auto* preferredItem = backend_ != nullptr ? backend_->valueRecord(preferred) : nullptr;
        if (preferredItem == nullptr) continue;
        nextRows.push_back(makeRow(RowKind::dataObject, preferred, group.name, *preferredItem));
        for (const auto sourceIndex : visibleMembers) {
            const auto* item = backend_ != nullptr ? backend_->valueRecord(sourceIndex) : nullptr;
            if (item == nullptr) continue;
            auto attribute = item->dataAttribute;
            if (attribute.isEmpty()) attribute = group.name;
            nextRows.push_back(makeRow(RowKind::dataAttribute, sourceIndex, attribute, *item));
        }
    }

    beginResetModel();
    rows_ = std::move(nextRows);
    sourceRows_.clear();
    for (int row = 0; row < rowCount(); ++row) {
        sourceRows_[rows_[static_cast<std::size_t>(row)].sourceIndex].push_back(row);
    }
    endResetModel();
    if (previousCount != rowCount()) emit visibleRowCountChanged();
    lastRebuildMilliseconds_ = rebuildElapsed.elapsed();
    maxRebuildMilliseconds_ = std::max(maxRebuildMilliseconds_, lastRebuildMilliseconds_);
    emit performanceCountersChanged();
}

void IedSignalModel::refreshSnapshot() {
    ++refreshFlushCount_;
    if (refreshLatencyTimer_.isValid()) {
        lastRefreshLatencyMilliseconds_ = refreshLatencyTimer_.elapsed();
        maxRefreshLatencyMilliseconds_ =
            std::max(maxRefreshLatencyMilliseconds_, lastRefreshLatencyMilliseconds_);
        refreshLatencyTimer_.invalidate();
    }
    emit performanceCountersChanged();

    if (backend_ == nullptr) {
        scheduleRebuild();
        return;
    }

    const int nextCount = backend_->valueCount();
    const bool sameStructure = backend_->selectedIedIndex() == observedIedIndex_ &&
        nextCount == observedSourceCount_ &&
        referenceAt(backend_, 0) == observedFirstReference_ &&
        referenceAt(backend_, nextCount - 1) == observedLastReference_;

    if (!sameStructure) {
        scheduleRebuild();
        return;
    }

    // A live value can be part of the active search predicate. Rebuild at the
    // existing 80 ms coalescing boundary so rows enter/leave the filtered view
    // correctly instead of publishing stale membership.
    if (!filterText_.trimmed().isEmpty()) {
        scheduleRebuild();
        return;
    }

    QVector<int> changedRows;
    for (auto it = sourceRows_.cbegin(); it != sourceRows_.cend(); ++it) {
        const int sourceIndex = it.key();
        const auto* after = backend_->valueRecord(sourceIndex);
        if (after == nullptr) {
            scheduleRebuild();
            return;
        }

        for (const int rowIndex : it.value()) {
            if (rowIndex < 0 || rowIndex >= rowCount()) {
                scheduleRebuild();
                return;
            }
            auto& row = rows_[static_cast<std::size_t>(rowIndex)];
            if (row.reference != after->reference) {
                scheduleRebuild();
                return;
            }
            if (row.value == after->value && row.quality == after->quality &&
                row.writable == after->writable && row.changed == after->changed) {
                continue;
            }
            row.value = after->value;
            row.quality = after->quality;
            row.writable = after->writable;
            row.changed = after->changed;
            changedRows.push_back(rowIndex);
        }
    }

    emitRowsChanged(
        std::move(changedRows),
        {ValueRole, QualityRole, WritableRole, ChangedRole});
}

void IedSignalModel::refreshSelectedRole() {
    const int nextSelected = backend_ != nullptr ? backend_->selectedValueIndex() : -1;
    if (nextSelected == observedSelectedSourceIndex_) return;

    QVector<int> changedRows = sourceRows_.value(observedSelectedSourceIndex_);
    changedRows += sourceRows_.value(nextSelected);
    observedSelectedSourceIndex_ = nextSelected;
    emitRowsChanged(std::move(changedRows), {SelectedRole});
}

void IedSignalModel::emitRowsChanged(QVector<int> rows, const QList<int>& roles) {
    if (rows.isEmpty() || rowCount() <= 0) return;
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());

    int rangeStart = -1;
    int previous = -2;
    const auto flush = [this, &roles](const int first, const int last) {
        if (first < 0 || last < first || last >= rowCount()) return;
        emit dataChanged(index(first, 0), index(last, 0), roles);
    };

    for (const int row : rows) {
        if (row < 0 || row >= rowCount()) continue;
        if (rangeStart < 0) {
            rangeStart = row;
            previous = row;
            continue;
        }
        if (row == previous + 1) {
            previous = row;
            continue;
        }
        flush(rangeStart, previous);
        rangeStart = row;
        previous = row;
    }
    flush(rangeStart, previous);
}
