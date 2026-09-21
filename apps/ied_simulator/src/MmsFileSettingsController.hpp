// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedEngineeringContextController.hpp"

#include <QObject>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <stop_token>

class MmsFileSettingsController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY configurationChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY configurationChanged)
    Q_PROPERTY(IedEngineeringContextController* engineeringContext READ engineeringContext WRITE setEngineeringContext NOTIFY engineeringContextChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool operationBusy READ operationBusy NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString associationProfile READ associationProfile NOTIFY settingsChanged)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY stateChanged)

    Q_PROPERTY(QString currentDirectory READ currentDirectory NOTIFY filesChanged)
    Q_PROPERTY(QVariantList fileEntries READ fileEntries NOTIFY filesChanged)
    Q_PROPERTY(int fileEntryCount READ fileEntryCount NOTIFY filesChanged)
    Q_PROPERTY(int filePageCount READ filePageCount NOTIFY filesChanged)

    Q_PROPERTY(bool downloadActive READ downloadActive NOTIFY transferChanged)
    Q_PROPERTY(qulonglong downloadBytes READ downloadBytes NOTIFY transferChanged)
    Q_PROPERTY(qulonglong downloadExpectedBytes READ downloadExpectedBytes NOTIFY transferChanged)
    Q_PROPERTY(bool downloadExpectedKnown READ downloadExpectedKnown NOTIFY transferChanged)
    Q_PROPERTY(double downloadPercent READ downloadPercent NOTIFY transferChanged)
    Q_PROPERTY(QString lastDownloadRemotePath READ lastDownloadRemotePath NOTIFY transferChanged)
    Q_PROPERTY(QString lastDownloadLocalPath READ lastDownloadLocalPath NOTIFY transferChanged)

    Q_PROPERTY(QVariantList settingGroups READ settingGroups NOTIFY settingsChanged)
    Q_PROPERTY(int settingGroupCount READ settingGroupCount NOTIFY settingsChanged)
    Q_PROPERTY(int selectedSettingGroupIndex READ selectedSettingGroupIndex NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedSettingGroup READ selectedSettingGroup NOTIFY selectionChanged)

public:
    explicit MmsFileSettingsController(QObject* parent = nullptr);
    ~MmsFileSettingsController() override;

    [[nodiscard]] QString host() const { return host_; }
    [[nodiscard]] int port() const noexcept { return port_; }
    void setHost(const QString& value);
    void setPort(int value);
    [[nodiscard]] IedEngineeringContextController* engineeringContext() const noexcept { return engineeringContext_; }
    void setEngineeringContext(IedEngineeringContextController* value);

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool operationBusy() const noexcept { return operationBusy_; }
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] QString associationProfile() const { return associationProfile_; }
    [[nodiscard]] qulonglong generation() const noexcept { return generation_; }

    [[nodiscard]] QString currentDirectory() const { return currentDirectory_; }
    [[nodiscard]] QVariantList fileEntries() const { return fileEntries_; }
    [[nodiscard]] int fileEntryCount() const noexcept { return fileEntries_.size(); }
    [[nodiscard]] int filePageCount() const noexcept { return filePageCount_; }

    [[nodiscard]] bool downloadActive() const noexcept { return downloadActive_; }
    [[nodiscard]] qulonglong downloadBytes() const noexcept { return downloadBytes_; }
    [[nodiscard]] qulonglong downloadExpectedBytes() const noexcept { return downloadExpectedBytes_; }
    [[nodiscard]] bool downloadExpectedKnown() const noexcept { return downloadExpectedKnown_; }
    [[nodiscard]] double downloadPercent() const noexcept;
    [[nodiscard]] QString lastDownloadRemotePath() const { return lastDownloadRemotePath_; }
    [[nodiscard]] QString lastDownloadLocalPath() const { return lastDownloadLocalPath_; }

    [[nodiscard]] QVariantList settingGroups() const { return settingGroups_; }
    [[nodiscard]] int settingGroupCount() const noexcept { return settingGroups_.size(); }
    [[nodiscard]] int selectedSettingGroupIndex() const noexcept { return selectedSettingGroupIndex_; }
    [[nodiscard]] QVariantMap selectedSettingGroup() const { return selectedSettingGroup_; }

    Q_INVOKABLE bool connectToIed();
    Q_INVOKABLE void disconnectFromIed();
    Q_INVOKABLE bool reconnect();
    Q_INVOKABLE bool browseDirectory(const QString& remoteDirectory = QString{});
    Q_INVOKABLE bool downloadFile(const QString& remotePath, const QString& localFileUrl);
    Q_INVOKABLE bool cancelOperation();
    Q_INVOKABLE bool refreshSettingGroups();
    Q_INVOKABLE bool selectSettingGroup(int row);
    Q_INVOKABLE bool activateSelectedSettingGroup(int group);
    Q_INVOKABLE QString diagnosticsText() const;

signals:
    void configurationChanged();
    void engineeringContextChanged();
    void stateChanged();
    void filesChanged();
    void transferChanged();
    void settingsChanged();
    void selectionChanged();
    void diagnosticsChanged();

private:
    enum class State : quint8 { disconnected, connecting, discovering, connected, faulted };
    struct WorkerState;

    void appendDiagnostic(const QString& text);
    void clearRemoteState();
    void adoptEngineeringInventory();
    void refreshSelectedSettingGroup();
    void setOperationBusy(bool value);
    [[nodiscard]] std::shared_ptr<std::stop_source> replaceSessionStopSource();
    std::shared_ptr<std::stop_source> replaceOperationStopSource();

    QString host_{QStringLiteral("127.0.0.1")};
    int port_{102};
    IedEngineeringContextController* engineeringContext_{};
    State state_{State::disconnected};
    bool operationBusy_{};
    QString lastError_;
    QString associationProfile_;
    QStringList diagnostics_;
    qulonglong generation_{};

    QString currentDirectory_;
    QVariantList fileEntries_;
    int filePageCount_{};

    bool downloadActive_{};
    qulonglong downloadBytes_{};
    qulonglong downloadExpectedBytes_{};
    bool downloadExpectedKnown_{};
    QString lastDownloadRemotePath_;
    QString lastDownloadLocalPath_;

    QVariantList settingGroups_;
    int selectedSettingGroupIndex_{-1};
    QVariantMap selectedSettingGroup_;

    QThreadPool ioPool_;
    std::shared_ptr<WorkerState> workerState_;
    std::shared_ptr<std::stop_source> sessionStopSource_;
    std::shared_ptr<std::stop_source> operationStopSource_;
};
