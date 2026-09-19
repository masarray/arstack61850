// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedFleetController.hpp"

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QTimer>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

class IedSignalModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(IedFleetController* backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString logicalDevice READ logicalDevice WRITE setLogicalDevice NOTIFY scopeChanged)
    Q_PROPERTY(QString logicalNode READ logicalNode WRITE setLogicalNode NOTIFY scopeChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int visibleRowCount READ visibleRowCount NOTIFY visibleRowCountChanged)
    Q_PROPERTY(qulonglong refreshRequestCount READ refreshRequestCount NOTIFY performanceCountersChanged)
    Q_PROPERTY(qulonglong refreshFlushCount READ refreshFlushCount NOTIFY performanceCountersChanged)
    Q_PROPERTY(qint64 lastRefreshLatencyMilliseconds READ lastRefreshLatencyMilliseconds NOTIFY performanceCountersChanged)
    Q_PROPERTY(qint64 maxRefreshLatencyMilliseconds READ maxRefreshLatencyMilliseconds NOTIFY performanceCountersChanged)
    Q_PROPERTY(qint64 lastRebuildMilliseconds READ lastRebuildMilliseconds NOTIFY performanceCountersChanged)
    Q_PROPERTY(qint64 maxRebuildMilliseconds READ maxRebuildMilliseconds NOTIFY performanceCountersChanged)

public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        NameRole,
        DepthRole,
        ValueRole,
        FunctionalConstraintRole,
        TypeRole,
        QualityRole,
        SourceIndexRole,
        WritableRole,
        ChangedRole,
        ReferenceRole,
        SelectedRole,
    };

    explicit IedSignalModel(QObject* parent = nullptr);

    [[nodiscard]] IedFleetController* backend() const noexcept;
    void setBackend(IedFleetController* backend);

    [[nodiscard]] QString logicalDevice() const;
    void setLogicalDevice(const QString& value);
    [[nodiscard]] QString logicalNode() const;
    void setLogicalNode(const QString& value);
    [[nodiscard]] QString filterText() const;
    void setFilterText(const QString& value);
    [[nodiscard]] int visibleRowCount() const noexcept;
    [[nodiscard]] quint64 refreshRequestCount() const noexcept { return refreshRequestCount_; }
    [[nodiscard]] quint64 refreshFlushCount() const noexcept { return refreshFlushCount_; }
    [[nodiscard]] qint64 lastRefreshLatencyMilliseconds() const noexcept {
        return lastRefreshLatencyMilliseconds_;
    }
    [[nodiscard]] qint64 maxRefreshLatencyMilliseconds() const noexcept {
        return maxRefreshLatencyMilliseconds_;
    }
    [[nodiscard]] qint64 lastRebuildMilliseconds() const noexcept { return lastRebuildMilliseconds_; }
    [[nodiscard]] qint64 maxRebuildMilliseconds() const noexcept { return maxRebuildMilliseconds_; }

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void resetPerformanceCounters();

signals:
    void backendChanged();
    void scopeChanged();
    void filterTextChanged();
    void visibleRowCountChanged();
    void performanceCountersChanged();

private:
    enum class RowKind { dataObject, dataAttribute };
    struct Row final {
        RowKind kind{RowKind::dataAttribute};
        int sourceIndex{-1};
        QString name;
        QString functionalConstraint;
        QString type;
        QString reference;
        QString value;
        QString quality;
        bool writable{};
        bool changed{};
    };

    void scheduleRebuild();
    void scheduleRefresh();
    void rebuild();
    void refreshSnapshot();
    void refreshSelectedRole();
    void emitRowsChanged(QVector<int> rows, const QList<int>& roles);
    [[nodiscard]] bool rowMatches(
        const IedPointStore::PointRecord& item,
        const QString& query) const;
    [[nodiscard]] Row makeRow(
        RowKind kind,
        int sourceIndex,
        const QString& name,
        const IedPointStore::PointRecord& item) const;

    IedFleetController* backend_{};
    QTimer rebuildTimer_;
    QTimer refreshTimer_;
    QElapsedTimer refreshLatencyTimer_;
    std::vector<Row> rows_;
    QHash<int, QVector<int>> sourceRows_;
    QString logicalDevice_;
    QString logicalNode_;
    QString filterText_;
    quint64 refreshRequestCount_{};
    quint64 refreshFlushCount_{};
    qint64 lastRefreshLatencyMilliseconds_{};
    qint64 maxRefreshLatencyMilliseconds_{};
    qint64 lastRebuildMilliseconds_{};
    qint64 maxRebuildMilliseconds_{};
    int observedIedIndex_{-1};
    int observedSelectedSourceIndex_{-1};
    int observedSourceCount_{};
    QString observedFirstReference_;
    QString observedLastReference_;
};
