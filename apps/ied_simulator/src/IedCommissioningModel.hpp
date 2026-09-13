// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedFleetController.hpp"

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

class IedCommissioningModel final : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(IedFleetController* backend READ backend WRITE setBackend NOTIFY backendChanged)
    Q_PROPERTY(QString kindFilter READ kindFilter WRITE setKindFilter NOTIFY filterChanged)
    Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterChanged)
    Q_PROPERTY(int itemCount READ itemCount NOTIFY modelChanged)
    Q_PROPERTY(int memberCount READ memberCount NOTIFY modelChanged)
    Q_PROPERTY(int selectedCatalogIndex READ selectedCatalogIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedItem READ selectedItem NOTIFY selectionChanged)
    Q_PROPERTY(int dataSetCount READ dataSetCount NOTIFY modelChanged)
    Q_PROPERTY(int reportCount READ reportCount NOTIFY modelChanged)
    Q_PROPERTY(int gooseCount READ gooseCount NOTIFY modelChanged)
    Q_PROPERTY(int controlCount READ controlCount NOTIFY modelChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY modelChanged)

public:
    explicit IedCommissioningModel(QObject* parent = nullptr);

    [[nodiscard]] IedFleetController* backend() const noexcept { return backend_; }
    void setBackend(IedFleetController* backend);

    [[nodiscard]] QString kindFilter() const { return kindFilter_; }
    void setKindFilter(const QString& filter);
    [[nodiscard]] QString filterText() const { return filterText_; }
    void setFilterText(const QString& filter);

    [[nodiscard]] int itemCount() const noexcept { return visibleHandles_.size(); }
    [[nodiscard]] int memberCount() const noexcept;
    [[nodiscard]] int selectedCatalogIndex() const noexcept { return selectedHandleIndex_; }
    [[nodiscard]] QVariantMap selectedItem() const;
    [[nodiscard]] int dataSetCount() const noexcept { return dataSetCount_; }
    [[nodiscard]] int reportCount() const noexcept { return reportCount_; }
    [[nodiscard]] int gooseCount() const noexcept { return gooseCount_; }
    [[nodiscard]] int controlCount() const noexcept { return controlCount_; }
    [[nodiscard]] quint64 revision() const noexcept { return revision_; }

    Q_INVOKABLE QVariantMap item(int visibleRow) const;
    Q_INVOKABLE QVariantMap member(int row) const;
    Q_INVOKABLE void select(int visibleRow);
    Q_INVOKABLE bool focusBoundDataSet();

signals:
    void backendChanged();
    void filterChanged();
    void modelChanged();
    void selectionChanged();

private:
    enum class Kind {
        dataSet,
        report,
        goose,
        control,
    };

    struct Handle final {
        Kind kind{Kind::dataSet};
        int documentIndex{-1};
        int sourceIndex{-1};
        QString identity;
    };

    void synchronize();
    void rebuild();
    void applyFilter(bool preserveSelection = true);
    [[nodiscard]] QVariantMap mapForHandle(const Handle& handle, int catalogIndex) const;
    [[nodiscard]] bool matches(const Handle& handle) const;
    [[nodiscard]] const std::vector<ar::iec61850::scl::SclDataSetEntry>* entriesFor(
        const Handle& handle) const noexcept;
    [[nodiscard]] QString dataSetReferenceFor(const Handle& handle) const;
    [[nodiscard]] static QString kindName(Kind kind);

    IedFleetController* backend_{};
    QVector<Handle> handles_;
    QVector<int> visibleHandles_;
    QString kindFilter_{QStringLiteral("All")};
    QString filterText_;
    QString synchronizedSessionKey_;
    qsizetype synchronizedDocumentCount_{-1};
    int selectedHandleIndex_{-1};
    int dataSetCount_{};
    int reportCount_{};
    int gooseCount_{};
    int controlCount_{};
    quint64 revision_{};
};
