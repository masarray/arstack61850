// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedFleetController.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>

class QTimer;

class IedCommissioningModel : public QObject {
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
    Q_PROPERTY(int behaviorCount READ behaviorCount NOTIFY behaviorChanged)
    Q_PROPERTY(int behaviorCapacity READ behaviorCapacity CONSTANT)
    Q_PROPERTY(quint64 behaviorTickCount READ behaviorTickCount NOTIFY behaviorChanged)
    Q_PROPERTY(quint64 behaviorRejectedCount READ behaviorRejectedCount NOTIFY behaviorChanged)

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
    [[nodiscard]] int behaviorCount() const noexcept { return behaviors_.size(); }
    [[nodiscard]] int behaviorCapacity() const noexcept { return kBehaviorCapacity; }
    [[nodiscard]] quint64 behaviorTickCount() const noexcept { return behaviorTickCount_; }
    [[nodiscard]] quint64 behaviorRejectedCount() const noexcept { return behaviorRejectedCount_; }

    Q_INVOKABLE QVariantMap item(int visibleRow) const;
    Q_INVOKABLE QVariantMap member(int row) const;
    Q_INVOKABLE void select(int visibleRow);
    Q_INVOKABLE bool focusBoundDataSet();

    // Runtime commissioning actions. These deliberately reuse the controller's
    // bounded live-delta path instead of creating a second simulator state path.
    Q_INVOKABLE int firstDrivableMember() const;
    Q_INVOKABLE bool canDriveMember(int row) const;
    Q_INVOKABLE bool focusMemberValue(int row);
    Q_INVOKABLE bool focusControlStatus();
    Q_INVOKABLE QVariantMap memberBehavior(int row) const;
    Q_INVOKABLE bool startMemberBehavior(
        int row,
        const QString& mode,
        int intervalMilliseconds,
        double step = 1.0);
    Q_INVOKABLE bool pulseSelectedService(int intervalMilliseconds = 500);
    Q_INVOKABLE bool stopMemberBehavior(int row, bool restore = true);
    Q_INVOKABLE void stopAllBehaviors(bool restore = true);

signals:
    void backendChanged();
    void filterChanged();
    void modelChanged();
    void selectionChanged();
    void behaviorChanged();

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

    struct PointBinding final {
        int iedIndex{-1};
        int sourceIndex{-1};
        int pointStoreIndex{-1};
        QString sessionKey;
        QString reference;
    };

    struct BehaviorSlot final {
        int iedIndex{-1};
        int pointStoreIndex{-1};
        QString sessionKey;
        QString reference;
        QString mode;
        QString originalValue;
        QString originalQuality;
        QString originalOrigin;
        QString originalUpdated;
        QString lastAppliedValue;
        double step{1.0};
        int intervalMilliseconds{1'000};
        qint64 nextDueMilliseconds{};
        quint64 ticks{};
        bool originalChanged{};
        bool pulseRestorePending{};
        bool rejectionReported{};
    };

    static constexpr int kBehaviorCapacity = 16;
    static constexpr int kBehaviorMinimumIntervalMilliseconds = 100;
    static constexpr int kBehaviorMaximumIntervalMilliseconds = 60'000;

    void synchronize();
    void rebuild();
    void applyFilter(bool preserveSelection = true);
    [[nodiscard]] QVariantMap mapForHandle(const Handle& handle, int catalogIndex) const;
    [[nodiscard]] bool matches(const Handle& handle) const;
    [[nodiscard]] const std::vector<ar::iec61850::scl::SclDataSetEntry>* entriesFor(
        const Handle& handle) const noexcept;
    [[nodiscard]] QString dataSetReferenceFor(const Handle& handle) const;
    [[nodiscard]] static QString kindName(Kind kind);

    [[nodiscard]] std::optional<PointBinding> pointBindingForMember(int row) const;
    [[nodiscard]] std::optional<PointBinding> controlStatusBinding() const;
    [[nodiscard]] QString iedSessionKey(int iedIndex) const;
    [[nodiscard]] bool drivablePoint(const IedPointStore::PointRecord& point) const;
    [[nodiscard]] QString normalizedBehaviorMode(const QString& mode) const;
    [[nodiscard]] QString nextBehaviorValue(
        const IedPointStore::PointRecord& point,
        const BehaviorSlot& slot,
        bool* ok) const;
    [[nodiscard]] int behaviorIndexFor(const PointBinding& binding) const noexcept;
    [[nodiscard]] bool applyBehaviorValue(
        BehaviorSlot& slot,
        const QString& value,
        bool restoreOriginal = false);
    void ensureBehaviorTimer();
    void advanceBehaviors();
    void pruneBehaviors();

    IedFleetController* backend_{};
    QVector<Handle> handles_;
    QVector<int> visibleHandles_;
    QVector<BehaviorSlot> behaviors_;
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
    quint64 behaviorTickCount_{};
    quint64 behaviorRejectedCount_{};
    QTimer* behaviorTimer_{};
    QElapsedTimer behaviorClock_;
};
