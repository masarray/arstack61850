// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedEngineeringContextController.hpp"
#include "ariec61850/scl/model.hpp"

#include <QObject>
#include <QByteArray>
#include <QStringList>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>
#include <stop_token>

class SclWorkspaceController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool loaded READ loaded NOTIFY workspaceChanged)
    Q_PROPERTY(IedEngineeringContextController* engineeringContext READ engineeringContext WRITE setEngineeringContext NOTIFY engineeringContextChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString stateText READ stateText NOTIFY stateChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY workspaceChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY workspaceChanged)
    Q_PROPERTY(QString sourceFormat READ sourceFormat NOTIFY workspaceChanged)
    Q_PROPERTY(QString editionText READ editionText NOTIFY workspaceChanged)
    Q_PROPERTY(QString namespaceUri READ namespaceUri NOTIFY workspaceChanged)
    Q_PROPERTY(QString headerId READ headerId NOTIFY workspaceChanged)
    Q_PROPERTY(QString headerVersion READ headerVersion NOTIFY workspaceChanged)
    Q_PROPERTY(QString headerRevision READ headerRevision NOTIFY workspaceChanged)
    Q_PROPERTY(int iedCount READ iedCount NOTIFY workspaceChanged)
    Q_PROPERTY(int logicalNodeCount READ logicalNodeCount NOTIFY workspaceChanged)
    Q_PROPERTY(int modelLeafCount READ modelLeafCount NOTIFY workspaceChanged)
    Q_PROPERTY(int dataSetCount READ dataSetCount NOTIFY workspaceChanged)
    Q_PROPERTY(int reportCount READ reportCount NOTIFY workspaceChanged)
    Q_PROPERTY(int gooseCount READ gooseCount NOTIFY workspaceChanged)
    Q_PROPERTY(QVariantList gooseStreams READ gooseStreams NOTIFY workspaceChanged)
    Q_PROPERTY(int smvCount READ smvCount NOTIFY workspaceChanged)
    Q_PROPERTY(bool exactSourceSaveSupported READ exactSourceSaveSupported NOTIFY workspaceChanged)
    Q_PROPERTY(bool reconstructionSupported READ reconstructionSupported NOTIFY workspaceChanged)
    Q_PROPERTY(bool editionConversionSupported READ editionConversionSupported NOTIFY workspaceChanged)
    Q_PROPERTY(bool profileConversionSupported READ profileConversionSupported NOTIFY workspaceChanged)
    Q_PROPERTY(QStringList preservationReport READ preservationReport NOTIFY workspaceChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY stateChanged)
    Q_PROPERTY(bool lastExportVerified READ lastExportVerified NOTIFY exportChanged)
    Q_PROPERTY(QString lastExportPath READ lastExportPath NOTIFY exportChanged)
    Q_PROPERTY(QString lastExportMode READ lastExportMode NOTIFY exportChanged)
    Q_PROPERTY(qulonglong generation READ generation NOTIFY stateChanged)

public:
    explicit SclWorkspaceController(QObject* parent = nullptr);
    ~SclWorkspaceController() override;

    [[nodiscard]] bool loaded() const noexcept { return document_.has_value(); }
    [[nodiscard]] IedEngineeringContextController* engineeringContext() const noexcept { return engineeringContext_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] QString stateText() const;
    [[nodiscard]] QString sourcePath() const { return sourcePath_; }
    [[nodiscard]] QString sourceName() const { return sourceName_; }
    [[nodiscard]] QString sourceFormat() const { return sourceFormat_; }
    [[nodiscard]] QString editionText() const;
    [[nodiscard]] QString namespaceUri() const;
    [[nodiscard]] QString headerId() const;
    [[nodiscard]] QString headerVersion() const;
    [[nodiscard]] QString headerRevision() const;
    [[nodiscard]] int iedCount() const noexcept;
    [[nodiscard]] int logicalNodeCount() const noexcept;
    [[nodiscard]] int modelLeafCount() const noexcept;
    [[nodiscard]] int dataSetCount() const noexcept;
    [[nodiscard]] int reportCount() const noexcept;
    [[nodiscard]] int gooseCount() const noexcept;
    [[nodiscard]] QVariantList gooseStreams() const;
    [[nodiscard]] int smvCount() const noexcept;
    [[nodiscard]] bool exactSourceSaveSupported() const noexcept { return loaded() && !sourceBytes_.isEmpty(); }
    [[nodiscard]] bool reconstructionSupported() const noexcept;
    [[nodiscard]] bool editionConversionSupported() const noexcept;
    [[nodiscard]] bool profileConversionSupported() const noexcept;
    [[nodiscard]] QStringList preservationReport() const;
    [[nodiscard]] QString lastError() const { return lastError_; }
    [[nodiscard]] bool lastExportVerified() const noexcept { return lastExportVerified_; }
    [[nodiscard]] QString lastExportPath() const { return lastExportPath_; }
    [[nodiscard]] QString lastExportMode() const { return lastExportMode_; }
    [[nodiscard]] qulonglong generation() const noexcept { return generation_; }

    void setEngineeringContext(IedEngineeringContextController* value);

    Q_INVOKABLE bool openFile(const QUrl& fileUrl);
    Q_INVOKABLE bool saveAs(const QUrl& fileUrl, const QString& targetEdition = QStringLiteral("preserve"));
    Q_INVOKABLE bool exportCanonical(const QUrl& fileUrl, const QString& targetEdition = QStringLiteral("preserve"));
    Q_INVOKABLE void cancelOperation();
    Q_INVOKABLE QString diagnosticsText() const;

signals:
    void stateChanged();
    void engineeringContextChanged();
    void workspaceChanged();
    void exportChanged();
    void diagnosticsChanged();

private:
    [[nodiscard]] std::shared_ptr<std::stop_source> replaceStopSource();
    void appendDiagnostic(const QString& text);
    void setFailure(const QString& text);

    std::optional<ar::iec61850::scl::SclDocument> document_;
    IedEngineeringContextController* engineeringContext_{};
    QByteArray sourceBytes_;
    QString sourcePath_;
    QString sourceName_;
    QString sourceFormat_;
    bool busy_{};
    QString operationText_;
    QString lastError_;
    bool lastExportVerified_{};
    QString lastExportPath_;
    QString lastExportMode_;
    QStringList diagnostics_;
    qulonglong generation_{};
    QThreadPool ioPool_;
    std::shared_ptr<std::stop_source> stopSource_;
};
