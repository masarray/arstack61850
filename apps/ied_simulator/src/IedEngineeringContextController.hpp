// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "MmsLiveTreeModel.hpp"

#include "ariec61850/mms/live_model.hpp"
#include "ariec61850/scl/model.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>

class IedEngineeringContextController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool hasSource READ hasSource NOTIFY contextChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY contextChanged)
    Q_PROPERTY(bool selectionRequired READ selectionRequired NOTIFY contextChanged)
    Q_PROPERTY(QString authority READ authority NOTIFY contextChanged)
    Q_PROPERTY(QString authorityKey READ authorityKey NOTIFY contextChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY contextChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY contextChanged)
    Q_PROPERTY(QString iedName READ iedName NOTIFY contextChanged)
    Q_PROPERTY(QStringList candidateIeds READ candidateIeds NOTIFY contextChanged)
    Q_PROPERTY(QString accessPointName READ accessPointName NOTIFY contextChanged)
    Q_PROPERTY(QString endpoint READ endpoint NOTIFY contextChanged)
    Q_PROPERTY(QString endpointHost READ endpointHost NOTIFY contextChanged)
    Q_PROPERTY(int endpointPort READ endpointPort NOTIFY contextChanged)
    Q_PROPERTY(QString structuralFingerprint READ structuralFingerprint NOTIFY contextChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY contextChanged)
    Q_PROPERTY(bool online READ online NOTIFY runtimeChanged)
    Q_PROPERTY(QString runtimeEndpoint READ runtimeEndpoint NOTIFY runtimeChanged)
    Q_PROPERTY(qulonglong contextGeneration READ contextGeneration NOTIFY contextChanged)
    Q_PROPERTY(qulonglong runtimeGeneration READ runtimeGeneration NOTIFY runtimeChanged)
    Q_PROPERTY(int logicalDeviceCount READ logicalDeviceCount NOTIFY contextChanged)
    Q_PROPERTY(int logicalNodeCount READ logicalNodeCount NOTIFY contextChanged)
    Q_PROPERTY(int dataObjectCount READ dataObjectCount NOTIFY contextChanged)
    Q_PROPERTY(int dataAttributeCount READ dataAttributeCount NOTIFY contextChanged)
    Q_PROPERTY(int dataSetCount READ dataSetCount NOTIFY contextChanged)
    Q_PROPERTY(int reportCount READ reportCount NOTIFY contextChanged)
    Q_PROPERTY(int gooseCount READ gooseCount NOTIFY contextChanged)
    Q_PROPERTY(int sampledValueCount READ sampledValueCount NOTIFY contextChanged)
    Q_PROPERTY(int settingGroupCount READ settingGroupCount NOTIFY contextChanged)
    Q_PROPERTY(MmsLiveTreeModel* treeModel READ treeModel CONSTANT)

public:
    explicit IedEngineeringContextController(QObject* parent = nullptr);

    [[nodiscard]] bool hasSource() const noexcept { return authority_ != Authority::none; }
    [[nodiscard]] bool loaded() const noexcept { return static_cast<bool>(model_); }
    [[nodiscard]] bool selectionRequired() const noexcept { return selectionRequired_; }
    [[nodiscard]] QString authority() const;
    [[nodiscard]] QString authorityKey() const;
    [[nodiscard]] QString sourcePath() const { return sourcePath_; }
    [[nodiscard]] QString sourceName() const { return sourceName_; }
    [[nodiscard]] QString iedName() const { return iedName_; }
    [[nodiscard]] QStringList candidateIeds() const { return candidateIeds_; }
    [[nodiscard]] QString accessPointName() const { return accessPointName_; }
    [[nodiscard]] QString endpoint() const { return endpoint_; }
    [[nodiscard]] QString endpointHost() const {
        return model_ ? QString::fromStdString(model_->endpoint.host) : QString{};
    }
    [[nodiscard]] int endpointPort() const noexcept {
        return model_ ? static_cast<int>(model_->endpoint.port) : 102;
    }
    [[nodiscard]] QString structuralFingerprint() const { return structuralFingerprint_; }
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] bool online() const noexcept { return online_; }
    [[nodiscard]] QString runtimeEndpoint() const { return runtimeEndpoint_; }
    [[nodiscard]] qulonglong contextGeneration() const noexcept { return contextGeneration_; }
    [[nodiscard]] qulonglong runtimeGeneration() const noexcept { return runtimeGeneration_; }

    [[nodiscard]] int logicalDeviceCount() const noexcept;
    [[nodiscard]] int logicalNodeCount() const noexcept;
    [[nodiscard]] int dataObjectCount() const noexcept;
    [[nodiscard]] int dataAttributeCount() const noexcept;
    [[nodiscard]] int dataSetCount() const noexcept;
    [[nodiscard]] int reportCount() const noexcept;
    [[nodiscard]] int gooseCount() const noexcept;
    [[nodiscard]] int sampledValueCount() const noexcept;
    [[nodiscard]] int settingGroupCount() const noexcept;
    [[nodiscard]] MmsLiveTreeModel* treeModel() noexcept { return &treeModel_; }

    bool publishSclDocument(
        const ar::iec61850::scl::SclDocument& document,
        const QString& sourcePath,
        const QString& selectedIedName = {});
    void publishLiveDiscovery(const ar::iec61850::mms::MmsLiveModelDocument& model);
    void publishTrustedSclOnline(
        const ar::iec61850::mms::MmsLiveModelDocument& onlineModel,
        const QString& sourcePath);
    void setRuntimeOnline(bool online, const QString& endpoint = {});

    Q_INVOKABLE bool selectIed(const QString& iedName);
    Q_INVOKABLE void clearContext();

    [[nodiscard]] const ar::iec61850::mms::MmsLiveModelDocument* modelSnapshot() const noexcept {
        return model_.get();
    }

signals:
    void contextChanged();
    void runtimeChanged();

private:
    enum class Authority : quint8 { none, openedScl, liveDiscovery };

    bool activateSclIed(const QString& iedName);
    void applyModel(std::shared_ptr<ar::iec61850::mms::MmsLiveModelDocument> model);

    Authority authority_{Authority::none};
    std::shared_ptr<ar::iec61850::scl::SclDocument> sclSource_;
    std::shared_ptr<ar::iec61850::mms::MmsLiveModelDocument> model_;
    MmsLiveTreeModel treeModel_;

    QString sourcePath_;
    QString sourceName_;
    QString iedName_;
    QStringList candidateIeds_;
    QString accessPointName_;
    QString endpoint_;
    QString structuralFingerprint_;
    QString lastError_;
    bool selectionRequired_{};
    bool online_{};
    QString runtimeEndpoint_;
    qulonglong contextGeneration_{};
    qulonglong runtimeGeneration_{};
};
