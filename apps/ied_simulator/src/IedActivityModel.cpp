// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedActivityModel.hpp"

#include <QDateTime>
#include <QVariantMap>

#include <algorithm>

IedActivityModel::IedActivityModel(QObject* parent)
    : QAbstractListModel(parent) {
    publishTimer_.setSingleShot(true);
    publishTimer_.setInterval(16);
    connect(&publishTimer_, &QTimer::timeout, this, &IedActivityModel::publishPending);

    filterTimer_.setSingleShot(true);
    filterTimer_.setInterval(80);
    connect(&filterTimer_, &QTimer::timeout, this, &IedActivityModel::rebuildVisible);
}

QString IedActivityModel::filterText() const { return filterText_; }
QString IedActivityModel::severityFilter() const { return severityFilter_; }
int IedActivityModel::retainedCount() const noexcept {
    const qsizetype stagedCount =
        events_.size() + pendingEvents_.size() + (droppedPendingEvents_ > 0 ? 1 : 0);
    return static_cast<int>(std::min<qsizetype>(maxRetainedEvents_, stagedCount));
}
int IedActivityModel::visibleCount() const noexcept { return visibleIndices_.size(); }

void IedActivityModel::setFilterText(const QString& value) {
    if (filterText_ == value) return;
    filterText_ = value;
    emit filterTextChanged();
    scheduleFilterRebuild();
}

void IedActivityModel::setSeverityFilter(const QString& value) {
    const auto normalized = value.trimmed().isEmpty() ? QStringLiteral("All") : value.trimmed();
    if (severityFilter_ == normalized) return;
    severityFilter_ = normalized;
    emit severityFilterChanged();
    scheduleFilterRebuild();
}

int IedActivityModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : visibleIndices_.size();
}

QVariant IedActivityModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= visibleIndices_.size()) return {};
    const int sourceIndex = visibleIndices_.at(index.row());
    if (sourceIndex < 0 || sourceIndex >= events_.size()) return {};
    const auto event = events_.at(sourceIndex).toMap();
    switch (role) {
    case TimeRole: return event.value(QStringLiteral("time"));
    case CategoryRole: return event.value(QStringLiteral("category"));
    case MessageRole: return event.value(QStringLiteral("message"));
    case SeverityRole: return event.value(QStringLiteral("severity"));
    case IedRole: return event.value(QStringLiteral("ied"));
    default: return {};
    }
}

QHash<int, QByteArray> IedActivityModel::roleNames() const {
    return {
        {TimeRole, "time"},
        {CategoryRole, "category"},
        {MessageRole, "message"},
        {SeverityRole, "severity"},
        {IedRole, "ied"},
    };
}

void IedActivityModel::push_front(const QVariant& value) {
    if (pendingEvents_.size() >= maxPendingEvents_) {
        pendingEvents_.removeFirst();
        ++droppedPendingEvents_;
    }
    pendingEvents_.push_back(value);
    schedulePublish();
}

void IedActivityModel::removeLast() {
    // The compatibility caller still performs a legacy >300 trim loop. size()
    // is already capped, so this is only used by older direct callers.
    if (!pendingEvents_.isEmpty()) {
        pendingEvents_.removeFirst();
        schedulePublish();
        return;
    }
    if (events_.isEmpty()) return;
    events_.removeLast();
    rebuildVisible();
}

void IedActivityModel::clear() {
    publishTimer_.stop();
    filterTimer_.stop();
    if (events_.isEmpty() && pendingEvents_.isEmpty() && droppedPendingEvents_ == 0) return;
    const int previousRetained = retainedCount();
    const int previousVisible = visibleCount();
    beginResetModel();
    events_.clear();
    pendingEvents_.clear();
    visibleIndices_.clear();
    droppedPendingEvents_ = 0;
    endResetModel();
    if (previousRetained != 0) emit retainedCountChanged();
    if (previousVisible != 0) emit visibleCountChanged();
}

int IedActivityModel::size() const noexcept { return retainedCount(); }

QVariantList IedActivityModel::snapshot() const {
    QVariantList result;
    result.reserve(retainedCount());

    if (droppedPendingEvents_ > 0) {
        QVariantMap summary;
        summary.insert(
            QStringLiteral("time"),
            QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
        summary.insert(QStringLiteral("category"), QStringLiteral("Diagnostics"));
        summary.insert(
            QStringLiteral("message"),
            QStringLiteral("%1 high-rate activity events were coalesced to protect the UI event loop.")
                .arg(droppedPendingEvents_));
        summary.insert(QStringLiteral("severity"), QStringLiteral("Warning"));
        summary.insert(QStringLiteral("ied"), QString{});
        result.push_back(summary);
    }
    for (auto it = pendingEvents_.crbegin(); it != pendingEvents_.crend(); ++it) {
        if (result.size() >= maxRetainedEvents_) break;
        result.push_back(*it);
    }
    for (const auto& event : events_) {
        if (result.size() >= maxRetainedEvents_) break;
        result.push_back(event);
    }
    return result;
}

bool IedActivityModel::matches(const QVariantMap& event) const {
    if (severityFilter_ != QStringLiteral("All") &&
        event.value(QStringLiteral("severity"), QStringLiteral("Info")).toString() != severityFilter_) {
        return false;
    }

    const auto query = filterText_.trimmed();
    if (query.isEmpty()) return true;
    const QStringList fields{
        event.value(QStringLiteral("time")).toString(),
        event.value(QStringLiteral("severity")).toString(),
        event.value(QStringLiteral("ied")).toString(),
        event.value(QStringLiteral("category")).toString(),
        event.value(QStringLiteral("message")).toString(),
    };
    for (const auto& field : fields) {
        if (field.contains(query, Qt::CaseInsensitive)) return true;
    }
    return false;
}

QVector<int> IedActivityModel::buildVisibleIndices(const QVariantList& events) const {
    QVector<int> result;
    result.reserve(events.size());
    for (int index = 0; index < events.size(); ++index) {
        if (matches(events.at(index).toMap())) result.push_back(index);
    }
    return result;
}

void IedActivityModel::schedulePublish() {
    if (!publishTimer_.isActive()) publishTimer_.start();
}

void IedActivityModel::publishPending() {
    if (pendingEvents_.isEmpty() && droppedPendingEvents_ == 0) return;
    const int previousRetained = events_.size();
    const int previousVisible = visibleIndices_.size();

    auto next = snapshot();
    auto nextVisible = buildVisibleIndices(next);
    beginResetModel();
    events_ = std::move(next);
    pendingEvents_.clear();
    droppedPendingEvents_ = 0;
    visibleIndices_ = std::move(nextVisible);
    endResetModel();

    if (previousRetained != events_.size()) emit retainedCountChanged();
    if (previousVisible != visibleIndices_.size()) emit visibleCountChanged();
}

void IedActivityModel::scheduleFilterRebuild() {
    if (!filterTimer_.isActive()) filterTimer_.start();
}

void IedActivityModel::rebuildVisible() {
    // Publish staged events first so a filter change never reveals a stale row
    // mapping. publishPending() already performs the reset in that case.
    if (!pendingEvents_.isEmpty() || droppedPendingEvents_ > 0) {
        publishTimer_.stop();
        publishPending();
        return;
    }

    const auto nextVisible = buildVisibleIndices(events_);
    if (nextVisible == visibleIndices_) return;
    const int previousVisible = visibleIndices_.size();
    beginResetModel();
    visibleIndices_ = nextVisible;
    endResetModel();
    if (previousVisible != visibleIndices_.size()) emit visibleCountChanged();
}
