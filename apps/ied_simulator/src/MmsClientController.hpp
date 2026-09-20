// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MmsLiveTreeModel.hpp"
#include "IedEngineeringContextController.hpp"

#include <QObject>
#include <QThreadPool>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <stop_token>

class MmsClientController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY configurationChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY configurationChanged)
    Q_PROPERTY(QString trustedSclPath READ trustedSclPath WRITE setTrustedSclPath NOTIFY configurationChanged)
    Q_PROPERTY(IedEngineeringContextController* engineeringContext READ engineeringContext WRITE setEngineeringContext NOTIFY engineeringContextChanged)
    Q_PROPERTY(bool trustedSclAvailable READ trustedSclAvailable NOTIFY configurationChanged)
    Q_PROPERTY(QString trustedSclHealth READ trustedSclHealth NOTIFY stateChanged)
    Q_PROPERTY(bool trustedSclDegraded READ trustedSclDegraded NOTIFY stateChanged)
    Q_PROPERTY(bool trustedSclIncompatible READ trustedSclIncompatible NOTIFY stateChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool operationBusy READ operationBusy NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY stateChanged)
    Q_PROPERTY(QString iedName READ iedName NOTIFY modelChanged)
    Q_PROPERTY(QString modelSummary READ modelSummary NOTIFY modelChanged)
    Q_PROPERTY(QString modelSource READ modelSource NOTIFY modelChanged)
    Q_PROPERTY(QString associationProfile READ associationProfile NOTIFY modelChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString lastDiagnostic READ lastDiagnostic NOTIFY diagnosticsChanged)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY stateChanged)
    Q_PROPERTY(int logicalDeviceCount READ logicalDeviceCount NOTIFY modelChanged)
    Q_PROPERTY(int logicalNodeCount READ logicalNodeCount NOTIFY modelChanged)
    Q_PROPERTY(int dataObjectCount READ dataObjectCount NOTIFY modelChanged)
    Q_PROPERTY(int dataAttributeCount READ dataAttributeCount NOTIFY modelChanged)
    Q_PROPERTY(MmsLiveTreeModel* treeModel READ treeModel CONSTANT)

public:
    explicit MmsClientController(QObject* parent = nullptr);
    ~MmsClientController() override;

    [[nodiscard]] QString host() const { return host_; }
    [[nodiscard]] int port() const noexcept { return port_; }
    [[nodiscard]] QString trustedSclPath() const { return trustedSclPath_; }
    [[nodiscard]] bool trustedSclAvailable() const noexcept { return !trustedSclPath_.isEmpty(); }
    [[nodiscard]] IedEngineeringContextController* engineeringContext() const noexcept { return engineeringContext_; }
    [[nodiscard]] QString trustedSclHealth() const {
        if (lastError_.contains(QStringLiteral("SCL online identity incompatible"), Qt::CaseInsensitive)) {
            return QStringLiteral("incompatible");
        }
        if (modelSource_ != QStringLiteral("Trusted SCL + initial snapshot")) return {};
        if (modelSummary_.contains(QStringLiteral("Trusted SCL [degraded]"), Qt::CaseInsensitive)) {
            return QStringLiteral("degraded");
        }
        if (modelSummary_.contains(QStringLiteral("Trusted SCL [matched]"), Qt::CaseInsensitive)) {
            return QStringLiteral("matched");
        }
        return {};
    }
    [[nodiscard]] bool trustedSclDegraded() const {
        return trustedSclHealth() == QStringLiteral("degraded");
    }
    [[nodiscard]] bool trustedSclIncompatible() const {
        return trustedSclHealth() == QStringLiteral("incompatible");
    }
    void setHost(const QString& value);
    void setPort(int value);
    void setTrustedSclPath(const QString& value);
    void setEngineeringContext(IedEngineeringContextController* value);

    [[nodiscard]] bool connected() const noexcept;
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] bool operationBusy() const noexcept { return operationBusy_; }
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString endpoint() const;
    [[nodiscard]] QString iedName() const { return iedName_; }
    [[nodiscard]] QString modelSummary() const { return modelSummary_; }
    [[nodiscard]] QString modelSource() const { return modelSource_; }
    [[nodiscard]] QString associationProfile() const { return associationProfile_; }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] QString lastDiagnostic() const;
    [[nodiscard]] qulonglong generation() const noexcept { return generation_; }
    [[nodiscard]] int logicalDeviceCount() const noexcept { return logicalDeviceCount_; }
    [[nodiscard]] int logicalNodeCount() const noexcept { return logicalNodeCount_; }
    [[nodiscard]] int dataObjectCount() const noexcept { return dataObjectCount_; }
    [[nodiscard]] int dataAttributeCount() const noexcept { return dataAttributeCount_; }
    [[nodiscard]] MmsLiveTreeModel* treeModel() noexcept { return &treeModel_; }

    Q_INVOKABLE bool connectToIed();
    Q_INVOKABLE void disconnectFromIed();
    Q_INVOKABLE bool reconnect();
    Q_INVOKABLE bool readSelected();
    Q_INVOKABLE bool refreshVisible(int firstRow, int lastRow);
    Q_INVOKABLE bool writeSelected(const QString& textValue);
    Q_INVOKABLE bool readEngineeringSelected();
    Q_INVOKABLE bool refreshEngineeringVisible(int firstRow, int lastRow);
    Q_INVOKABLE bool writeEngineeringSelected(const QString& textValue);
    Q_INVOKABLE QString diagnosticsText() const;

signals:
    void configurationChanged();
    void engineeringContextChanged();
    void stateChanged();
    void modelChanged();
    void diagnosticsChanged();

private:
    enum class State : quint8 { disconnected, connecting, discovering, connected, faulted };
    struct WorkerState;

    void startRead(QVector<MmsLiveTreeModel::ReadTarget> targets, QString operationName);
    void appendDiagnostic(const QString& text);
    void clearModelState();
    void setOperationBusy(bool value);
    [[nodiscard]] std::shared_ptr<std::stop_source> replaceStopSource();

    QString host_{QStringLiteral("127.0.0.1")};
    int port_{102};
    QString trustedSclPath_;
    IedEngineeringContextController* engineeringContext_{};
    State state_{State::disconnected};
    bool operationBusy_{};
    QString iedName_;
    QString modelSummary_;
    QString modelSource_;
    QString associationProfile_;
    QString lastError_;
    QStringList diagnostics_;
    qulonglong generation_{};
    int logicalDeviceCount_{};
    int logicalNodeCount_{};
    int dataObjectCount_{};
    int dataAttributeCount_{};
    MmsLiveTreeModel treeModel_;
    QThreadPool ioPool_;
    std::shared_ptr<WorkerState> workerState_;
    std::shared_ptr<std::stop_source> stopSource_;
};
