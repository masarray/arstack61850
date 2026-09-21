// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedEngineeringContextController.hpp"

#include <QObject>
#include <QThreadPool>
#include <QVariantMap>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <stop_token>

class MmsControlController final : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString host READ host WRITE setHost NOTIFY configurationChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY configurationChanged)
    Q_PROPERTY(IedEngineeringContextController* engineeringContext READ engineeringContext WRITE setEngineeringContext NOTIFY engineeringContextChanged)
    Q_PROPERTY(bool connected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool prepared READ prepared NOTIFY stateChanged)
    Q_PROPERTY(QString objectReference READ objectReference NOTIFY stateChanged)
    Q_PROPERTY(QString modelName READ modelName NOTIFY stateChanged)
    Q_PROPERTY(QString cdc READ cdc NOTIFY stateChanged)
    Q_PROPERTY(bool requiresSelect READ requiresSelect NOTIFY stateChanged)
    Q_PROPERTY(bool enhanced READ enhanced NOTIFY stateChanged)
    Q_PROPERTY(bool activeSelection READ activeSelection NOTIFY stateChanged)
    Q_PROPERTY(bool supportsCancel READ supportsCancel NOTIFY stateChanged)
    Q_PROPERTY(bool supportsTimeActivated READ supportsTimeActivated NOTIFY stateChanged)
    Q_PROPERTY(bool supportsCommandTermination READ supportsCommandTermination NOTIFY stateChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(QString lastStatus READ lastStatus NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap lastResult READ lastResult NOTIFY resultChanged)
    Q_PROPERTY(QStringList discoveryEvidence READ discoveryEvidence NOTIFY stateChanged)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY stateChanged)

public:
    explicit MmsControlController(QObject* parent = nullptr);
    ~MmsControlController() override;

    [[nodiscard]] QString host() const { return host_; }
    [[nodiscard]] int port() const noexcept { return port_; }
    void setHost(const QString& value);
    void setPort(int value);
    [[nodiscard]] IedEngineeringContextController* engineeringContext() const noexcept { return engineeringContext_; }
    void setEngineeringContext(IedEngineeringContextController* value);

    [[nodiscard]] bool connected() const noexcept { return connected_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] bool prepared() const noexcept { return prepared_; }
    [[nodiscard]] QString objectReference() const { return objectReference_; }
    [[nodiscard]] QString modelName() const { return modelName_; }
    [[nodiscard]] QString cdc() const { return cdc_; }
    [[nodiscard]] bool requiresSelect() const noexcept { return requiresSelect_; }
    [[nodiscard]] bool enhanced() const noexcept { return enhanced_; }
    [[nodiscard]] bool activeSelection() const noexcept { return activeSelection_; }
    [[nodiscard]] bool supportsCancel() const noexcept { return supportsCancel_; }
    [[nodiscard]] bool supportsTimeActivated() const noexcept { return supportsTimeActivated_; }
    [[nodiscard]] bool supportsCommandTermination() const noexcept { return supportsCommandTermination_; }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] QString lastStatus() const { return lastStatus_; }
    [[nodiscard]] QVariantMap lastResult() const { return lastResult_; }
    [[nodiscard]] QStringList discoveryEvidence() const { return discoveryEvidence_; }
    [[nodiscard]] qulonglong generation() const noexcept { return generation_; }

    Q_INVOKABLE bool prepareObject(const QString& objectReference);
    Q_INVOKABLE bool select(
        const QString& valueKind,
        const QString& value,
        int originCategory,
        const QString& originIdentifier,
        bool test,
        bool interlockCheck,
        bool synchroCheck);
    Q_INVOKABLE bool operate(
        const QString& valueKind,
        const QString& value,
        int originCategory,
        const QString& originIdentifier,
        bool test,
        bool interlockCheck,
        bool synchroCheck,
        bool autoSelect);
    Q_INVOKABLE bool selectAndOperate(
        const QString& valueKind,
        const QString& value,
        int originCategory,
        const QString& originIdentifier,
        bool test,
        bool interlockCheck,
        bool synchroCheck);
    Q_INVOKABLE bool cancel();
    Q_INVOKABLE void disconnectFromIed();

signals:
    void configurationChanged();
    void engineeringContextChanged();
    void stateChanged();
    void resultChanged();

private:
    struct WorkerState;

    enum class RequestedAction : quint8 {
        select,
        operate,
        selectAndOperate,
        cancel
    };

    bool startAction(
        RequestedAction action,
        const QString& valueKind,
        const QString& value,
        int originCategory,
        const QString& originIdentifier,
        bool test,
        bool interlockCheck,
        bool synchroCheck,
        bool autoSelect);
    void resetDescriptorUi();
    [[nodiscard]] std::shared_ptr<std::stop_source> replaceStopSource();

    QString host_{QStringLiteral("127.0.0.1")};
    int port_{102};
    IedEngineeringContextController* engineeringContext_{};
    bool connected_{};
    bool busy_{};
    bool prepared_{};
    QString objectReference_;
    QString modelName_;
    QString cdc_;
    bool requiresSelect_{};
    bool enhanced_{};
    bool activeSelection_{};
    bool supportsCancel_{};
    bool supportsTimeActivated_{};
    bool supportsCommandTermination_{};
    QString lastError_;
    QString lastStatus_;
    QVariantMap lastResult_;
    QStringList discoveryEvidence_;
    qulonglong generation_{};
    QThreadPool ioPool_;
    std::shared_ptr<WorkerState> workerState_;
    std::shared_ptr<std::stop_source> stopSource_;
};
