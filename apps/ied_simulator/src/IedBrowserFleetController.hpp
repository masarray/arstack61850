// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedBrowserSessionController.hpp"
#include "SclWorkspaceController.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <vector>

// A slot owns every Browser service for exactly one engineering context.
// Active selection changes only the presentation pointer. It never rebinds
// one live MMS association to a different IED or tears down another slot.
class IedBrowserFleetController : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int workspaceCount READ workspaceCount NOTIFY workspacesChanged)
    Q_PROPERTY(int activeIndex READ activeIndex NOTIFY activeChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(IedEngineeringContextController* activeContext READ activeContext NOTIFY activeChanged)
    Q_PROPERTY(IedBrowserSessionController* activeSession READ activeSession NOTIFY activeChanged)
    Q_PROPERTY(MmsClientController* activeClient READ activeClient NOTIFY activeChanged)
    Q_PROPERTY(MmsReportController* activeReports READ activeReports NOTIFY activeChanged)
    Q_PROPERTY(MmsControlController* activeControls READ activeControls NOTIFY activeChanged)
    Q_PROPERTY(MmsFileSettingsController* activeUtilities READ activeUtilities NOTIFY activeChanged)
    Q_PROPERTY(SclWorkspaceController* activeEngineering READ activeEngineering NOTIFY activeChanged)

public:
    enum Role {
        SlotIdRole = Qt::UserRole + 1,
        LabelRole,
        EndpointRole,
        OnlineRole,
        BusyRole,
        LoadedRole,
        AuthorityRole,
    };
    static constexpr int maximumWorkspaces = 8;

    explicit IedBrowserFleetController(QObject* parent = nullptr);
    ~IedBrowserFleetController() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] int workspaceCount() const noexcept {
        return static_cast<int>(entries_.size());
    }
    [[nodiscard]] int activeIndex() const noexcept { return activeIndex_; }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] IedEngineeringContextController* activeContext() const;
    [[nodiscard]] IedBrowserSessionController* activeSession() const;
    [[nodiscard]] MmsClientController* activeClient() const;
    [[nodiscard]] MmsReportController* activeReports() const;
    [[nodiscard]] MmsControlController* activeControls() const;
    [[nodiscard]] MmsFileSettingsController* activeUtilities() const;
    [[nodiscard]] SclWorkspaceController* activeEngineering() const;

    Q_INVOKABLE IedEngineeringContextController* contextAt(int index) const;
    Q_INVOKABLE IedBrowserSessionController* sessionAt(int index) const;
    Q_INVOKABLE MmsClientController* clientAt(int index) const;
    Q_INVOKABLE MmsReportController* reportsAt(int index) const;
    Q_INVOKABLE MmsControlController* controlsAt(int index) const;
    Q_INVOKABLE MmsFileSettingsController* utilitiesAt(int index) const;
    Q_INVOKABLE SclWorkspaceController* engineeringAt(int index) const;
    Q_INVOKABLE bool switchTo(int index);
    Q_INVOKABLE bool newWorkspace();
    Q_INVOKABLE bool prepareForNewSource();
    Q_INVOKABLE bool openSclIedInNewWorkspace(const QString& iedName);
    Q_INVOKABLE bool closeWorkspace(int index);

signals:
    void workspacesChanged();
    void activeChanged();
    void stateChanged();

private:
    struct Entry;
    [[nodiscard]] Entry* entryAt(int index) const noexcept;
    [[nodiscard]] Entry* activeEntry() const noexcept;
    void connectEntry(Entry* entry);
    void emitEntryChanged(const Entry* entry);
    bool fail(const QString& message);
    void clearError();

    std::vector<std::unique_ptr<Entry>> entries_;
    int activeIndex_{-1};
    quint64 nextSlotId_{1};
    QString lastError_;
};
