// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedFleetController.hpp"

#include <QAbstractListModel>
#include <QSet>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

class IedNavigationModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(IedFleetController* backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString selectedLogicalDevice READ selectedLogicalDevice NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedLogicalNode READ selectedLogicalNode NOTIFY selectionChanged)

public:
    enum Roles {
        KindRole = Qt::UserRole + 1,
        NameRole,
        DepthRole,
        ExpandedRole,
        ExpandableRole,
        SelectedRole,
        LogicalDeviceRole,
        LogicalNodeRole,
    };

    explicit IedNavigationModel(QObject* parent = nullptr);

    [[nodiscard]] IedFleetController* backend() const noexcept;
    void setBackend(IedFleetController* backend);

    [[nodiscard]] QString selectedLogicalDevice() const;
    [[nodiscard]] QString selectedLogicalNode() const;

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void activate(int row);
    Q_INVOKABLE void expandAll();
    Q_INVOKABLE void collapseAll();

signals:
    void backendChanged();
    void selectionChanged();

private:
    struct Row final {
        QString kind;
        QString name;
        QString logicalDevice;
        QString logicalNode;
        int depth{};
        bool expandable{};
        bool expanded{};
    };

    void scheduleRebuild();
    void rebuild();
    [[nodiscard]] bool containsSelection(
        const QString& logicalDevice,
        const QString& logicalNode) const;

    IedFleetController* backend_{};
    QTimer rebuildTimer_;
    std::vector<Row> rows_;
    QSet<QString> collapsedDevices_;
    QString selectedLogicalDevice_;
    QString selectedLogicalNode_;
    int observedIedIndex_{-1};
};
