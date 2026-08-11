// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/scl/model.hpp"

#include <QObject>
#include <QProcess>
#include <QUrl>
#include <QVariantList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <optional>
#include <vector>

class IedSimulatorController : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool imported READ imported NOTIFY modelChanged)
    Q_PROPERTY(bool running READ running NOTIFY runtimeChanged)
    Q_PROPERTY(bool starting READ starting NOTIFY runtimeChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY modelChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY modelChanged)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY modelChanged)
    Q_PROPERTY(QString fatalError READ fatalError NOTIFY modelChanged)
    Q_PROPERTY(QString listenAddress READ listenAddress WRITE setListenAddress NOTIFY configurationChanged)
    Q_PROPERTY(QStringList availableAddresses READ availableAddresses CONSTANT)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY configurationChanged)
    Q_PROPERTY(bool gooseEnabled READ gooseEnabled WRITE setGooseEnabled NOTIFY configurationChanged)
    Q_PROPERTY(bool fileServiceEnabled READ fileServiceEnabled WRITE setFileServiceEnabled NOTIFY configurationChanged)
    Q_PROPERTY(QString fileFolder READ fileFolder WRITE setFileFolder NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList ieds READ ieds NOTIFY modelChanged)
    Q_PROPERTY(int selectedIedIndex READ selectedIedIndex WRITE selectIed NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedIed READ selectedIed NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged)
    Q_PROPERTY(int selectedValueIndex READ selectedValueIndex WRITE selectValue NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedValue READ selectedValue NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList activity READ activity NOTIFY activityChanged)
    Q_PROPERTY(int logicalDeviceCount READ logicalDeviceCount NOTIFY modelChanged)
    Q_PROPERTY(int dataObjectCount READ dataObjectCount NOTIFY modelChanged)
    Q_PROPERTY(int dataAttributeCount READ dataAttributeCount NOTIFY modelChanged)
    Q_PROPERTY(int dataSetCount READ dataSetCount NOTIFY modelChanged)
    Q_PROPERTY(int reportCount READ reportCount NOTIFY modelChanged)
    Q_PROPERTY(int gooseCount READ gooseCount NOTIFY modelChanged)

public:
    explicit IedSimulatorController(QObject* parent = nullptr);
    ~IedSimulatorController() override;

    [[nodiscard]] bool imported() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool starting() const noexcept;
    [[nodiscard]] QString sourceName() const;
    [[nodiscard]] QString sourcePath() const;
    [[nodiscard]] QString modelStatus() const;
    [[nodiscard]] QString fatalError() const;
    [[nodiscard]] QString listenAddress() const;
    [[nodiscard]] QStringList availableAddresses() const;
    [[nodiscard]] int port() const noexcept;
    [[nodiscard]] bool gooseEnabled() const noexcept;
    [[nodiscard]] bool fileServiceEnabled() const noexcept;
    [[nodiscard]] QString fileFolder() const;
    [[nodiscard]] QVariantList ieds() const;
    [[nodiscard]] int selectedIedIndex() const noexcept;
    [[nodiscard]] QVariantMap selectedIed() const;
    [[nodiscard]] QVariantList values() const;
    [[nodiscard]] int selectedValueIndex() const noexcept;
    [[nodiscard]] QVariantMap selectedValue() const;
    [[nodiscard]] QVariantList activity() const;
    [[nodiscard]] int logicalDeviceCount() const noexcept;
    [[nodiscard]] int dataObjectCount() const noexcept;
    [[nodiscard]] int dataAttributeCount() const noexcept;
    [[nodiscard]] int dataSetCount() const noexcept;
    [[nodiscard]] int reportCount() const noexcept;
    [[nodiscard]] int gooseCount() const noexcept;

    void setListenAddress(const QString& value);
    void setPort(int value);
    void setGooseEnabled(bool value);
    void setFileServiceEnabled(bool value);
    void setFileFolder(const QString& value);

    Q_INVOKABLE bool loadFile(const QUrl& fileUrl);
    Q_INVOKABLE bool addFile(const QUrl& fileUrl);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectIed(int index);
    Q_INVOKABLE void selectValue(int index);
    Q_INVOKABLE bool startSimulation();
    Q_INVOKABLE void stopSimulation();
    Q_INVOKABLE bool applySelectedValue(
        const QString& value,
        const QString& quality,
        const QString& origin);
    Q_INVOKABLE bool undoLastChange();
    Q_INVOKABLE void clearActivity();

signals:
    void modelChanged();
    void configurationChanged();
    void runtimeChanged();
    void selectionChanged();
    void valuesChanged();
    void activityChanged();

private:
    struct LoadedDocument final {
        QString path;
        ar::iec61850::scl::SclDocument document;
    };

    struct ValueSnapshot final {
        int iedIndex{-1};
        int valueIndex{-1};
        QVariantMap value;
    };

    bool importFile(const QUrl& fileUrl, bool append);
    void rebuildPresentation();
    void rebuildValues();
    void appendActivity(
        const QString& category,
        const QString& message,
        const QString& severity = QStringLiteral("Info"));
    void setRuntimeState(bool running, bool starting);
    [[nodiscard]] QString serverExecutable() const;
    [[nodiscard]] static QVariantMap valueMap(
        const ar::iec61850::scl::SclDataSetEntry& entry);

    std::vector<LoadedDocument> documents_;
    QVariantList ieds_;
    QVariantList values_;
    QVariantList activity_;
    std::optional<ValueSnapshot> previousValue_;
    QProcess serverProcess_;
    QString sourceName_;
    QString sourcePath_;
    QString fatalError_;
    QString listenAddress_{QStringLiteral("0.0.0.0")};
    QString fileFolder_;
    int port_{102};
    int selectedIedIndex_{-1};
    int selectedValueIndex_{-1};
    int logicalDeviceCount_{};
    int dataObjectCount_{};
    int dataAttributeCount_{};
    int dataSetCount_{};
    int reportCount_{};
    int gooseCount_{};
    bool running_{};
    bool starting_{};
    bool gooseEnabled_{true};
    bool fileServiceEnabled_{};
};
