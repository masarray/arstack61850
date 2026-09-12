// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedFleetController.hpp"

#include <QAbstractListModel>
#include <QTimer>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

class IedSignalModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(IedFleetController* backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString logicalDevice READ logicalDevice WRITE setLogicalDevice NOTIFY scopeChanged)
    Q_PROPERTY(QString logicalNode READ logicalNode WRITE setLogicalNode NOTIFY scopeChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int visibleRowCount READ visibleRowCount NOTIFY visibleRowCountChanged)

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

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void activate(int row);

signals:
    void backendChanged();
    void scopeChanged();
    void filterTextChanged();
    void visibleRowCountChanged();

private:
    enum class RowKind { dataObject, dataAttribute };
    struct Row final {
        RowKind kind{RowKind::dataAttribute};
        int sourceIndex{-1};
        QString name;
    };

    void scheduleRebuild();
    void scheduleRefresh();
    void rebuild();
    void refreshSnapshot();
    void refreshSelectedRole();
    [[nodiscard]] QVariantMap sourceItem(int sourceIndex) const;
    [[nodiscard]] bool rowMatches(const QVariantMap& item, const QString& query) const;

    IedFleetController* backend_{};
    QTimer rebuildTimer_;
    QTimer refreshTimer_;
    QVariantList sourceValues_;
    std::vector<Row> rows_;
    QString logicalDevice_;
    QString logicalNode_;
    QString filterText_;
    int observedIedIndex_{-1};
    QString observedFirstReference_;
    QString observedLastReference_;
};
