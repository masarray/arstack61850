// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAbstractListModel>
#include <QTimer>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

class IedActivityModel : public QAbstractListModel {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
    Q_PROPERTY(QString severityFilter READ severityFilter WRITE setSeverityFilter NOTIFY severityFilterChanged)
    Q_PROPERTY(int retainedCount READ retainedCount NOTIFY retainedCountChanged)
    Q_PROPERTY(int visibleCount READ visibleCount NOTIFY visibleCountChanged)

public:
    enum Role {
        TimeRole = Qt::UserRole + 1,
        CategoryRole,
        MessageRole,
        SeverityRole,
        IedRole,
    };
    Q_ENUM(Role)

    explicit IedActivityModel(QObject* parent = nullptr);

    [[nodiscard]] QString filterText() const;
    [[nodiscard]] QString severityFilter() const;
    [[nodiscard]] int retainedCount() const noexcept;
    [[nodiscard]] int visibleCount() const noexcept;

    void setFilterText(const QString& value);
    void setSeverityFilter(const QString& value);

    [[nodiscard]] int rowCount(const QModelIndex& parent = QModelIndex{}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Compatibility surface for the controller's existing bounded activity
    // append path. Events are staged for at most one UI frame, then published
    // as one model reset instead of one QML list rebuild per child-process line.
    void push_front(const QVariant& value);
    void removeLast();
    void clear();
    [[nodiscard]] int size() const noexcept;
    [[nodiscard]] QVariantList snapshot() const;
    operator QVariantList() const { return snapshot(); }

    [[nodiscard]] QVariantList::const_iterator begin() const noexcept { return events_.cbegin(); }
    [[nodiscard]] QVariantList::const_iterator end() const noexcept { return events_.cend(); }

signals:
    void filterTextChanged();
    void severityFilterChanged();
    void retainedCountChanged();
    void visibleCountChanged();

private:
    static constexpr int maxRetainedEvents_ = 300;
    static constexpr int maxPendingEvents_ = 128;

    [[nodiscard]] bool matches(const QVariantMap& event) const;
    [[nodiscard]] QVector<int> buildVisibleIndices(const QVariantList& events) const;
    void schedulePublish();
    void publishPending();
    void scheduleFilterRebuild();
    void rebuildVisible();

    QVariantList events_;
    QVariantList pendingEvents_;
    QVector<int> visibleIndices_;
    QTimer publishTimer_;
    QTimer filterTimer_;
    QString filterText_;
    QString severityFilter_{QStringLiteral("All")};
    int droppedPendingEvents_{};
};
