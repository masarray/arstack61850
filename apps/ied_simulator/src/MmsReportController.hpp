// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedEngineeringContextController.hpp"

#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <QVariantList>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <stop_token>

class MmsReportController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY configurationChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY configurationChanged)
    Q_PROPERTY(IedEngineeringContextController* engineeringContext READ engineeringContext WRITE setEngineeringContext NOTIFY engineeringContextChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool cleanupRequired READ cleanupRequired NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString associationProfile READ associationProfile NOTIFY inventoryChanged)
    Q_PROPERTY(QVariantList dataSets READ dataSets NOTIFY inventoryChanged)
    Q_PROPERTY(QVariantList reportControls READ reportControls NOTIFY inventoryChanged)
    Q_PROPERTY(QVariantList staticCandidates READ staticCandidates NOTIFY inventoryChanged)
    Q_PROPERTY(QVariantList dynamicCandidates READ dynamicCandidates NOTIFY inventoryChanged)
    Q_PROPERTY(QStringList ownedDynamicDataSets READ ownedDynamicDataSets NOTIFY inventoryChanged)
    Q_PROPERTY(int selectedRcbIndex READ selectedRcbIndex NOTIFY selectionChanged)
    Q_PROPERTY(int selectedDataSetIndex READ selectedDataSetIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedRcb READ selectedRcb NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedDataSetMembers READ selectedDataSetMembers NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList receivedReports READ receivedReports NOTIFY reportsChanged)
    Q_PROPERTY(QStringList events READ events NOTIFY reportsChanged)
    Q_PROPERTY(qulonglong receivedReportCount READ receivedReportCount NOTIFY reportsChanged)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY stateChanged)

public:
    explicit MmsReportController(QObject* parent = nullptr);
    ~MmsReportController() override;

    [[nodiscard]] QString host() const { return host_; }
    [[nodiscard]] int port() const noexcept { return port_; }
    void setHost(const QString& value);
    void setPort(int value);
    [[nodiscard]] IedEngineeringContextController* engineeringContext() const noexcept { return engineeringContext_; }
    void setEngineeringContext(IedEngineeringContextController* value);

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] bool cleanupRequired() const noexcept { return cleanupRequired_; }
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] QString associationProfile() const { return associationProfile_; }
    [[nodiscard]] QVariantList dataSets() const { return dataSets_; }
    [[nodiscard]] QVariantList reportControls() const { return reportControls_; }
    [[nodiscard]] QVariantList staticCandidates() const { return staticCandidates_; }
    [[nodiscard]] QVariantList dynamicCandidates() const { return dynamicCandidates_; }
    [[nodiscard]] QStringList ownedDynamicDataSets() const { return ownedDynamicDataSets_; }
    [[nodiscard]] int selectedRcbIndex() const noexcept { return selectedRcbIndex_; }
    [[nodiscard]] int selectedDataSetIndex() const noexcept { return selectedDataSetIndex_; }
    [[nodiscard]] QVariantMap selectedRcb() const { return selectedRcb_; }
    [[nodiscard]] QStringList selectedDataSetMembers() const { return selectedDataSetMembers_; }
    [[nodiscard]] QVariantList receivedReports() const { return receivedReports_; }
    [[nodiscard]] QStringList events() const { return events_; }
    [[nodiscard]] qulonglong receivedReportCount() const noexcept { return receivedReportCount_; }
    [[nodiscard]] qulonglong generation() const noexcept { return generation_; }

    Q_INVOKABLE bool connectToIed();
    Q_INVOKABLE void disconnectFromIed();
    Q_INVOKABLE bool reconnect();
    Q_INVOKABLE bool selectRcb(int row);
    Q_INVOKABLE bool selectDataSet(int row);
    Q_INVOKABLE bool enableSelected(bool requestGeneralInterrogation = true);
    Q_INVOKABLE bool enableSelectedAuthored(
        const QString& dataSetReference,
        const QStringList& triggerOptions,
        const QStringList& optionalFields,
        bool requestGeneralInterrogation = true);
    Q_INVOKABLE bool createDynamicDataSet(
        const QString& dataSetReference,
        const QVariantList& canonicalMembers);
    Q_INVOKABLE bool deleteDynamicDataSet(const QString& dataSetReference);
    Q_INVOKABLE bool disableSelected();
    Q_INVOKABLE bool retryCleanup();
    Q_INVOKABLE QString diagnosticsText() const;

signals:
    void configurationChanged();
    void engineeringContextChanged();
    void stateChanged();
    void inventoryChanged();
    void selectionChanged();
    void reportsChanged();
    void diagnosticsChanged();

private:
    enum class State : quint8 {
        disconnected,
        connecting,
        discovering,
        ready,
        authoring,
        enabling,
        active,
        disabling,
        cleanup_required,
        faulted
    };
    struct WorkerState;

    void appendDiagnostic(const QString& text);
    void clearInventory();
    void adoptEngineeringInventory();
    void refreshSelection();
    void schedulePoll();
    [[nodiscard]] std::shared_ptr<std::stop_source> replaceStopSource();

    QString host_{QStringLiteral("127.0.0.1")};
    int port_{102};
    IedEngineeringContextController* engineeringContext_{};
    State state_{State::disconnected};
    bool operationBusy_{};
    bool pollPending_{};
    bool active_{};
    bool cleanupRequired_{};
    QString lastError_;
    QString associationProfile_;
    QVariantList dataSets_;
    QVariantList reportControls_;
    QVariantList staticCandidates_;
    QVariantList dynamicCandidates_;
    QStringList ownedDynamicDataSets_;
    int selectedRcbIndex_{-1};
    int selectedDataSetIndex_{-1};
    QVariantMap selectedRcb_;
    QStringList selectedDataSetMembers_;
    QVariantList receivedReports_;
    QStringList events_;
    QStringList diagnostics_;
    qulonglong receivedReportCount_{};
    qulonglong generation_{};
    QThreadPool ioPool_;
    QTimer pollTimer_;
    std::shared_ptr<WorkerState> workerState_;
    std::shared_ptr<std::stop_source> stopSource_;
};
