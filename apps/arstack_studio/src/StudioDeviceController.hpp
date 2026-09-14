// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeviceController.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QTimer>

#include <cmath>
#include <limits>

#ifndef ARSTACK_STUDIO_VERSION
#define ARSTACK_STUDIO_VERSION "0.1.0"
#endif

class DeterministicSessionHarness;

class StudioDeviceController : public DeviceController {
    Q_OBJECT
    Q_PROPERTY(bool controlResponsive READ controlResponsive NOTIFY controlHealthChanged)
    Q_PROPERTY(int missedHealthReplies READ missedHealthReplies NOTIFY controlHealthChanged)

public:
    explicit StudioDeviceController(QObject* parent = nullptr) : DeviceController(parent) {
        liveFlushTimer_.setSingleShot(true);
        liveFlushTimer_.setInterval(20);
        connect(&liveFlushTimer_, &QTimer::timeout, this, [this] {
            static_cast<void>(flushLiveCommands());
        });

        // S6 health monitoring is intentionally separate from the 700 ms lease
        // heartbeat. The heartbeat protects the firmware output failsafe; this
        // lower-rate positive SHOW/structured-state exchange proves that the
        // bidirectional control path is still responsive.
        controlHealthTimer_.setSingleShot(false);
        controlHealthTimer_.setInterval(controlHealthProbeIntervalMs());
        connect(&controlHealthTimer_, &QTimer::timeout, this, [this] {
            serviceControlHealth();
        });

        connect(this, &DeviceController::connectedChanged, this, [this] {
            if (!connected()) {
                clearPendingLiveCommands();
                resetControlHealth();
            }
            ensureSessionHeartbeat();
        });
        connect(this, &DeviceController::deviceVerifiedChanged, this, [this] {
            if (!deviceVerified()) {
                clearPendingLiveCommands();
                resetControlHealth();
            } else {
                establishControlHealth();
            }
            ensureSessionHeartbeat();
        });
        connect(this, &DeviceController::deviceIdentityChanged, this, [this] {
            // S1 identity still controls permission, but S4 moves the actual
            // heartbeat timer/write ownership into DeviceIoWorker.
            ensureSessionHeartbeat();
        });
        connect(this, &DeviceController::telemetryChanged, this, [this] {
            // SHOW's machine-parsed state line emits telemetryChanged. Periodic
            // firmware telemetry also counts as positive control-path evidence,
            // so it naturally suppresses unnecessary health retries while RUNNING.
            confirmControlHealth();
        });
        connect(this, &DeviceController::portsChanged, this, [this] {
            // QSerialPort errors are still authoritative, but the worker's
            // 750 ms enumeration snapshot closes the gap where Windows removes
            // a COM device before Qt surfaces ResourceError/DeviceNotFoundError.
            const QString activePort = portName().trimmed();
            if (connected() && !activePort.isEmpty() && !ports().contains(activePort)) {
                emit deviceMessage(QStringLiteral(
                    "%1 disappeared from USB/COM enumeration. Releasing the stale session for automatic recovery.")
                    .arg(activePort));
                disconnectPort();
            }
        });

        if (auto* app = QCoreApplication::instance(); app != nullptr) {
            connect(app, &QCoreApplication::aboutToQuit, this, [this] {
                controlHealthTimer_.stop();
                healthProbeOutstanding_ = false;
                setSessionHeartbeatEnabled(false);
                clearPendingLiveCommands();
                if (connected() && running()) {
                    static_cast<void>(DeviceController::stop());
                }
            });
        }
    }

    [[nodiscard]] bool currentFirmwareIdentitySeen() const {
        return DeviceController::identitySupportsCurrentContract(
            deviceIdentity(), QStringLiteral(ARSTACK_STUDIO_VERSION));
    }

    [[nodiscard]] bool controlResponsive() const noexcept { return controlResponsive_; }
    [[nodiscard]] int missedHealthReplies() const noexcept { return missedHealthReplies_; }
    [[nodiscard]] static constexpr int controlHealthProbeIntervalMs() noexcept { return 2000; }
    [[nodiscard]] static constexpr int controlHealthMaxMisses() noexcept { return 2; }

    Q_INVOKABLE bool start() override {
        if (!deviceVerified()) {
            emit deviceMessage(QStringLiteral("Connect and verify the ARStack ESP32-P4 before Start."));
            return false;
        }
        if (!controlResponsive_) {
            emit deviceMessage(QStringLiteral("Device control health is degraded; wait for recovery before Start."));
            return false;
        }
        if (!currentFirmwareIdentitySeen()) {
            emit deviceMessage(QStringLiteral("Firmware update required before Start."));
            return false;
        }
        if (!profileArmed() || profileDeploying()) {
            emit deviceMessage(QStringLiteral("ARStack Studio is still preparing the 4I+4V profile."));
            return false;
        }

        // Queued invocations to one worker preserve sender order: enabling the
        // heartbeat queues an immediate HEARTBEAT before live values and START.
        setSessionHeartbeatEnabled(true);
        if (!flushLiveCommands()) {
            emit deviceMessage(QStringLiteral("Latest injection values could not be queued before Start."));
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
        if (!currentFirmwareIdentitySeen()) {
            emit deviceMessage(QStringLiteral("Firmware update required before profile synchronization."));
            return false;
        }
        return DeviceController::deployProfile(profile);
    }

signals:
    void controlHealthChanged();
    void controlHealthFailed();

private:
    friend class DeterministicSessionHarness;

    struct PendingSignal {
        double magnitude{};
        double phaseDegrees{};
        quint32 quality{};
        double currentCountsPerAmp{};
        double voltageCountsPerVolt{};
    };

    void ensureSessionHeartbeat() {
        setSessionHeartbeatEnabled(
            deviceVerified() && connected() && currentFirmwareIdentitySeen());
    }

    void establishControlHealth() {
        healthProbeOutstanding_ = false;
        missedHealthReplies_ = 0;
        setControlResponsive(true);
        if (!controlHealthTimer_.isActive()) controlHealthTimer_.start();
    }

    void resetControlHealth() {
        controlHealthTimer_.stop();
        healthProbeOutstanding_ = false;
        const bool changed = controlResponsive_ || missedHealthReplies_ != 0;
        controlResponsive_ = false;
        missedHealthReplies_ = 0;
        if (changed) emit controlHealthChanged();
    }

    void setControlResponsive(const bool value) {
        if (controlResponsive_ == value) return;
        controlResponsive_ = value;
        emit controlHealthChanged();
    }

    void confirmControlHealth() {
        if (!connected() || !deviceVerified()) return;
        const bool changed = !controlResponsive_ || missedHealthReplies_ != 0;
        healthProbeOutstanding_ = false;
        controlResponsive_ = true;
        missedHealthReplies_ = 0;
        if (changed) emit controlHealthChanged();
    }

    void serviceControlHealth() {
        if (!connected() || !deviceVerified()) {
            resetControlHealth();
            return;
        }

        // Profile deployment is an exclusive transport transaction. Health
        // observation is deferred instead of interleaving another command or
        // treating intentional queue exclusivity as a missed response.
        if (profileDeploying()) return;

        if (healthProbeOutstanding_) {
            ++missedHealthReplies_;
            setControlResponsive(false);
            emit controlHealthChanged();
            if (missedHealthReplies_ >= controlHealthMaxMisses()) {
                controlHealthTimer_.stop();
                healthProbeOutstanding_ = false;
                emit deviceMessage(QStringLiteral(
                    "Device control health timed out after bounded probes. Reconnecting without restarting SMV automatically."));
                emit controlHealthFailed();
                disconnectPort();
                return;
            }
        }

        // SHOW returns a structured state line parsed by DeviceController. The
        // request is quiet so the diagnostics log is not polluted by outbound
        // health traffic; any normal structured telemetry can satisfy the same
        // positive-liveness proof before the next deadline.
        if (sendQuietCommand(QStringLiteral("SHOW"))) {
            healthProbeOutstanding_ = true;
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
    QTimer controlHealthTimer_;
    QHash<QString, PendingSignal> pendingSignals_;
    double requestedFrequencyHz_{50.0};
    double pendingFrequencyHz_{50.0};
    int missedHealthReplies_{0};
    bool frequencyPending_{false};
    bool controlResponsive_{false};
    bool healthProbeOutstanding_{false};
};