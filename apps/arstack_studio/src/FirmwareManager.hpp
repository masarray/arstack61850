// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QThread>

class FirmwareWorker;

class FirmwareManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool bundleReady READ bundleReady NOTIFY stateChanged)
    Q_PROPERTY(bool flasherAvailable READ flasherAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool targetVerified READ targetVerified NOTIFY stateChanged)
    Q_PROPERTY(bool bootloaderHelpNeeded READ bootloaderHelpNeeded NOTIFY stateChanged)
    Q_PROPERTY(int flashProgress READ flashProgress NOTIFY stateChanged)
    Q_PROPERTY(QString selectedPort READ selectedPort NOTIFY stateChanged)
    Q_PROPERTY(QString targetChip READ targetChip NOTIFY stateChanged)
    Q_PROPERTY(QString firmwareVersion READ firmwareVersion NOTIFY stateChanged)
    Q_PROPERTY(QString expectedProtocol READ expectedProtocol NOTIFY stateChanged)
    Q_PROPERTY(QString firmwareSha256 READ firmwareSha256 NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString bundleStatus READ bundleStatus NOTIFY stateChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY logChanged)

public:
    explicit FirmwareManager(QObject* parent = nullptr);
    ~FirmwareManager() override;

    [[nodiscard]] bool bundleReady() const noexcept { return bundleReady_; }
    [[nodiscard]] bool flasherAvailable() const noexcept { return flasherAvailable_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] bool targetVerified() const noexcept { return targetVerified_; }
    [[nodiscard]] bool bootloaderHelpNeeded() const noexcept { return bootloaderHelpNeeded_; }
    [[nodiscard]] int flashProgress() const noexcept { return flashProgress_; }
    [[nodiscard]] QString selectedPort() const { return selectedPort_; }
    [[nodiscard]] QString targetChip() const { return targetChip_; }
    [[nodiscard]] QString firmwareVersion() const { return firmwareVersion_; }
    [[nodiscard]] QString expectedProtocol() const { return expectedProtocol_; }
    [[nodiscard]] QString firmwareSha256() const { return firmwareSha256_; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] QString bundleStatus() const { return bundleStatus_; }
    [[nodiscard]] QString logText() const { return logText_; }
    [[nodiscard]] quint64 sessionGeneration() const noexcept { return sessionGeneration_; }
    [[nodiscard]] bool workerReady() const noexcept { return workerReady_; }
    [[nodiscard]] bool workerAffinityValid() const noexcept { return workerAffinityValid_; }

    [[nodiscard]] static bool parseEsp32P4Revision(const QString& output, int& major, int& minor);
    [[nodiscard]] static bool supportsEsp32P4Revision(int major, int minor) noexcept;
    [[nodiscard]] static int parseFlashProgress(const QString& output);
    [[nodiscard]] static constexpr int launchTimeoutMs() noexcept { return 15000; }
    [[nodiscard]] static constexpr int probeTimeoutMs() noexcept { return 30000; }
    [[nodiscard]] static constexpr int flashTimeoutMs() noexcept { return 180000; }
    [[nodiscard]] static constexpr int resetTimeoutMs() noexcept { return 20000; }
    [[nodiscard]] static constexpr bool workerEventIsCurrent(
        const quint64 activeGeneration,
        const quint64 eventGeneration) noexcept {
        return activeGeneration != 0 && eventGeneration == activeGeneration;
    }

    void setSessionGeneration(quint64 generation) noexcept { sessionGeneration_ = generation; }

    Q_INVOKABLE void refreshBundle();
    Q_INVOKABLE bool probeTarget(const QString& portName);
    Q_INVOKABLE bool installFirmware(const QString& portName);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void shutdown();
    Q_INVOKABLE void clearLog();

signals:
    void stateChanged();
    void logChanged();
    void installationFinished(bool resetSucceeded);
    void operationFailed(const QString& message, bool bootloaderHelpNeeded);

private:
    enum class Operation { none, probe, flash, reset };

    [[nodiscard]] QString bundleRoot() const;
    [[nodiscard]] QString flasherPath() const;
    bool loadManifest();
    bool startEspflash(const QStringList& arguments, Operation operation);
    int operationTimeoutMs(Operation operation) const noexcept;
    void connectWorkerSignals();
    void finishOperation(int exitCode, bool normalExit);
    void handleLaunchFailure(const QString& message, bool timeout);
    void handleOperationTimeout();
    void handleOperationRejected(const QString& message);
    void updateProgressFromOutput(const QString& text);
    void appendOperationOutput(const QString& text);
    void appendLog(const QString& text);
    void setStatus(const QString& text);
    void fail(const QString& text);

    FirmwareWorker* worker_{nullptr};
    QThread workerThread_;
    Operation operation_{Operation::none};
    QString operationOutput_;
    QString selectedPort_;
    QString targetChip_{QStringLiteral("Not checked")};
    QString firmwareVersion_{QStringLiteral("-")};
    QString expectedProtocol_{QStringLiteral("-")};
    QString revisionPolicy_;
    QString firmwareSha256_;
    QString firmwareImagePath_;
    QString status_{QStringLiteral("Firmware setup ready")};
    QString bundleStatus_{QStringLiteral("Checking firmware package...")};
    QString logText_;
    quint64 sessionGeneration_{1};
    quint64 activeOperationGeneration_{0};
    int flashProgress_{-1};
    bool bundleReady_{false};
    bool flasherAvailable_{false};
    bool busy_{false};
    bool targetVerified_{false};
    bool bootloaderHelpNeeded_{false};
    bool cancelRequested_{false};
    bool shuttingDown_{false};
    bool workerReady_{false};
    bool workerAffinityValid_{false};
};
