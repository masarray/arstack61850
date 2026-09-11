// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeviceController.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QTimer>

#include <cmath>
#include <limits>

#ifndef ARSTACK_STUDIO_VERSION
#define ARSTACK_STUDIO_VERSION "0.1.0"
#endif

class StudioDeviceController : public DeviceController {
    Q_OBJECT

public:
    explicit StudioDeviceController(QObject* parent = nullptr) : DeviceController(parent) {
        liveFlushTimer_.setSingleShot(true);
        liveFlushTimer_.setInterval(20);
        connect(&liveFlushTimer_, &QTimer::timeout, this, [this] {
            static_cast<void>(flushLiveCommands());
        });

        sessionHeartbeatTimer_.setSingleShot(false);
        sessionHeartbeatTimer_.setInterval(700);
        connect(&sessionHeartbeatTimer_, &QTimer::timeout, this, [this] {
            if (!deviceVerified() || !connected() || !currentFirmwareIdentitySeen()) {
                sessionHeartbeatTimer_.stop();
                return;
            }
            if (!sendQuietCommand(QStringLiteral("HEARTBEAT")))
                sessionHeartbeatTimer_.stop();
        });

        connect(this, &DeviceController::connectedChanged, this, [this] {
            if (!connected()) {
                sessionHeartbeatTimer_.stop();
                clearPendingLiveCommands();
            }
        });
        connect(this, &DeviceController::deviceVerifiedChanged, this, [this] {
            if (!deviceVerified()) {
                sessionHeartbeatTimer_.stop();
                clearPendingLiveCommands();
                return;
            }
            ensureSessionHeartbeat();
        });
        connect(this, &DeviceController::logTextChanged, this, [this] {
            // The semantic version field arrives on the same identity line that
            // verifies the device. Start the lease only for current firmware so
            // legacy builds are not spammed with an unknown command.
            ensureSessionHeartbeat();
        });

        if (auto* app = QCoreApplication::instance(); app != nullptr) {
            connect(app, &QCoreApplication::aboutToQuit, this, [this] {
                sessionHeartbeatTimer_.stop();
                clearPendingLiveCommands();
                if (connected() && running()) {
                    static_cast<void>(DeviceController::stop());
                }
            });
        }
    }

    [[nodiscard]] bool currentFirmwareIdentitySeen() const {
        const QString log = logText();
        const qsizetype identityPos = log.lastIndexOf(QStringLiteral("ARSTACK identity"), -1, Qt::CaseInsensitive);
        if (identityPos < 0) return false;
        qsizetype lineEnd = log.indexOf(QLatin1Char('\n'), identityPos);
        if (lineEnd < 0) lineEnd = log.size();
        const QString identityLine = log.mid(identityPos, lineEnd - identityPos);
        static const QRegularExpression versionExpression{
            QStringLiteral(R"(\bfirmware=([0-9A-Za-z._+\-]+))"),
            QRegularExpression::CaseInsensitiveOption};
        const auto match = versionExpression.match(identityLine);
        return match.hasMatch() && match.captured(1) == QStringLiteral(ARSTACK_STUDIO_VERSION);
    }

    Q_INVOKABLE bool start() override {
        if (!deviceVerified()) {
            emit deviceMessage(QStringLiteral("Connect and verify the ARStack ESP32-P4 before Start."));
            return false;
        }
        if (protocolVersion() != QStringLiteral("1") || !currentFirmwareIdentitySeen()) {
            emit deviceMessage(QStringLiteral("Firmware update required before Start."));
            return false;
        }
        if (!profileArmed() || profileDeploying()) {
            emit deviceMessage(QStringLiteral("ARStack Studio is still preparing the 4I+4V profile."));
            return false;
        }

        // Refresh the firmware lease synchronously so a STOP -> immediate START
        // cannot have a short unprotected RUN window while waiting for the next
        // periodic heartbeat tick.
        if (!sendQuietCommand(QStringLiteral("HEARTBEAT"))) {
            emit deviceMessage(QStringLiteral("Control session could not be established before Start."));
            return false;
        }
        if (!sessionHeartbeatTimer_.isActive()) sessionHeartbeatTimer_.start();

        if (!flushLiveCommands()) {
            emit deviceMessage(QStringLiteral("Latest injection values could not be sent before Start."));
            return false;
        }
        return DeviceController::start();
    }

    Q_INVOKABLE bool zero() override {
        pendingSignals_.clear();
        if (!frequencyPending_) liveFlushTimer_.stop();
        return DeviceController::zero();
    }

    Q_INVOKABLE bool setFrequency(const double hz) override {
        if (!std::isfinite(hz) || hz < 0.0 || hz > 1000.0) {
            emit deviceMessage(QStringLiteral("Frequency must be within 0..1000 Hz."));
            return false;
        }
        if (!deviceVerified()) return false;

        requestedFrequencyHz_ = hz;
        pendingFrequencyHz_ = hz;
        frequencyPending_ = true;
        scheduleLiveFlush();
        return true;
    }

    Q_INVOKABLE bool setSignal(
        const QString& signalId,
        const double magnitude,
        const double phaseDegrees,
        const quint32 quality,
        const double currentCountsPerAmp,
        const double voltageCountsPerVolt) override {
        const QString id = signalId.trimmed().toUpper();
        static const QStringList validIds{
            QStringLiteral("IA"), QStringLiteral("IB"), QStringLiteral("IC"), QStringLiteral("IN"),
            QStringLiteral("UA"), QStringLiteral("UB"), QStringLiteral("UC"), QStringLiteral("UN")};
        if (!deviceVerified() || !validIds.contains(id) || !std::isfinite(magnitude) ||
            !std::isfinite(phaseDegrees) || !std::isfinite(currentCountsPerAmp) ||
            !std::isfinite(voltageCountsPerVolt) || currentCountsPerAmp <= 0.0 || voltageCountsPerVolt <= 0.0 ||
            (requestedFrequencyHz_ > 0.0 && magnitude < 0.0)) {
            return false;
        }

        const double scale = id.startsWith(QLatin1Char('I')) ? currentCountsPerAmp : voltageCountsPerVolt;
        const double counts = magnitude * scale;
        const double phaseMdeg = phaseDegrees * 1000.0;
        if (counts < static_cast<double>(std::numeric_limits<qint32>::min()) ||
            counts > static_cast<double>(std::numeric_limits<qint32>::max()) ||
            phaseMdeg < static_cast<double>(std::numeric_limits<qint32>::min()) ||
            phaseMdeg > static_cast<double>(std::numeric_limits<qint32>::max())) {
            return false;
        }

        pendingSignals_.insert(id, PendingSignal{
            magnitude,
            phaseDegrees,
            quality,
            currentCountsPerAmp,
            voltageCountsPerVolt});
        scheduleLiveFlush();
        return true;
    }

    Q_INVOKABLE bool deployProfile(const QVariantMap& profile) override {
        if (protocolVersion() != QStringLiteral("1") || !currentFirmwareIdentitySeen()) {
            emit deviceMessage(QStringLiteral("Firmware update required before profile synchronization."));
            return false;
        }
        return DeviceController::deployProfile(profile);
    }

private:
    struct PendingSignal {
        double magnitude{};
        double phaseDegrees{};
        quint32 quality{};
        double currentCountsPerAmp{};
        double voltageCountsPerVolt{};
    };

    void ensureSessionHeartbeat() {
        if (!deviceVerified() || !connected() || !currentFirmwareIdentitySeen()) return;
        if (!sessionHeartbeatTimer_.isActive()) {
            if (sendQuietCommand(QStringLiteral("HEARTBEAT")))
                sessionHeartbeatTimer_.start();
        }
    }

    void scheduleLiveFlush() {
        if (!liveFlushTimer_.isActive()) liveFlushTimer_.start();
    }

    void clearPendingLiveCommands() {
        liveFlushTimer_.stop();
        pendingSignals_.clear();
        frequencyPending_ = false;
    }

    bool flushLiveCommands() {
        liveFlushTimer_.stop();
        if (!deviceVerified()) {
            clearPendingLiveCommands();
            return false;
        }

        bool ok = true;
        if (frequencyPending_) {
            const double frequency = pendingFrequencyHz_;
            frequencyPending_ = false;
            ok = DeviceController::setFrequency(frequency) && ok;
        }

        static const QStringList order{
            QStringLiteral("IA"), QStringLiteral("IB"), QStringLiteral("IC"), QStringLiteral("IN"),
            QStringLiteral("UA"), QStringLiteral("UB"), QStringLiteral("UC"), QStringLiteral("UN")};
        for (const QString& id : order) {
            const auto it = pendingSignals_.find(id);
            if (it == pendingSignals_.end()) continue;
            const PendingSignal value = it.value();
            pendingSignals_.erase(it);
            ok = DeviceController::setSignal(
                id,
                value.magnitude,
                value.phaseDegrees,
                value.quality,
                value.currentCountsPerAmp,
                value.voltageCountsPerVolt) && ok;
        }
        return ok;
    }

    QTimer liveFlushTimer_;
    QTimer sessionHeartbeatTimer_;
    QHash<QString, PendingSignal> pendingSignals_;
    double requestedFrequencyHz_{50.0};
    double pendingFrequencyHz_{50.0};
    bool frequencyPending_{false};
};
