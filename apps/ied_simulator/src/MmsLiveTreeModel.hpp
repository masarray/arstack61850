// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/mms/live_model.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>


class MmsLiveTreeModel;

// Virtualized, bounded view of canonical MMS nodes; never a second semantic tree.
class MmsLiveDetailModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString rootLabel READ rootLabel NOTIFY rootChanged)
    Q_PROPERTY(QString rootKind READ rootKind NOTIFY rootChanged)
    Q_PROPERTY(int rootChildCount READ rootChildCount NOTIFY rootChanged)
    Q_PROPERTY(bool truncated READ truncated NOTIFY projectionChanged)
public:
    enum DetailRole {
        LabelRole = Qt::UserRole + 1, KindRole, ReferenceRole, DepthRole,
        HasChildrenRole, ExpandedRole, SelectedRole, FunctionalConstraintRole,
        DescriptionRole, ValueRole
    };
    explicit MmsLiveDetailModel(MmsLiveTreeModel* owner);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString rootLabel() const;
    QString rootKind() const;
    int rootChildCount() const;
    bool truncated() const noexcept { return truncated_; }
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void select(int row);
    Q_INVOKABLE void drillInto(int row);
    Q_INVOKABLE void selectParent();
    void setRoot(int nodeIndex);
    void resetForDocument();
    void nodeValueChanged(int nodeIndex);
signals:
    void rootChanged();
    void projectionChanged();
private:
    struct Row { int node; int depth; };
    void rebuild();
    void append(int node, int depth);
    int rowOf(int node) const;
    MmsLiveTreeModel* owner_;
    QVector<Row> rows_;
    QSet<int> expanded_;
    int root_{-1};
    int selected_{-1};
    bool truncated_{};
    static constexpr int maximumRows_{4096};
};

class MmsLiveTreeModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(int selectedRow READ selectedRow WRITE selectRow NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedNode READ selectedNode NOTIFY selectionChanged)
    Q_PROPERTY(QAbstractItemModel* detailModel READ detailModel CONSTANT)
    Q_PROPERTY(QVariantMap presentationNode READ presentationNode NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList selectedChildren READ selectedChildren NOTIFY selectionChanged)
    Q_PROPERTY(int selectedChildCount READ selectedChildCount NOTIFY selectionChanged)
    Q_PROPERTY(bool selectedChildrenTruncated READ selectedChildrenTruncated NOTIFY selectionChanged)
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
    [[nodiscard]] QAbstractItemModel* detailModel() const noexcept { return detailModel_; }
    [[nodiscard]] QVariantMap presentationNode() const;
    [[nodiscard]] QVariantList selectedChildren() const;
    [[nodiscard]] int selectedChildCount() const noexcept;
    [[nodiscard]] bool selectedChildrenTruncated() const noexcept;
    [[nodiscard]] int totalNodeCount() const noexcept { return nodes_.size(); }
    [[nodiscard]] int visibleNodeCount() const noexcept { return visible_.size(); }

    Q_INVOKABLE void selectRow(int row);
    Q_INVOKABLE bool selectChild(int row);
    Q_INVOKABLE bool selectParent();
    Q_INVOKABLE bool selectRoot();
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE bool selectMmsItem(const QString& domain, const QString& item);
    Q_INVOKABLE QVariantMap nodeForReference(const QString& reference) const;
    Q_INVOKABLE void clear();

    void applyDocument(const ar::iec61850::mms::MmsLiveModelDocument& document);
    [[nodiscard]] std::optional<Selection> selectionSnapshot() const;
    [[nodiscard]] QVector<ReadTarget> selectedReadTargets() const;
    [[nodiscard]] QVector<ReadTarget> readTargetsForVisibleRange(
        int firstRow,
        int lastRow,
        int maximumTargets = 64) const;
    [[nodiscard]] QVector<ReadTarget> readTargetsForReferences(
        const QStringList& references,
        int maximumTargets = 64) const;
    void applyReadValue(const QString& key, const QString& displayValue);

signals:
    void filterTextChanged();
    void selectionChanged();
    void countsChanged();

private:
    friend class MmsLiveDetailModel;
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
        QString description;
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
    [[nodiscard]] QVariantMap nodeMap(int nodeIndex) const;
    [[nodiscard]] int presentationNodeIndex() const noexcept;
    bool selectNode(int nodeIndex);
    [[nodiscard]] QVector<ReadTarget> readTargetsForNode(int nodeIndex) const;
    [[nodiscard]] int appendNode(Node node);

    MmsLiveDetailModel* detailModel_{};
    QVector<Node> nodes_;
    QVector<int> visible_;
    QHash<QString, int> mmsIndex_;
    QString filterText_;
    int selectedNode_{-1};
    static constexpr int maximumProjectedChildren_{4096};
};
