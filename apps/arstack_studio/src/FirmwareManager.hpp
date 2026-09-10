// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class FirmwareManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool bundleReady READ bundleReady NOTIFY stateChanged)
    Q_PROPERTY(bool flasherAvailable READ flasherAvailable NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(bool targetVerified READ targetVerified NOTIFY stateChanged)
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

    [[nodiscard]] bool bundleReady() const noexcept { return bundleReady_; }
    [[nodiscard]] bool flasherAvailable() const noexcept { return flasherAvailable_; }
    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] bool targetVerified() const noexcept { return targetVerified_; }
    [[nodiscard]] QString selectedPort() const { return selectedPort_; }
    [[nodiscard]] QString targetChip() const { return targetChip_; }
    [[nodiscard]] QString firmwareVersion() const { return firmwareVersion_; }
    [[nodiscard]] QString expectedProtocol() const { return expectedProtocol_; }
    [[nodiscard]] QString firmwareSha256() const { return firmwareSha256_; }
    [[nodiscard]] QString status() const { return status_; }
    [[nodiscard]] QString bundleStatus() const { return bundleStatus_; }
    [[nodiscard]] QString logText() const { return logText_; }

    Q_INVOKABLE void refreshBundle();
    Q_INVOKABLE bool probeTarget(const QString& portName);
    Q_INVOKABLE bool installFirmware(const QString& portName);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void clearLog();

signals:
    void stateChanged();
    void logChanged();
    void installationFinished(bool resetSucceeded);

private:
    enum class Operation { none, probe, flash, reset };

    [[nodiscard]] QString bundleRoot() const;
    [[nodiscard]] QString flasherPath() const;
    bool loadManifest();
    bool startEspflash(const QStringList& arguments, Operation operation);
    void finishOperation(int exitCode, QProcess::ExitStatus exitStatus);
    void appendLog(const QString& text);
    void setStatus(const QString& text);
    void fail(const QString& text);

    QProcess process_;
    Operation operation_{Operation::none};
    QString operationOutput_;
    QString selectedPort_;
    QString targetChip_{QStringLiteral("Not probed")};
    QString firmwareVersion_{QStringLiteral("-")};
    QString expectedProtocol_{QStringLiteral("-")};
    QString firmwareSha256_;
    QString firmwareImagePath_;
    QString status_{QStringLiteral("Firmware Manager ready")};
    QString bundleStatus_{QStringLiteral("Checking firmware package...")};
    QString logText_;
    bool bundleReady_{false};
    bool flasherAvailable_{false};
    bool busy_{false};
    bool targetVerified_{false};
};
