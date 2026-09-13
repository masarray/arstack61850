// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_model.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

class MmsLiveTreeModel final : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE selectRow NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedNode READ selectedNode NOTIFY selectionChanged)
    Q_PROPERTY(int totalNodeCount READ totalNodeCount NOTIFY countsChanged)
    Q_PROPERTY(int visibleNodeCount READ visibleNodeCount NOTIFY countsChanged)

public:
    enum Role {
        LabelRole = Qt::UserRole + 1,
        KindRole,
        ReferenceRole,
        DepthRole,
        HasChildrenRole,
        ExpandedRole,
        SelectedRole,
        FunctionalConstraintRole,
        MmsTypeRole,
        SclTypeRole,
        TypeStatusRole,
        ValueRole,
        WritableRole,
        MmsDomainRole,
        MmsItemRole,
    };
    Q_ENUM(Role)

    struct ReadTarget final {
        QString key;
        QString domain;
        QString item;
    };

    struct Selection final {
        QString key;
        QString domain;
        QString item;
        QString reference;
        QString functionalConstraint;
        QString mmsType;
        QString sclType;
        bool writable{};
    };

    explicit MmsLiveTreeModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString filterText() const { return filterText_; }
    void setFilterText(const QString& value);

    [[nodiscard]] int selectedRow() const noexcept;
    [[nodiscard]] QVariantMap selectedNode() const;
    [[nodiscard]] int totalNodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] int visibleNodeCount() const noexcept { return visible_.size(); }

    Q_INVOKABLE void selectRow(int row);
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE bool selectMmsItem(const QString& domain, const QString& item);
    Q_INVOKABLE void clear();

    void applyDocument(const ar::iec61850::mms::MmsLiveModelDocument& document);
    [[nodiscard]] std::optional<Selection> selectionSnapshot() const;
    [[nodiscard]] QVector<ReadTarget> selectedReadTargets() const;
    [[nodiscard]] QVector<ReadTarget> readTargetsForVisibleRange(
        int firstRow,
        int lastRow,
        int maximumTargets = 64) const;
    void applyReadValue(const QString& key, const QString& displayValue);

signals:
    void filterTextChanged();
    void selectionChanged();
    void countsChanged();

private:
    enum class NodeKind : quint8 { ied, logicalDevice, logicalNode, dataObject, dataAttribute };

    struct Node final {
        NodeKind kind{NodeKind::ied};
        int parent{-1};
        QVector<int> children;
        int depth{};
        bool expanded{};
        QString label;
        QString reference;
        QString domain;
        QString item;
        QString attributePath;
        QString functionalConstraint;
        QString mmsType;
        QString sclType;
        QString typeStatus;
        QString value;
        bool writable{};
    };

    [[nodiscard]] static QString kindName(NodeKind kind);
    [[nodiscard]] static QString nodeKey(const QString& domain, const QString& item);
    [[nodiscard]] static bool isWritableAttribute(
        const QString& functionalConstraint,
        const QString& typeStatus,
        const QString& mmsType);
    [[nodiscard]] bool matchesFilter(const Node& node, const QString& needle) const;
    void rebuildVisible();
    void appendVisibleSubtree(int nodeIndex, const QVector<bool>* included);
    [[nodiscard]] int visibleRowForNode(int nodeIndex) const noexcept;
    [[nodiscard]] QString siblingValue(int nodeIndex, const QString& leafName) const;
    [[nodiscard]] QVector<ReadTarget> readTargetsForNode(int nodeIndex) const;
    [[nodiscard]] int appendNode(Node node);

    QVector<Node> nodes_;
    QVector<int> visible_;
    QHash<QString, int> mmsIndex_;
    QString filterText_;
    int selectedNode_{-1};
};
