// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/sampled_values/live_signal_transaction.hpp"
#include "ariec61850/sampled_values/profile_identity.hpp"
#include "ariec61850/time_sync/ptp_power_profile.hpp"
#include "DeviceController.hpp"

#include "DeviceIoWorker.hpp"

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/compiled_device_profile.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QProcess>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDebug>
#include <QMetaObject>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QSemaphore>
#include <QSharedPointer>

#include <algorithm>
#include <cmath>
#include <limits>
#include <span>
#include <utility>

namespace {
const QRegularExpression kAnsiExpression{QStringLiteral("\\x1B\\[[0-9;]*m")};
const QRegularExpression kStateExpression{
    QStringLiteral("state=(RUNNING|STOPPED).*generation=(\\d+).*signal_frequency=(\\d+)\\s*mHz"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kTimingExpression{
    QStringLiteral("samples=(\\d+)\\s+\\(~(\\d+)\\s+fps\\).*?MC ok=(\\d+) fail=(\\d+).*?missed=(\\d+).*?signal_gen=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kSignalGenerationExpression{
    QStringLiteral("Live signal generation\\s+(\\d+)\\s+committed"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kScenarioCommitExpression{
    QStringLiteral("CFG COMMITTED token=(\\d+) hash=([0-9A-Fa-f]{16}) generation=(\\d+) boot=([0-9A-Fa-f]{16})")};
const QRegularExpression kScenarioReadbackExpression{
    QStringLiteral("CFG READBACK token=(\\d+) hash=([0-9A-Fa-f]{16}) generation=(\\d+) boot=([0-9A-Fa-f]{16})")};
const QRegularExpression kScenarioStartExpression{
    QStringLiteral("START accepted: profile armed token=(\\d+) hash=([0-9A-Fa-f]{16}) stop_seq=(\\d+)")};
const QRegularExpression kBinaryCommitExpression{
    QStringLiteral("PROFILE BINCOMMIT transaction=(\\d+)\\s+committed generation=(\\d+)\\s+svID=(\\S+)\\s+APPID=0x([0-9A-Fa-f]+)\\s+rate=(\\d+)\\s+wrap=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kBinaryAbortExpression{
    QStringLiteral("PROFILE BINABORT transaction=(\\d+)\\s+accepted"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kProfileArmedExpression{
    QStringLiteral("PROFILE armed generation=(\\d+)\\s+svID=(\\S+)\\s+APPID=0x([0-9A-Fa-f]+)\\s+rate=(\\d+)\\s+wrap=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kProfileReadbackExpression{
    QStringLiteral("PROFILE generation=(\\d+)\\s+svID=(\\S+)\\s+APPID=0x([0-9A-Fa-f]+)\\s+rate=(\\d+)\\s+wrap=(\\d+)\\s+confRev=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kBinaryIdentityExpression{
    QStringLiteral("PROFILE BINIDENTITY generation=(\\d+) hash=([0-9A-Fa-f]{16}) bytes=(\\d+)")};
const QRegularExpression kBinaryStatusExpression{
    QStringLiteral("PROFILE BINSTATUS active=([01])\\s+transaction=(\\d+)\\s+last=(\\d+)\\s+received=(\\d+)\\s+expected=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kBinaryBeginExpression{
    QStringLiteral("PROFILE BINBEGIN transaction=(\\d+)\\s+bytes=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kBinaryChunkExpression{
    QStringLiteral("PROFILE BINCHUNK transaction=(\\d+)\\s+received=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kIdentityExpression{
    QStringLiteral(
        "ARSTACK identity product=([A-Z0-9_-]+) target=([A-Z0-9_-]+) protocol=(\\d+) "
        "device_id=([A-Fa-f0-9]{12}) firmware=([0-9A-Za-z._+\\-]+) "
        "(?:build=([A-Fa-f0-9]{16}) )?"
        "(?:boot_id=([A-Fa-f0-9]{16}) )?capabilities=([A-Z0-9_,.\\-]+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpStatusExpression{
    QStringLiteral("PTP status=(RUNNING|STOPPED) Announce=(\\d+) Sync=(\\d+) FollowUp=(\\d+) PdelayFrames=(\\d+) TXfail=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpEthernetExpression{
    QStringLiteral("PTP ETH driver=(STARTED|STOPPED) link=(UP|DOWN) tx_ready=([01]) start_reason=([A-Z_]+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpConfigExpression{
    QStringLiteral("PTP config domain=(\\d+) transportSpecific=0x([0-9A-Fa-f]+) VLAN=(\\S+) Announce=(\\d+) ms Sync=(\\d+) ms Pdelay=(ON|OFF)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpRoleExpression{
    QStringLiteral("PTPROLE role=(SOURCE|RECEIVER|MONITOR) value=(\\d+) running=([01])"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kPtpMeasuredStateExpression{
    QStringLiteral(
        "PTP2 role=(SOURCE|RECEIVER|MONITOR) discipline=(UNLOCKED|ACQUIRING|LOCKED|HOLDOVER|FAULT) "
        "source=(\\S+) offset=(NA|-?\\d+) path=(NA|-?\\d+) jitter=(NA|-?\\d+) freq=(-?\\d+) "
        "global=([01]) measured=(NA|[012]) rxAnnounce=(\\d+) rxSync=(\\d+) rxFollowUp=(\\d+) "
        "rxPdelay=(\\d+) pdelayReq=(\\d+) accepted=(\\d+) rejected=(\\d+)"),
    QRegularExpression::CaseInsensitiveOption};
const QRegularExpression kSmpSynchExpression{
    QStringLiteral(
        "SMPSYNCH mode=([A-Z0-9_]+) advertised=([0-2]) source=([A-Z0-9_]+) simulated=([01]) measured=([01])"),
    QRegularExpression::CaseInsensitiveOption};

[[nodiscard]] bool finitePositive(const double value) noexcept {
    return std::isfinite(value) && value > 0.0;
}

// Same portable SOURCE admission contract as ESP32-P4 firmware, before any
// command is sent. Unsupported profiles never start a half-transaction.
[[nodiscard]] QString sourcePtpProfileError(const QVariantMap& profile) {
    const auto p = ar::iec61850::time_sync::inspect_ptp_power_profile_cadence(
        profile.value(QStringLiteral("announceIntervalMs")).toUInt(),
        profile.value(QStringLiteral("syncIntervalMs")).toUInt(),
        profile.value(QStringLiteral("respondToPeerDelay")).toBool());
    if (p.compatible()) return {};
    return QStringLiteral(
        "IEC/IEEE 61850-9-3 SOURCE requires Announce 1000 ms, Sync 1000 ms "
        "and hardware peer-delay responses. Review Advanced PTP profile.");
}
} // namespace

DeviceController::DeviceController(QObject* parent) : QObject(parent) {
    // A fresh per-process log lives on disk even if Studio closes or crashes.
    // Never use the install directory (often read-only on Windows).
    QString dataRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dataRoot.isEmpty()) dataRoot = QDir::tempPath() + QStringLiteral("/ARStackStudio");
    diagnosticLogPath_ = QDir(dataRoot).filePath(
        QStringLiteral("Diagnostics/ARStack-Studio-%1-P%2.log")
            .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")))
            .arg(QCoreApplication::applicationPid()));
    // Serial status/log bursts must not force QML to rebind a growing text
    // document for every row. Flush at <=10 Hz; parsing/ACKs are immediate.
    logUpdateTimer_.setSingleShot(true);
    logUpdateTimer_.setInterval(100);
    connect(&logUpdateTimer_, &QTimer::timeout, this,
            &DeviceController::logTextChanged);
    scenarioTimer_.setSingleShot(true);
    scenarioTimer_.setInterval(8000);
    connect(&scenarioTimer_, &QTimer::timeout, this, [this] {
        if (scenarioStage_ != ScenarioStage::idle)
            failScenarioStart(QStringLiteral("SV scenario firmware ACK/readback timed out; START cancelled."));
    });
    ptpStartTimer_.setSingleShot(true);
    connect(&ptpStartTimer_, &QTimer::timeout, this, [this] {
        if (!ptpStarting()) return;
        if (ptpStartStage_ == PtpStartStage::awaiting_stop_ack ||
            ptpStartStage_ == PtpStartStage::awaiting_stop_readback) {
            markPtpStopUnconfirmed(QStringLiteral(
                "PTP STOP was not confirmed by the board. Retry Stop PTP; output may still be active."));
            return;
        }
        if (ptpStartStage_ == PtpStartStage::stop_unconfirmed) return;
        const bool rollback = ptpStartStage_ == PtpStartStage::awaiting_start ||
                              ptpStartStage_ == PtpStartStage::awaiting_runtime;
        failPtpStartup(QStringLiteral("PTP %1 timed out; START not verified.").arg(ptpStartPhase()), rollback);
    });
    ptpStopPollTimer_.setSingleShot(true);
    connect(&ptpStopPollTimer_, &QTimer::timeout, this, [this] {
        if (ptpStartStage_ == PtpStartStage::awaiting_stop_readback &&
            !sendPtpShow()) {
            markPtpStopUnconfirmed(QStringLiteral(
                "PTP STOP could not be verified from the board. Check USB connection / Copy diagnostics."));
        }
    });
    // Retry a coalesced readback once after transport authority is released.
    const auto drainDeferred = [this] {
        if (deferredShow_ || deferredProfileShow_ || deferredPtpShow_)
            QTimer::singleShot(0, this, [this] { flushDeferredReadbacks(); });
    };
    connect(this, &DeviceController::profileStateChanged, this, drainDeferred);
    connect(this, &DeviceController::scenarioStateChanged, this, drainDeferred);
    ioWorker_ = new DeviceIoWorker;
    ioWorker_->moveToThread(&ioThread_);
    ioThread_.setObjectName(QStringLiteral("ARStackDeviceIo"));

    connect(&ioThread_, &QThread::started, ioWorker_, &DeviceIoWorker::initialize);
    connect(&ioThread_, &QThread::finished, ioWorker_, &QObject::deleteLater);
    connectWorkerSignals();
    ioThread_.start();
}

DeviceController::~DeviceController() {
    static_cast<void>(shutdown());
}

bool DeviceController::shutdown() {
    if (shutdownComplete_) return true;
    if (shuttingDown_) return !ioThread_.isRunning();
    shuttingDown_ = true;
    pendingAutoDetect_ = false;
    pendingConnectPort_.clear();
    const bool requestPtpStop = needsPtpStopBeforeShutdown();
    resetPtpStartup();
    resetScenarioStart();

    if (ioWorker_ == nullptr || !ioThread_.isRunning()) {
        ioWorker_ = nullptr;
        ioWorkerReady_ = false;
        ioWorkerAffinityValid_ = false;
        shutdownComplete_ = true;
        shuttingDown_ = false;
        return true;
    }

    const bool requestSvStop = running_;
    if (QThread::currentThread() == &ioThread_) {
        ioWorker_->shutdown(requestSvStop, requestPtpStop);
        ioThread_.quit();
        shuttingDown_ = false;
        return false;
    }

    auto acknowledged = QSharedPointer<QSemaphore>::create();
    DeviceIoWorker* const worker = ioWorker_;
    const bool queued = QMetaObject::invokeMethod(
        worker,
        [worker, requestSvStop, requestPtpStop, acknowledged] {
            worker->shutdown(requestSvStop, requestPtpStop);
            acknowledged->release();
        },
        Qt::QueuedConnection);
    bool graceful = queued && acknowledged->tryAcquire(1, shutdownAckTimeoutMs());
    if (!graceful) {
        qCritical().noquote()
            << "Device I/O shutdown acknowledgement timed out; entering bounded retirement fallback.";
    }

    ioThread_.quit();
    bool joined = ioThread_.wait(shutdownJoinTimeoutMs());
    if (!joined) {
        graceful = false;
        qCritical().noquote()
            << "Device I/O thread missed primary join deadline; retrying bounded retirement.";
        ioThread_.requestInterruption();
        ioThread_.quit();
        joined = ioThread_.wait(shutdownRetryTimeoutMs());
    }
    if (!joined) {
        // Emergency process-exit containment only. Normal CI must never reach this.
        graceful = false;
        qCritical().noquote()
            << "Device I/O thread still alive; emergency terminate fallback engaged.";
        ioThread_.terminate();
        joined = ioThread_.wait(shutdownForceTimeoutMs());
    }

    ioWorker_ = nullptr;
    ioWorkerReady_ = false;
    ioWorkerAffinityValid_ = false;
    connected_ = false;
    running_ = false;
    profileDeploying_ = false;
    shutdownComplete_ = joined;
    shuttingDown_ = false;
    if (!joined) qCritical().noquote() << "Device I/O thread could not be retired before exit.";
    return graceful && joined;
}

void DeviceController::connectWorkerSignals() {
    connect(ioWorker_, &DeviceIoWorker::ready, this,
            [this](const quint64, const bool affinityValid) {
        ioWorkerReady_ = true;
        ioWorkerAffinityValid_ = affinityValid;

        const quint64 generation = sessionGeneration_;
        if (!pendingConnectPort_.isEmpty() && generation != 0) {
            const QString requested = std::exchange(pendingConnectPort_, QString{});
            pendingAutoDetect_ = false;
            QMetaObject::invokeMethod(
                ioWorker_,
                [worker = ioWorker_, requested, generation] {
                    worker->connectPort(requested, generation);
                },
                Qt::QueuedConnection);
            return;
        }
        if (pendingAutoDetect_ && generation != 0) {
            pendingAutoDetect_ = false;
            QMetaObject::invokeMethod(
                ioWorker_,
                [worker = ioWorker_, generation] { worker->autoDetectAndConnect(generation); },
                Qt::QueuedConnection);
        }
    });

    connect(ioWorker_, &DeviceIoWorker::portsObserved, this,
            [this](const quint64 generation, const QStringList& ports,
                   const QString& recommended, const int highConfidenceCount) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        handlePortSnapshot(ports, recommended, highConfidenceCount);
    });

    connect(ioWorker_, &DeviceIoWorker::openingPort, this,
            [this](const quint64 generation, const QString& port, const bool automatic) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        Q_UNUSED(port)
        setDiscoveryState(
            automatic
                ? QStringLiteral("Checking connected device for an ARStack injector...")
                : QStringLiteral("Verifying ARStack injector identity..."),
            true);
    });

    connect(ioWorker_, &DeviceIoWorker::portOpened, this,
            [this](const quint64 generation, const QString& port, const bool automatic) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        handlePortOpened(port, automatic);
    });

    connect(ioWorker_, &DeviceIoWorker::portOpenFailed, this,
            [this](const quint64 generation, const QString& port,
                   const QString& message, const bool automatic) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        Q_UNUSED(port)
        if (!automatic) {
            setIdentificationState(IdentificationState::Idle);
            setDiscoveryState(QStringLiteral("The selected serial port could not be opened."), false);
            setError(message);
        }
    });

    connect(ioWorker_, &DeviceIoWorker::portClosed, this,
            [this](const quint64 generation, const QString& port) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        handlePortClosed(port);
    });

    connect(ioWorker_, &DeviceIoWorker::portReleased, this,
            [this](const quint64 generation, const QString& port) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        emit portReleased(generation, port);
    });

    connect(ioWorker_, &DeviceIoWorker::identificationAttempt, this,
            [this](const quint64 generation, const QString& port,
                   const int attempt, const int maximum) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        lastIdentificationPort_ = port;
        identifyAttempts_ = attempt;
        setIdentificationState(IdentificationState::Identifying);
        setDiscoveryState(
            QStringLiteral("Verifying ARStack semantic identity… attempt %1/%2")
                .arg(attempt)
                .arg(maximum),
            true);
    });

    connect(ioWorker_, &DeviceIoWorker::identificationTimedOut, this,
            [this](const quint64 generation, const QString& port,
                   const int attempts, const bool continuing) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        lastIdentificationPort_ = port;
        identifyAttempts_ = attempts;
        const QString timedOutPort = port.trimmed();
        const bool singleVisiblePort = ports_.size() == 1 &&
            ports_.constFirst().compare(timedOutPort, Qt::CaseInsensitive) == 0;
        const bool recommendedCandidate = !recommendedPort_.isEmpty() &&
            recommendedPort_.compare(timedOutPort, Qt::CaseInsensitive) == 0;
        if (!timedOutPort.isEmpty() && (singleVisiblePort || recommendedCandidate)) {
            recoveryCandidatePort_ = timedOutPort;
        }
        if (continuing) {
            setIdentificationState(IdentificationState::Identifying);
            setDiscoveryState(QStringLiteral("Checking connected devices for an ARStack injector..."), true);
            return;
        }
        setIdentificationState(IdentificationState::Unidentified);
        setDiscoveryState(
            QStringLiteral("No ARStack semantic identity received from %1 after %2 bounded attempts.")
                .arg(port.isEmpty() ? QStringLiteral("the selected port") : port)
                .arg(attempts),
            false);
    });

    connect(ioWorker_, &DeviceIoWorker::automaticProbeExhausted, this,
            [this](const quint64 generation) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        if (identificationState_ != IdentificationState::Unidentified) {
            identifyAttempts_ = 0;
            setIdentificationState(IdentificationState::Idle);
        }
        setDiscoveryState(
            ports_.isEmpty()
                ? QStringLiteral("No verified injector found; setpoints remain editable offline.")
                : QStringLiteral("No ARStack injector answered; manual port selection remains available."),
            false);
    });

    connect(ioWorker_, &DeviceIoWorker::lineReceived, this,
            [this](const quint64 generation, const QString& line) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        processLine(line);
    });

    connect(ioWorker_, &DeviceIoWorker::commandTransmitted, this,
            [this](const quint64 generation, const QString& command, const bool quiet) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        // HEARTBEAT is a firmware output lease, not disposable chatter:
        // preserve *transmitted* times to diagnose a 6-second watchdog STOP.
        if (command == QStringLiteral("HEARTBEAT"))
            appendLog(QStringLiteral("HB-TX"), QStringLiteral("HEARTBEAT host drained serial bytes"));
        else if (!quiet) appendLog(QStringLiteral("→"), command);
    });

    connect(ioWorker_, &DeviceIoWorker::commandRejected, this,
            [this](const quint64 generation, const QString& message) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        if (scenarioStage_ != ScenarioStage::idle)
            failScenarioStart(QStringLiteral("SV scenario serial command rejected: ") + message);
        if (profileDeploying_) {
            profileDeploying_ = false;
            profileArmed_ = false;
            emit profileStateChanged();
        }
        if (ptpStartStage_ == PtpStartStage::awaiting_stop_ack ||
            ptpStartStage_ == PtpStartStage::awaiting_stop_readback) {
            markPtpStopUnconfirmed(QStringLiteral("PTP STOP verification command rejected: ") + message);
        } else if (ptpStarting() && ptpStartStage_ != PtpStartStage::stop_unconfirmed) {
            const bool rollback = ptpStartStage_ == PtpStartStage::awaiting_start ||
                                  ptpStartStage_ == PtpStartStage::awaiting_runtime;
            failPtpStartup(QStringLiteral("PTP serial command rejected: ") + message, rollback);
        } else setError(message);
    });

    connect(ioWorker_, &DeviceIoWorker::transportError, this,
            [this](const quint64 generation, const QString& message, const bool fatal) {
        if (!workerEventIsCurrent(sessionGeneration_, generation)) return;
        Q_UNUSED(fatal)
        if (scenarioStage_ != ScenarioStage::idle)
            failScenarioStart(QStringLiteral("SV transaction transport error: ") + message);
        setError(message);
    });
}

QStringList DeviceController::ports() const { return ports_; }
QString DeviceController::recommendedPort() const { return recommendedPort_; }
QString DeviceController::recoveryCandidatePort() const { return recoveryCandidatePort_; }
QString DeviceController::discoveryStatus() const { return discoveryStatus_; }
bool DeviceController::discovering() const noexcept { return discovering_; }
bool DeviceController::deviceVerified() const noexcept { return deviceVerified_; }
DeviceController::IdentificationState DeviceController::identificationState() const noexcept { return identificationState_; }
int DeviceController::identifyAttempts() const noexcept { return identifyAttempts_; }
QString DeviceController::deviceProduct() const { return identity_.product; }
QString DeviceController::deviceTarget() const { return identity_.target; }
QString DeviceController::deviceId() const { return identity_.deviceId; }
QString DeviceController::protocolVersion() const { return identity_.protocolVersion; }
QString DeviceController::firmwareVersion() const { return identity_.firmwareVersion; }
QString DeviceController::firmwareBuildId() const { return identity_.buildId; }
QString DeviceController::bootId() const { return identity_.bootId; }
QStringList DeviceController::capabilities() const { return identity_.capabilities; }
DeviceIdentity DeviceController::deviceIdentity() const { return identity_; }
bool DeviceController::connected() const noexcept { return connected_; }
bool DeviceController::running() const noexcept { return running_; }
bool DeviceController::scenarioApplying() const noexcept { return scenarioStage_ != ScenarioStage::idle; }
QString DeviceController::portName() const { return portName_; }
QString DeviceController::lastError() const { return lastError_; }
QString DeviceController::logText() const { return logText_; }
QString DeviceController::fps() const { return fps_; }
QString DeviceController::missed() const { return missed_; }
QString DeviceController::txFailures() const { return txFailures_; }
QString DeviceController::signalGeneration() const { return signalGeneration_; }
QString DeviceController::profileGeneration() const { return profileGeneration_; }
bool DeviceController::profileArmed() const noexcept { return profileArmed_; }
bool DeviceController::profileDeploying() const noexcept { return profileDeploying_; }

QString DeviceController::profileDeploymentProgressSummary() const {
    QString stage;
    switch (binaryProfileStage_) {
    case BinaryProfileStage::idle: stage = QStringLiteral("idle"); break;
    case BinaryProfileStage::waiting_status: stage = QStringLiteral("BINSTATUS"); break;
    case BinaryProfileStage::waiting_reuse: stage = QStringLiteral("PROFILE reuse readback"); break;
    case BinaryProfileStage::waiting_abort: stage = QStringLiteral("BINABORT"); break;
    case BinaryProfileStage::waiting_begin: stage = QStringLiteral("BINBEGIN"); break;
    case BinaryProfileStage::waiting_chunk: stage = QStringLiteral("BINCHUNK"); break;
    case BinaryProfileStage::waiting_commit: stage = QStringLiteral("BINCOMMIT"); break;
    case BinaryProfileStage::waiting_readback: stage = QStringLiteral("PROFILE SHOW readback"); break;
    }
    return QStringLiteral("phase=%1, verified_bytes=%2/%3, transaction=%4, boot=%5")
        .arg(stage).arg(binaryProfile_.offset).arg(binaryProfile_.bytes.size())
        .arg(binaryProfile_.transaction).arg(identity_.bootId);
}
bool DeviceController::ptpAvailable() const noexcept { return ptpAvailable_; }
bool DeviceController::ptpRunning() const noexcept { return ptpRunning_; }
QString DeviceController::ptpStatus() const { return ptpStatus_; }
QString DeviceController::ptpStartPhase() const {
    switch (ptpStartStage_) {
    case PtpStartStage::idle: return QStringLiteral("IDLE");
    case PtpStartStage::awaiting_preflight: return QStringLiteral("VERIFY_STOPPED");
    case PtpStartStage::awaiting_role_ack: return QStringLiteral("ROLE_ACK");
    case PtpStartStage::awaiting_role_readback: return QStringLiteral("ROLE_READBACK");
    case PtpStartStage::awaiting_config: return QStringLiteral("CONFIG_ACK");
    case PtpStartStage::awaiting_start: return QStringLiteral("START_ACK");
    case PtpStartStage::awaiting_runtime: return QStringLiteral("RUNTIME_READBACK");
    case PtpStartStage::awaiting_stop_ack: return QStringLiteral("STOP_ACK");
    case PtpStartStage::awaiting_stop_readback: return QStringLiteral("STOP_READBACK");
    case PtpStartStage::stop_unconfirmed: return QStringLiteral("STOP_UNVERIFIED");
    }
    return QStringLiteral("UNKNOWN");
}
QString DeviceController::ptpDomain() const { return ptpDomain_; }
QString DeviceController::ptpTransportSpecific() const { return ptpTransportSpecific_; }
QString DeviceController::ptpVlan() const { return ptpVlan_; }
QString DeviceController::ptpAnnounceSent() const { return ptpAnnounceSent_; }
QString DeviceController::ptpSyncSent() const { return ptpSyncSent_; }
QString DeviceController::ptpTxFailures() const { return ptpTxFailures_; }

void DeviceController::setSessionGeneration(const quint64 generation) {
    if (generation == 0 || generation == sessionGeneration_) return;
    resetPtpStartup();
    resetScenarioStart();
    deferredShow_ = false;
    deferredProfileShow_ = false;
    deferredPtpShow_ = false;
    sessionGeneration_ = generation;
    pendingAutoDetect_ = false;
    pendingConnectPort_.clear();
}

bool DeviceController::parseIdentityLine(const QString& line, DeviceIdentity& identity) {
    const auto match = kIdentityExpression.match(line.trimmed());
    if (!match.hasMatch()) return false;

    DeviceIdentity parsed;
    parsed.product = match.captured(1).toUpper();
    parsed.target = match.captured(2).toUpper();
    parsed.protocolVersion = match.captured(3);
    parsed.deviceId = match.captured(4).toUpper();
    parsed.firmwareVersion = match.captured(5);
    parsed.buildId = match.captured(6).toLower();
    parsed.bootId = match.captured(7).toUpper();
    parsed.capabilities = match.captured(8).split(QLatin1Char(','), Qt::SkipEmptyParts);
    for (QString& capability : parsed.capabilities) capability = capability.trimmed().toUpper();
    parsed.capabilities.removeDuplicates();

    bool protocolOk = false;
    static_cast<void>(parsed.protocolVersion.toUInt(&protocolOk));
    if (!protocolOk || parsed.product != QStringLiteral("SMV-INJECTOR") ||
        parsed.target != QStringLiteral("ESP32-P4") || parsed.capabilities.isEmpty()) {
        return false;
    }

    identity = std::move(parsed);
    return true;
}

bool DeviceController::identitySupportsCurrentContract(
    const DeviceIdentity& identity,
    const QString& expectedFirmwareVersion,
    const QString& expectedBuildId) {
    static const QStringList requiredCapabilities{
        QStringLiteral("SMV-4I4V"),
        QStringLiteral("PROFILE"),
        QStringLiteral("PROFILE-BINARY-V1"),
        QStringLiteral("LIVE-SETPOINTS"),
        QStringLiteral("SESSION-LEASE"),
        QStringLiteral("PTP-P2"),
        QStringLiteral("SMPSYNCH-AUTO")};
    const QString wantedBuild = expectedBuildId.trimmed().toLower();
    if (identity.product != QStringLiteral("SMV-INJECTOR") ||
        identity.target != QStringLiteral("ESP32-P4") ||
        identity.protocolVersion != QStringLiteral("1") ||
        identity.firmwareVersion != expectedFirmwareVersion ||
        (!wantedBuild.isEmpty() && identity.buildId.compare(wantedBuild, Qt::CaseInsensitive) != 0) ||
        identity.bootId.size() != 16) {
        return false;
    }
    for (const QString& required : requiredCapabilities) {
        if (!identity.capabilities.contains(required, Qt::CaseInsensitive)) return false;
    }
    return true;
}

void DeviceController::refreshPorts() {
    if (!ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0) return;
    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, generation] { worker->refreshPorts(generation); },
        Qt::QueuedConnection);
}

bool DeviceController::autoDetectAndConnect() {
    if (deviceVerified_) return true;
    if (connected_ || discovering_ || sessionGeneration_ == 0) return false;

    pendingConnectPort_.clear();
    pendingAutoDetect_ = !ioWorkerReady_;
    setDiscoveryState(QStringLiteral("Looking for an ARStack ESP32-P4 injector..."), true);

    if (ioWorkerReady_ && ioWorker_ != nullptr) {
        const quint64 generation = sessionGeneration_;
        QMetaObject::invokeMethod(
            ioWorker_,
            [worker = ioWorker_, generation] { worker->autoDetectAndConnect(generation); },
            Qt::QueuedConnection);
    }
    return true;
}

bool DeviceController::connectPort(const QString& portName) {
    const QString requested = portName.trimmed();
    if (requested.isEmpty()) {
        setError(QStringLiteral("Select a serial port first."));
        return false;
    }
    if (sessionGeneration_ == 0) {
        setError(QStringLiteral("Device connection has no active supervisor generation."));
        return false;
    }

    pendingAutoDetect_ = false;
    setDiscoveryState(QStringLiteral("Verifying ARStack injector identity..."), true);
    if (!ioWorkerReady_) {
        pendingConnectPort_ = requested;
        return true;
    }

    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, requested, generation] {
            worker->connectPort(requested, generation);
        },
        Qt::QueuedConnection);
    return true;
}

void DeviceController::disconnectPort() {
    pendingAutoDetect_ = false;
    pendingConnectPort_.clear();
    if (!ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0) return;
    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, generation] { worker->disconnectPort(generation); },
        Qt::QueuedConnection);
}

void DeviceController::handlePortSnapshot(
    const QStringList& ports,
    const QString& recommendedPort,
    const int highConfidenceCount) {
    const bool portsDidChange = ports != ports_;
    const bool recommendationChanged = recommendedPort != recommendedPort_;
    ports_ = ports;
    recommendedPort_ = recommendedPort;
    highConfidenceCount_ = highConfidenceCount;
    if (!recoveryCandidatePort_.isEmpty() && !ports_.contains(recoveryCandidatePort_)) {
        recoveryCandidatePort_.clear();
    }

    if (identificationState_ == IdentificationState::Unidentified &&
        !lastIdentificationPort_.isEmpty() && !ports_.contains(lastIdentificationPort_)) {
        lastIdentificationPort_.clear();
        identifyAttempts_ = 0;
        setIdentificationState(IdentificationState::Idle);
    }

    if (portsDidChange) emit portsChanged();
    if (recommendationChanged) emit discoveryChanged();

    if (!connected_ && !discovering_) {
        if (identificationState_ == IdentificationState::Unidentified) {
            setDiscoveryState(
                QStringLiteral("ARStack identity was not received. Retry identification or choose firmware setup explicitly."),
                false);
        } else if (!recommendedPort_.isEmpty()) {
            setDiscoveryState(QStringLiteral("Compatible ESP32-P4 USB device found."), false);
        } else if (highConfidenceCount_ > 1) {
            setDiscoveryState(QStringLiteral("Multiple Espressif devices found; choose the intended injector."), false);
        } else {
            setDiscoveryState(QStringLiteral("No verified injector found; setpoints remain editable offline."), false);
        }
    }
}

void DeviceController::handlePortOpened(const QString& portName, const bool automatic) {
    Q_UNUSED(automatic)
    const bool wasVerified = deviceVerified_;
    connected_ = true;
    portName_ = portName;
    lastIdentificationPort_ = portName;
    recoveryCandidatePort_.clear();
    identifyAttempts_ = 0;
    deviceVerified_ = false;
    clearIdentity();
    running_ = false;
    profileArmed_ = false;
    profileWasReused_ = false;
    profileDeploying_ = false;
    deferredShow_ = false;
    deferredProfileShow_ = false;
    deferredPtpShow_ = false;
    resetBinaryProfileTransfer();
    resetScenarioStart();
    resetPtpState();
    resetTelemetry();

    if (!lastError_.isEmpty()) {
        lastError_.clear();
        emit lastErrorChanged();
    }

    appendLog(QStringLiteral("•"), QStringLiteral("Connected %1 at 115200 8N1").arg(portName_));
    setIdentificationState(IdentificationState::Identifying);
    emit connectedChanged();
    emit runningChanged();
    emit profileStateChanged();
    emit ptpStateChanged();
    if (wasVerified) emit deviceVerifiedChanged();
}

void DeviceController::handlePortClosed(const QString& portName) {
    const bool wasConnected = connected_;
    const bool wasVerified = deviceVerified_;
    connected_ = false;
    if (!portName.isEmpty()) portName_ = portName;
    running_ = false;
    profileDeploying_ = false;
    profileArmed_ = false;
    profileWasReused_ = false;
    deferredShow_ = false;
    deferredProfileShow_ = false;
    deferredPtpShow_ = false;
    resetBinaryProfileTransfer();
    deviceVerified_ = false;
    clearIdentity();
    identifyAttempts_ = 0;
    setIdentificationState(IdentificationState::Idle);
    discovering_ = false;
    resetPtpState();
    resetScenarioStart();
    resetTelemetry();

    emit runningChanged();
    emit profileStateChanged();
    emit discoveryChanged();
    emit ptpStateChanged();
    if (wasVerified) emit deviceVerifiedChanged();
    if (wasConnected) {
        appendLog(QStringLiteral("•"), QStringLiteral("Device disconnected"));
        emit connectedChanged();
    }
}

bool DeviceController::sendShow() { return sendCommand(QStringLiteral("SHOW")); }
bool DeviceController::start() {
    // A bare START bypasses the 4I+4V scenario snapshot and may reuse stale firmware state.
    // Until firmware-applied transaction ACK is implemented, never expose this shortcut.
    setError(QStringLiteral("Bare START refused: a fresh 4I+4V scenario is required."));
    return false;
}

bool DeviceController::startWithScenario(const QVariantMap& scenario) {
    return submitScenario(scenario, true);
}

bool DeviceController::applyLiveScenario(const QVariantMap& scenario) {
    return submitScenario(scenario, false);
}

bool DeviceController::submitScenario(const QVariantMap& scenario, const bool starting) {
    // Same exact 4I4V wire encoder, token, commit and independent readback for
    // START and RUNNING edit. Live mode NEVER sends START or resets phase.
    if (!deviceVerified_ || running_ == starting ||
        scenarioStage_ != ScenarioStage::idle) return false;
    if (!identity_.capabilities.contains(QStringLiteral("SV-CONFIG-TXN-V1"), Qt::CaseInsensitive) ||
        identity_.bootId.size() != 16 || sessionGeneration_ == 0) {
        setError(QStringLiteral("Firmware does not support verified SV configuration transactions; START refused."));
        return false;
    }
    bool hzOk = false, currentScaleOk = false, voltageScaleOk = false;
    const double hz = scenario.value(QStringLiteral("hz")).toDouble(&hzOk);
    const double currentScale = scenario.value(QStringLiteral("currentScale")).toDouble(&currentScaleOk);
    const double voltageScale = scenario.value(QStringLiteral("voltageScale")).toDouble(&voltageScaleOk);
    if (!hzOk || !std::isfinite(hz) || hz < 0.0 || hz > 1000.0 ||
        !currentScaleOk || !voltageScaleOk || !finitePositive(currentScale) ||
        !finitePositive(voltageScale)) {
        setError(QStringLiteral("Invalid frequency or engineering scale; START refused."));
        return false;
    }
    const auto current = scenario.value(QStringLiteral("current")).toList();
    const auto voltage = scenario.value(QStringLiteral("voltage")).toList();
    if (current.size() != 4 || voltage.size() != 4) {
        setError(QStringLiteral("START requires exactly four current and four voltage channels."));
        return false;
    }
    using ar::iec61850::sampled_values::SvLiveSignalState;
    using ar::iec61850::sampled_values::sv_scenario_hash;
    SvLiveSignalState normalized{};
    normalized.frequency_millihz = static_cast<quint32>(std::llround(hz * 1000.0));
    QStringList commands;
    commands.reserve(22);
    commands << QStringLiteral("CFG FREQ %1").arg(normalized.frequency_millihz);
    static const QStringList ids{QStringLiteral("IA"), QStringLiteral("IB"),
        QStringLiteral("IC"), QStringLiteral("IN"), QStringLiteral("UA"),
        QStringLiteral("UB"), QStringLiteral("UC"), QStringLiteral("UN")};
    for (int i = 0; i < 8; ++i) {
        const QVariantList row = (i < 4 ? current.at(i) : voltage.at(i - 4)).toList();
        if (row.size() != 4 || row.at(2).metaType().id() != QMetaType::Bool) {
            setError(QStringLiteral("Invalid channel state; START refused."));
            return false;
        }
        bool magOk = false, phaseOk = false, qualityOk = false;
        const double magnitude = row.at(0).toDouble(&magOk);
        const double phase = row.at(1).toDouble(&phaseOk);
        const qulonglong quality = row.at(3).toULongLong(&qualityOk);
        if (!magOk || !phaseOk || !qualityOk || quality > std::numeric_limits<quint32>::max()) {
            setError(QStringLiteral("Invalid channel data; START refused."));
            return false;
        }
        const QString command = formatSignalCommand(ids.at(i), magnitude, phase,
            static_cast<quint32>(quality), currentScale, voltageScale, hz,
            &normalized.channels[static_cast<std::size_t>(i)]);
        if (command.isEmpty()) {
            setError(QStringLiteral("Out-of-range setpoint; START refused."));
            return false;
        }
        normalized.channels[static_cast<std::size_t>(i)].enabled = row.at(2).toBool();
        commands << QStringLiteral("CFG ") + command;
        commands << QStringLiteral("CFG ENABLE %1 %2").arg(ids.at(i)).arg(row.at(2).toBool() ? 1 : 0);
    }
    const QVariantMap shape = scenario.value(QStringLiteral("ct")).toMap();
    bool dcOk = false, harmonicOk = false, clipOk = false, orderOk = false;
    const double dc = shape.value(QStringLiteral("dc")).toDouble(&dcOk);
    const double harmonic = shape.value(QStringLiteral("harmonic")).toDouble(&harmonicOk);
    const double clip = shape.value(QStringLiteral("clip")).toDouble(&clipOk);
    const int order = shape.value(QStringLiteral("order")).toInt(&orderOk);
    const bool shaping = shape.value(QStringLiteral("enabled")).toBool();
    if (!dcOk || !harmonicOk || !clipOk || !orderOk ||
        (shaping && hz == 0.0) || !std::isfinite(dc) || dc < -300.0 || dc > 300.0 ||
        !std::isfinite(harmonic) || harmonic < 0.0 || harmonic > 300.0 ||
        !std::isfinite(clip) || clip < 1.0 || clip > 1000.0 || order < 2 || order > 63) {
        setError(QStringLiteral("Invalid CT shaping; START refused."));
        return false;
    }
    normalized.current_shape.enabled = shaping;
    normalized.current_shape.dc_offset_permille = static_cast<qint32>(std::llround(dc * 10.0));
    normalized.current_shape.harmonic_permille = static_cast<quint32>(std::llround(harmonic * 10.0));
    normalized.current_shape.harmonic_order = static_cast<quint8>(order);
    normalized.current_shape.clip_permille = static_cast<quint32>(std::llround(clip * 10.0));
    if (!ar::iec61850::sampled_values::SvLiveSignalBank::validate(normalized)) {
        setError(QStringLiteral("Scenario is outside firmware wire bounds; START refused."));
        return false;
    }
    commands << QStringLiteral("CFG SHAPE CT %1 %2 %3 %4 %5")
        .arg(shaping ? 1 : 0).arg(std::llround(dc * 10.0))
        .arg(std::llround(harmonic * 10.0)).arg(order).arg(std::llround(clip * 10.0));
    quint64 token = QRandomGenerator::global()->generate64();
    if (token == 0) token = 1;
    const quint64 hash = sv_scenario_hash(normalized);
    const QString hashText = QString::number(hash, 16).toUpper().rightJustified(16, QLatin1Char('0'));
    commands.prepend(QStringLiteral("CFG BEGIN %1 %2").arg(token).arg(identity_.bootId));
    commands << QStringLiteral("CFG COMMIT %1 %2").arg(token).arg(hashText);
    // Only the immutable staging+COMMIT goes into the bounded exclusive FIFO.
    // START is dispatched later, after firmware ACK and independent bank readback.
    scenarioStage_ = ScenarioStage::awaiting_commit;
    scenarioLiveUpdate_ = !starting;
    scenarioPendingFrequencyHz_ = hz;
    scenarioToken_ = token;
    scenarioHash_ = hash;
    scenarioBoot_ = identity_.bootId;
    scenarioSessionGeneration_ = sessionGeneration_;
    scenarioSignalGeneration_ = 0;
    scenarioProfileGeneration_ = profileGeneration_;
    if (!sendCommandBatch(commands, false, true)) { resetScenarioStart(); return false; }
    scenarioTimer_.start();
    appendLog(QStringLiteral("SVTX"), QStringLiteral(
        "TXN_BEGIN session=%1 boot=%2 token=%3 profile=%4 state=CFG_COMMIT")
        .arg(sessionGeneration_).arg(scenarioBoot_).arg(scenarioToken_)
        .arg(scenarioProfileGeneration_));
    emit scenarioStateChanged();
    return true;
}
bool DeviceController::stop() {
    operatorStopPending_ = true;
    appendLog(QStringLiteral("SVTX"), QStringLiteral(
        "OPERATOR_STOP_REQUEST session=%1 boot=%2 running=%3 cfg_pending=%4")
        .arg(sessionGeneration_).arg(identity_.bootId)
        .arg(running_ ? 1 : 0).arg(scenarioStage_ != ScenarioStage::idle ? 1 : 0));
    resetScenarioStart();
    return sendCommand(QStringLiteral("STOP"));
}
bool DeviceController::zero() { return sendCommand(QStringLiteral("ZERO")); }

bool DeviceController::setFrequency(const double hz) {
    if (!std::isfinite(hz) || hz < 0.0 || hz > 1000.0) {
        setError(QStringLiteral("Frequency must be within 0..1000 Hz (0 = DC)."));
        return false;
    }
    const auto millihertz = std::llround(hz * 1000.0);
    if (millihertz < 0 || millihertz > std::numeric_limits<quint32>::max()) return false;
    if (!sendCommand(QStringLiteral("FREQ %1").arg(millihertz))) return false;
    signalFrequencyHz_ = hz;
    return true;
}

QString DeviceController::formatSignalCommand(
    const QString& signalId,
    const double magnitude,
    const double phaseDegrees,
    const quint32 quality,
    const double currentCountsPerAmp,
    const double voltageCountsPerVolt,
    const double frequencyHz,
    ar::iec61850::sampled_values::SvLiveChannelState* const encoded) {
    const QString id = signalId.trimmed().toUpper();
    static const QStringList validIds{
        QStringLiteral("IA"), QStringLiteral("IB"), QStringLiteral("IC"), QStringLiteral("IN"),
        QStringLiteral("UA"), QStringLiteral("UB"), QStringLiteral("UC"), QStringLiteral("UN")};
    if (!validIds.contains(id) || !std::isfinite(magnitude) ||
        (frequencyHz > 0.0 && magnitude < 0.0) || !std::isfinite(phaseDegrees) ||
        !finitePositive(currentCountsPerAmp) || !finitePositive(voltageCountsPerVolt)) return {};

    const double scale = id.startsWith(QLatin1Char('I')) ? currentCountsPerAmp : voltageCountsPerVolt;
    const double scaled = magnitude * scale;
    const double angleMdeg = phaseDegrees * 1000.0;
    if (!std::isfinite(scaled) || !std::isfinite(angleMdeg) ||
        scaled < static_cast<double>(std::numeric_limits<qint32>::min()) ||
        scaled > static_cast<double>(std::numeric_limits<qint32>::max()) ||
        angleMdeg < static_cast<double>(std::numeric_limits<qint32>::min()) ||
        angleMdeg > static_cast<double>(std::numeric_limits<qint32>::max())) return {};
    const auto counts = std::llround(scaled);
    const auto phaseMdeg = std::llround(angleMdeg);
    if (encoded != nullptr) {
        encoded->rms_counts = static_cast<qint32>(counts);
        encoded->phase_millidegrees = static_cast<qint32>(phaseMdeg);
        encoded->quality = quality;
    }
    return QStringLiteral("SET %1 %2 %3 %4")
        .arg(id)
        .arg(counts)
        .arg(phaseMdeg)
        .arg(static_cast<qulonglong>(quality));
}

bool DeviceController::setSignal(
    const QString& signalId,
    const double magnitude,
    const double phaseDegrees,
    const quint32 quality,
    const double currentCountsPerAmp,
    const double voltageCountsPerVolt) {
    const QString wire = formatSignalCommand(
        signalId, magnitude, phaseDegrees, quality,
        currentCountsPerAmp, voltageCountsPerVolt, signalFrequencyHz_);
    if (wire.isEmpty()) {
        setError(QStringLiteral("Invalid signal or engineering scale; SET not queued."));
        return false;
    }
    return sendCommand(wire);
}

bool DeviceController::setEnabled(const QString& signalId, const bool enabled) {
    return sendCommand(QStringLiteral("ENABLE %1 %2")
        .arg(signalId.trimmed().toUpper())
        .arg(enabled ? 1 : 0));
}

bool DeviceController::setQuality(const QString& signalId, const quint32 quality) {
    return sendCommand(QStringLiteral("QUALITY %1 %2")
        .arg(signalId.trimmed().toUpper())
        .arg(static_cast<qulonglong>(quality)));
}

bool DeviceController::deployProfile(const QVariantMap& profile) {
    if (!deviceVerified_) {
        setError(QStringLiteral("Connect the ESP32-P4 before deployment."));
        return false;
    }
    if (running_) {
        setError(QStringLiteral("Stop the publisher before changing profile identity/layout."));
        return false;
    }
    if (profile.value(QStringLiteral("compatibilityClass")).toString() != QStringLiteral("A") ||
        profile.value(QStringLiteral("deviceSupport")).toString() != QStringLiteral("ready")) {
        setError(QStringLiteral("Only Class A profiles supported by the current ESP32-P4 layout can be deployed."));
        return false;
    }
    if (!identity_.capabilities.contains(QStringLiteral("PROFILE-BINARY-V1"), Qt::CaseInsensitive)) {
        setError(QStringLiteral("Firmware update required: canonical binary profile deployment is unavailable."));
        return false;
    }

    const QByteArray bytes = profile.value(QStringLiteral("deviceProfileBinary")).toByteArray();
    if (bytes.isEmpty()) {
        setError(QStringLiteral("The selected SCL profile has no canonical device-profile binary."));
        return false;
    }

    using namespace ar::iec61850::sampled_values;
    CompiledSvDeviceProfile decoded{};
    const auto source = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size())};
    if (!SvDeviceProfileBinaryCodec::decode(source, decoded).success()) {
        setError(QStringLiteral("The native SCL compiler produced an invalid device-profile envelope."));
        return false;
    }

    const QString svId = QString::fromUtf8(
        decoded.sv_id.data(), static_cast<qsizetype>(decoded.sv_id_length));
    if (svId.isEmpty() ||
        svId != profile.value(QStringLiteral("svId")).toString() ||
        decoded.app_id != profile.value(QStringLiteral("appId")).toUInt() ||
        decoded.vlan_present != profile.value(QStringLiteral("vlanPresent")).toBool() ||
        decoded.vlan_id != profile.value(QStringLiteral("vlanId")).toUInt() ||
        decoded.vlan_priority != profile.value(QStringLiteral("vlanPriority")).toUInt() ||
        decoded.frame_rate_hz != profile.value(QStringLiteral("publisherRate")).toULongLong() ||
        decoded.sample_counter_modulus != profile.value(QStringLiteral("counterModulus")).toUInt() ||
        decoded.configuration_revision != profile.value(QStringLiteral("confRev")).toULongLong()) {
        setError(QStringLiteral("Canonical binary profile does not match the selected Studio engineering profile."));
        return false;
    }

    resetBinaryProfileTransfer();
    profileWasReused_ = false;
    binaryProfile_.bytes = bytes;
    binaryProfile_.recordHash = sv_binary_profile_identity_hash(source);
    binaryProfile_.svId = svId;
    binaryProfile_.bootId = identity_.bootId;
    binaryProfile_.appId = decoded.app_id;
    binaryProfile_.rate = decoded.frame_rate_hz;
    binaryProfile_.modulus = decoded.sample_counter_modulus;
    binaryProfile_.confRev = decoded.configuration_revision;
    binaryProfileStage_ = BinaryProfileStage::waiting_status;
    profileDeploying_ = true;
    profileArmed_ = false;
    emit profileStateChanged();

    if (!sendCommand(QStringLiteral("PROFILE BINSTATUS"))) {
        failBinaryProfileDeployment(QStringLiteral("Could not query firmware binary-profile staging state."));
        return false;
    }
    return true;
}

void DeviceController::abandonProfileDeployment() {
    if (!profileDeploying_) return;
    const auto stage = binaryProfileStage_;
    const auto transaction = binaryProfile_.transaction;
    resetBinaryProfileTransfer();
    profileDeploying_ = false;
    profileArmed_ = false;
    profileWasReused_ = false;
    emit profileStateChanged();

    if (transaction != 0U &&
        (stage == BinaryProfileStage::waiting_begin ||
         stage == BinaryProfileStage::waiting_chunk ||
         stage == BinaryProfileStage::waiting_commit)) {
        static_cast<void>(sendQuietCommand(
            QStringLiteral("PROFILE BINABORT %1").arg(transaction)));
    }
}

void DeviceController::resetBinaryProfileTransfer() {
    binaryProfileStage_ = BinaryProfileStage::idle;
    binaryProfile_ = {};
}

void DeviceController::failBinaryProfileDeployment(const QString& message) {
    resetBinaryProfileTransfer();
    profileDeploying_ = false;
    profileArmed_ = false;
    profileWasReused_ = false;
    setError(message);
    emit profileStateChanged();
}

bool DeviceController::beginBinaryProfileTransaction(const quint32 lastTransaction) {
    if (lastTransaction == std::numeric_limits<quint32>::max()) {
        failBinaryProfileDeployment(
            QStringLiteral("Firmware binary-profile transaction space is exhausted; reset the board."));
        return false;
    }
    binaryProfile_.lastObservedTransaction = lastTransaction;
    binaryProfile_.reusedReadbackMatched = false;
    binaryProfile_.reusedReadbackGeneration.clear();
    binaryProfile_.transaction = lastTransaction + 1U;
    binaryProfile_.offset = 0;
    binaryProfile_.expectedReceived = 0;
    binaryProfileStage_ = BinaryProfileStage::waiting_begin;
    if (!sendCommand(QStringLiteral("PROFILE BINBEGIN %1 %2")
            .arg(binaryProfile_.transaction)
            .arg(binaryProfile_.bytes.size()))) {
        failBinaryProfileDeployment(QStringLiteral("Could not start binary profile staging."));
        return false;
    }
    return true;
}

bool DeviceController::sendNextBinaryProfileChunk() {
    constexpr qsizetype kChunkBytes = 48;
    if (binaryProfile_.offset >= binaryProfile_.bytes.size()) {
        binaryProfileStage_ = BinaryProfileStage::waiting_commit;
        if (!sendCommand(QStringLiteral("PROFILE BINCOMMIT %1").arg(binaryProfile_.transaction))) {
            failBinaryProfileDeployment(QStringLiteral("Could not commit the staged binary profile."));
            return false;
        }
        return true;
    }

    const qsizetype remaining = binaryProfile_.bytes.size() - binaryProfile_.offset;
    const qsizetype count = std::min(kChunkBytes, remaining);
    const QByteArray chunk = binaryProfile_.bytes.mid(binaryProfile_.offset, count).toHex().toUpper();
    binaryProfile_.expectedReceived = binaryProfile_.offset + count;
    binaryProfileStage_ = BinaryProfileStage::waiting_chunk;
    if (!sendCommand(QStringLiteral("PROFILE BINCHUNK %1 %2 %3")
            .arg(binaryProfile_.transaction)
            .arg(binaryProfile_.offset)
            .arg(QString::fromLatin1(chunk)))) {
        failBinaryProfileDeployment(QStringLiteral("Could not send a binary profile chunk."));
        return false;
    }
    return true;
}

bool DeviceController::processBinaryProfileLine(const QString& line) {
    if (!profileDeploying_ || binaryProfileStage_ == BinaryProfileStage::idle) return false;
    if (!binaryProfile_.bootId.isEmpty() &&
        binaryProfile_.bootId.compare(identity_.bootId, Qt::CaseInsensitive) != 0) {
        failBinaryProfileDeployment(QStringLiteral(
            "Firmware boot changed during binary profile transaction; stale ACK refused."));
        return true;
    }

    auto match = kBinaryStatusExpression.match(line);
    if (match.hasMatch() && binaryProfileStage_ == BinaryProfileStage::waiting_status) {
        const bool active = match.captured(1) == QStringLiteral("1");
        bool transactionOk = false;
        bool lastOk = false;
        const quint32 transaction = match.captured(2).toUInt(&transactionOk);
        const quint32 last = match.captured(3).toUInt(&lastOk);
        if (!transactionOk || !lastOk || (active && transaction == 0U)) {
            failBinaryProfileDeployment(QStringLiteral("Firmware returned malformed binary staging status."));
            return true;
        }
        binaryProfile_.lastObservedTransaction = std::max(last, transaction);
        emit profileDeploymentProgress();
        if (active) {
            binaryProfile_.transaction = transaction;
            binaryProfileStage_ = BinaryProfileStage::waiting_abort;
            if (!sendCommand(QStringLiteral("PROFILE BINABORT %1").arg(transaction))) {
                failBinaryProfileDeployment(QStringLiteral("Could not clear stale firmware profile staging."));
            }
        } else if (identity_.capabilities.contains(
                       QStringLiteral("PROFILE-REUSE-V1"), Qt::CaseInsensitive)) {
            // An already armed and byte-identical profile is safe to reuse.
            // Require a fresh full PROFILE SHOW followed by matching digest
            // from the *same* committed generation before accepting it.
            binaryProfileStage_ = BinaryProfileStage::waiting_reuse;
            if (!sendCommand(QStringLiteral("PROFILE SHOW"))) {
                failBinaryProfileDeployment(
                    QStringLiteral("Could not query active profile for safe reuse."));
            }
        } else {
            static_cast<void>(beginBinaryProfileTransaction(last));
        }
        return true;
    }

    if (binaryProfileStage_ == BinaryProfileStage::waiting_reuse) {
        const auto existing = kProfileReadbackExpression.match(line);
        if (existing.hasMatch()) {
            bool appOk = false, rateOk = false, modulusOk = false, confOk = false;
            const quint32 appId = existing.captured(3).toUInt(&appOk, 16);
            const quint32 rate = existing.captured(4).toUInt(&rateOk);
            const quint32 modulus = existing.captured(5).toUInt(&modulusOk);
            const quint32 confRev = existing.captured(6).toUInt(&confOk);
            binaryProfile_.reusedReadbackGeneration = existing.captured(1);
            binaryProfile_.reusedReadbackMatched =
                appOk && rateOk && modulusOk && confOk &&
                existing.captured(2) == binaryProfile_.svId &&
                appId == binaryProfile_.appId &&
                rate == binaryProfile_.rate &&
                modulus == binaryProfile_.modulus &&
                confRev == binaryProfile_.confRev;
            return true;
        }
        const auto fingerprint = kBinaryIdentityExpression.match(line);
        if (fingerprint.hasMatch()) {
            bool lengthOk = false, hashOk = false;
            const quint64 hash = fingerprint.captured(2).toULongLong(&hashOk, 16);
            const quint64 length = fingerprint.captured(3).toULongLong(&lengthOk);
            if (binaryProfile_.reusedReadbackMatched &&
                hashOk && lengthOk &&
                hash == binaryProfile_.recordHash &&
                length == static_cast<quint64>(binaryProfile_.bytes.size()) &&
                fingerprint.captured(1) == binaryProfile_.reusedReadbackGeneration &&
                !identity_.bootId.isEmpty()) {
                profileGeneration_ = fingerprint.captured(1);
                resetBinaryProfileTransfer();
                profileDeploying_ = false;
                profileArmed_ = true;
                profileWasReused_ = true;
                emit profileStateChanged();
                emit deviceMessage(QStringLiteral(
                    "Existing canonical 4I+4V profile confirmed from matching firmware digest and readback; no upload needed."));
                return true;
            }
            // Stale or different profile: never arm. Deploy a fresh canonical
            // record via the old bounded firmware transaction instead.
            static_cast<void>(beginBinaryProfileTransaction(binaryProfile_.lastObservedTransaction));
            return true;
        }
    }

    match = kBinaryAbortExpression.match(line);
    if (match.hasMatch() && binaryProfileStage_ == BinaryProfileStage::waiting_abort) {
        bool txOk = false;
        const quint32 transaction = match.captured(1).toUInt(&txOk);
        if (!txOk || transaction != binaryProfile_.transaction) {
            failBinaryProfileDeployment(QStringLiteral("Firmware BINABORT acknowledgement did not match the stale transaction."));
            return true;
        }
        emit profileDeploymentProgress();
        static_cast<void>(beginBinaryProfileTransaction(binaryProfile_.lastObservedTransaction));
        return true;
    }

    match = kBinaryBeginExpression.match(line);
    if (match.hasMatch() && binaryProfileStage_ == BinaryProfileStage::waiting_begin) {
        bool txOk = false;
        bool sizeOk = false;
        const quint32 transaction = match.captured(1).toUInt(&txOk);
        const qulonglong bytes = match.captured(2).toULongLong(&sizeOk);
        if (!txOk || !sizeOk || transaction != binaryProfile_.transaction ||
            bytes != static_cast<qulonglong>(binaryProfile_.bytes.size())) {
            failBinaryProfileDeployment(QStringLiteral("Firmware BINBEGIN acknowledgement did not match the requested transaction."));
            return true;
        }
        binaryProfile_.offset = 0;
        emit profileDeploymentProgress();
        static_cast<void>(sendNextBinaryProfileChunk());
        return true;
    }

    match = kBinaryChunkExpression.match(line);
    if (match.hasMatch() && binaryProfileStage_ == BinaryProfileStage::waiting_chunk) {
        bool txOk = false;
        bool receivedOk = false;
        const quint32 transaction = match.captured(1).toUInt(&txOk);
        const qulonglong received = match.captured(2).toULongLong(&receivedOk);
        if (!txOk || !receivedOk || transaction != binaryProfile_.transaction ||
            received != static_cast<qulonglong>(binaryProfile_.expectedReceived)) {
            failBinaryProfileDeployment(QStringLiteral("Firmware BINCHUNK acknowledgement did not match the requested byte range."));
            return true;
        }
        binaryProfile_.offset = binaryProfile_.expectedReceived;
        emit profileDeploymentProgress();
        static_cast<void>(sendNextBinaryProfileChunk());
        return true;
    }

    match = kBinaryCommitExpression.match(line);
    if (match.hasMatch() && binaryProfileStage_ == BinaryProfileStage::waiting_commit) {
        bool txOk = false;
        bool appOk = false;
        bool rateOk = false;
        bool modulusOk = false;
        const quint32 transaction = match.captured(1).toUInt(&txOk);
        const quint32 appId = match.captured(4).toUInt(&appOk, 16);
        const quint32 rate = match.captured(5).toUInt(&rateOk);
        const quint32 modulus = match.captured(6).toUInt(&modulusOk);
        if (!txOk || !appOk || !rateOk || !modulusOk ||
            transaction != binaryProfile_.transaction ||
            match.captured(3) != binaryProfile_.svId ||
            appId != binaryProfile_.appId ||
            rate != binaryProfile_.rate ||
            modulus != binaryProfile_.modulus) {
            failBinaryProfileDeployment(QStringLiteral("Firmware BINCOMMIT acknowledgement does not match the compiled transaction."));
            return true;
        }
        binaryProfile_.committedGeneration = match.captured(2);
        binaryProfileStage_ = BinaryProfileStage::waiting_readback;
        emit profileDeploymentProgress();
        if (!sendCommand(QStringLiteral("PROFILE SHOW"))) {
            failBinaryProfileDeployment(QStringLiteral("Could not verify the committed binary profile."));
        }
        return true;
    }

    match = kProfileReadbackExpression.match(line);
    if (match.hasMatch() && binaryProfileStage_ == BinaryProfileStage::waiting_readback) {
        bool appOk = false;
        bool rateOk = false;
        bool modulusOk = false;
        bool confOk = false;
        const quint32 appId = match.captured(3).toUInt(&appOk, 16);
        const quint32 rate = match.captured(4).toUInt(&rateOk);
        const quint32 modulus = match.captured(5).toUInt(&modulusOk);
        const quint32 confRev = match.captured(6).toUInt(&confOk);
        if (!appOk || !rateOk || !modulusOk || !confOk ||
            match.captured(1) != binaryProfile_.committedGeneration ||
            match.captured(2) != binaryProfile_.svId ||
            appId != binaryProfile_.appId ||
            rate != binaryProfile_.rate ||
            modulus != binaryProfile_.modulus ||
            confRev != binaryProfile_.confRev) {
            failBinaryProfileDeployment(QStringLiteral("Firmware PROFILE SHOW readback differs from the compiled binary profile."));
            return true;
        }

        profileGeneration_ = match.captured(1);
        resetBinaryProfileTransfer();
        profileDeploying_ = false;
        profileArmed_ = true;
        emit profileStateChanged();
        emit deviceMessage(QStringLiteral("Canonical binary V1 profile committed and verified."));
        return true;
    }

    if (line.contains(QStringLiteral("Unknown command"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("Discarded invalid"), Qt::CaseInsensitive) ||
        (line.contains(QStringLiteral("PROFILE"), Qt::CaseInsensitive) &&
         line.contains(QStringLiteral("rejected"), Qt::CaseInsensitive))) {
        failBinaryProfileDeployment(
            QStringLiteral("Firmware console rejected a command during binary profile transfer "
                           "(possible serial line corruption): ") + line.right(180));
        return true;
    }

    return false;
}

bool DeviceController::setCtSaturation(
    const bool enabled,
    const double dcOffsetPercent,
    const double harmonicPercent,
    const int harmonicOrder,
    const double clipPercent) {
    if (enabled && signalFrequencyHz_ == 0.0) {
        setError(QStringLiteral("CT saturation shaping requires AC mode."));
        return false;
    }
    if (!std::isfinite(dcOffsetPercent) || dcOffsetPercent < -300.0 || dcOffsetPercent > 300.0 ||
        !std::isfinite(harmonicPercent) || harmonicPercent < 0.0 || harmonicPercent > 300.0 ||
        harmonicOrder < 2 || harmonicOrder > 63 || !std::isfinite(clipPercent) ||
        clipPercent < 1.0 || clipPercent > 1000.0) {
        setError(QStringLiteral("CT saturation parameters are outside the bounded lab range."));
        return false;
    }
    return sendCommand(QStringLiteral("SHAPE CT %1 %2 %3 %4 %5")
        .arg(enabled ? 1 : 0)
        .arg(std::llround(dcOffsetPercent * 10.0))
        .arg(std::llround(harmonicPercent * 10.0))
        .arg(harmonicOrder)
        .arg(std::llround(clipPercent * 10.0)));
}

bool DeviceController::sendPtpShow() { return sendCommand(QStringLiteral("PTP SHOW")); }
bool DeviceController::startPtp() {
    // Legacy direct START must not bypass a guided START, in-flight STOP,
    // or an unverified cancellation. Both paths share this device authority.
    if (!deviceVerified_ || !ptpAvailable_ || ptpRunning_ || ptpStarting()) {
        setError(QStringLiteral("PTP must be verified and stopped before START."));
        return false;
    }
    return sendCommand(QStringLiteral("PTP START"));
}
bool DeviceController::stopPtp() {
    if (!connected_ || !ioWorkerReady_ || !deviceVerified_) {
        setError(QStringLiteral("Connect the verified ESP32-P4 before stopping PTP."));
        return false;
    }
    return beginPtpStopVerification();
}

QString DeviceController::ptpConfigCommand(const QVariantMap& profile) {
    const auto parse = [&profile](const char* key, const unsigned maximum, unsigned& out) {
        const QVariant value = profile.value(QString::fromLatin1(key));
        bool valid = false;
        const uint number = value.toUInt(&valid);
        if (!valid || number > maximum) return false;
        out = number;
        return true;
    };
    unsigned domain{}, transport{}, vlan{}, pcp{}, announce{}, sync{};
    if (!parse("domain", 255U, domain) ||
        !parse("transportSpecific", 15U, transport) ||
        !parse("vlanId", 4094U, vlan) ||
        !parse("vlanPriority", 7U, pcp) ||
        !parse("announceIntervalMs", 10000U, announce) ||
        !parse("syncIntervalMs", 5000U, sync) ||
        announce < 100U || sync < 20U) {
        return {};
    }
    QString command = QStringLiteral("PTP CONFIG %1 %2 %3 %4 %5 %6 %7 %8")
        .arg(domain).arg(transport)
        .arg(profile.value(QStringLiteral("vlanEnabled")).toBool() ? 1 : 0)
        .arg(vlan).arg(pcp).arg(announce).arg(sync)
        .arg(profile.value(QStringLiteral("respondToPeerDelay")).toBool() ? 1 : 0);
    if (profile.value(QStringLiteral("benchGlobalGm")).toBool()) {
        // Studio supplies a fresh host UTC epoch. The device then uses
        // one EMAC IEEE1588 timebase for both PTP TX and SV sampling.
        command += QStringLiteral(" 1 %1").arg(QDateTime::currentMSecsSinceEpoch());
    }
    return command;
}

void DeviceController::resetPtpStartup() noexcept {
    ptpStartTimer_.stop();
    ptpStopPollTimer_.stop();
    ptpStartStage_ = PtpStartStage::idle;
    pendingPtpRole_.clear();
    pendingPtpConfig_.clear();
    ptpRuntimeRoleSeen_ = false;
    ptpRuntimeRunningSeen_ = false;
}

bool DeviceController::beginPtpStopVerification() {
    ptpStartTimer_.stop();
    ptpStopPollTimer_.stop();
    ptpStartStage_ = PtpStartStage::awaiting_stop_ack;
    pendingPtpRole_.clear();
    pendingPtpConfig_.clear();
    ptpRuntimeRoleSeen_ = false;
    ptpRuntimeRunningSeen_ = false;
    ptpStatus_ = QStringLiteral("Stopping PTP; waiting for board confirmation...");
    emit ptpStateChanged();
    // Emergency STOP has one reserved slot in the existing serial owner and
    // eliminates unsent START commands, even when the normal queue is full.
    if (!sendCommand(QStringLiteral("PTP STOP"))) {
        markPtpStopUnconfirmed(QStringLiteral("Could not queue safety PTP STOP."));
        return false;
    }
    // Absolute STOP readback deadline; STOP itself uses the worker safety lane.
    ptpStartTimer_.start(profileDeploying_ || scenarioStage_ != ScenarioStage::idle
        ? 12000 : 5000);
    return true;
}

void DeviceController::markPtpStopUnconfirmed(const QString& reason) {
    ptpStartTimer_.stop();
    ptpStopPollTimer_.stop();
    // Failed verification is not proof that the transmitter stopped. Block
    // new START intents until an explicit STOP retry or a new session.
    ptpStartStage_ = PtpStartStage::stop_unconfirmed;
    ptpStatus_ = QStringLiteral("PTP STOP unverified — output may still be active");
    emit ptpStateChanged();
    setError(reason);
}

void DeviceController::failPtpStartup(const QString& reason, const bool rollback) {
    ptpLastStartFailure_ = reason;
    emit ptpStateChanged();
    setError(reason);
    if (rollback && connected_ && sessionGeneration_ != 0) {
        static_cast<void>(beginPtpStopVerification());
        return;
    }
    resetPtpStartup();
    ptpRunning_ = false;
    ptpStatus_ = QStringLiteral("PTP start failed: %1").arg(reason);
    emit ptpStateChanged();
}

bool DeviceController::startPtpConfigured(const QString& requestedRole,
                                           const QVariantMap& profile) {
    const QString role = requestedRole.trimmed().toUpper();
    if (!deviceVerified_ || !connected_ || !ioWorkerReady_ || !ptpAvailable_ ||
        ptpRunning_ || ptpStarting() ||
        (role != QStringLiteral("SOURCE") &&
         role != QStringLiteral("RECEIVER") &&
         role != QStringLiteral("MONITOR"))) {
        setError(QStringLiteral("Connect a PTP-capable ESP32-P4 and stop timing before starting."));
        return false;
    }
    if (profile.value(QStringLiteral("benchGlobalGm")).toBool() &&
        (role != QStringLiteral("SOURCE") ||
         !identity_.capabilities.contains(QStringLiteral("PTP-GM-BENCH-V1"),
                                          Qt::CaseInsensitive))) {
        setError(QStringLiteral("Bench GM requires SOURCE and matched PTP-GM-BENCH-V1 firmware."));
        return false;
    }
    const QString command = ptpConfigCommand(profile);
    if (command.isEmpty()) {
        const QString reason = QStringLiteral(
            "Invalid PTP settings. Check domain, VLAN and timing intervals.");
        ptpLastStartFailure_ = reason;
        ptpStatus_ = QStringLiteral("PTP profile rejected: %1").arg(reason);
        emit ptpStateChanged();
        setError(reason);
        return false;
    }
    const QString contractError = role == QStringLiteral("SOURCE")
        ? sourcePtpProfileError(profile) : QString{};
    if (!contractError.isEmpty()) {
        ptpLastStartFailure_ = contractError;
        ptpStatus_ = QStringLiteral("PTP profile rejected: %1").arg(contractError);
        emit ptpStateChanged();
        setError(contractError);
        return false;
    }
    ptpLastStartFailure_.clear();
    pendingPtpRole_ = role;
    pendingPtpConfig_ = command;
    // Verify actual firmware runtime before selecting another role. A stale
    // local OFF flag after reconnect must not provoke a rejected PTPROLE or
    // force-stop a transmitter still running on the board.
    ptpStartStage_ = PtpStartStage::awaiting_preflight;
    ptpStatus_ = QStringLiteral("Checking actual board PTP runtime before START...");
    emit ptpStateChanged();
    if (!sendPtpShow()) {
        failPtpStartup(QStringLiteral("Could not queue PTP preflight readback."), false);
        return false;
    }
    ptpStartTimer_.start(5000);
    return true;
}

void DeviceController::advancePtpStartup(const QString& line) {
    if (!ptpStarting() || ptpStartStage_ == PtpStartStage::stop_unconfirmed) return;
    if (ptpStartStage_ == PtpStartStage::awaiting_preflight) {
        // PTP SHOW provides measured PTP2 role before status. Only adopt an
        // already-active transmitter if THIS preflight saw the expected role;
        // historical UI role cannot authorize mutation or claim an exact role.
        const auto roleProof = kPtpMeasuredStateExpression.match(line);
        if (roleProof.hasMatch())
            ptpRuntimeRoleSeen_ = roleProof.captured(1).toUpper() == pendingPtpRole_;
        const auto actual = kPtpStatusExpression.match(line);
        if (!actual.hasMatch()) return;
        if (actual.captured(1).compare(QStringLiteral("RUNNING"), Qt::CaseInsensitive) == 0) {
            if (ptpRuntimeRoleSeen_) {
                const QString existingRole = pendingPtpRole_;
                resetPtpStartup();
                ptpStatus_ = QStringLiteral("PTP %1 already active on board; external sync unqualified").arg(existingRole);
                emit ptpStateChanged();
                return; // No redundant role/config/start, no surprise STOP.
            }
            ptpLastStartFailure_ = QStringLiteral(
                "Board PTP is already RUNNING. Click Stop PTP, wait for verified STOPPED, then retry Start.");
            ptpStatus_ = ptpLastStartFailure_;
            resetPtpStartup();
            emit ptpStateChanged();
            setError(ptpLastStartFailure_);
            return;
        }
        if (actual.captured(1).compare(QStringLiteral("STOPPED"), Qt::CaseInsensitive) != 0)
            return;
        ptpStartStage_ = PtpStartStage::awaiting_role_ack;
        ptpStatus_ = QStringLiteral("Board STOPPED confirmed. Selecting PTP mode...");
        emit ptpStateChanged();
        if (!sendCommand(QStringLiteral("PROFILE PTPROLE %1").arg(pendingPtpRole_))) {
            failPtpStartup(QStringLiteral("Could not queue PTP role selection after STOPPED readback."), false);
            return;
        }
        ptpStartTimer_.start(5000);
        return;
    }
    if (ptpStartStage_ == PtpStartStage::awaiting_stop_ack) {
        // Firmware emits "PTP stop requested", not "PTP stop accepted".
        // A stale STOPPED status from an older SHOW cannot confirm this STOP.
        if (line.contains(QStringLiteral("PTP stop requested"), Qt::CaseInsensitive)) {
            ptpStartStage_ = PtpStartStage::awaiting_stop_readback;
            ptpStatus_ = QStringLiteral("Checking that board PTP is stopped...");
            emit ptpStateChanged();
            if (!sendPtpShow()) {
                markPtpStopUnconfirmed(QStringLiteral(
                    "PTP STOP readback could not be queued. Check USB connection / Copy diagnostics."));
                return;
            }
            // Preserve the absolute deadline from STOP dispatch; do not
            // restart it on ACK while another transaction owns the console.
        }
        return;
    }
    if (ptpStartStage_ == PtpStartStage::awaiting_stop_readback) {
        const auto readback = kPtpStatusExpression.match(line);
        // A queued pre-STOP SHOW can arrive while post-STOP readback is
        // still deferred. It cannot verify the requested STOP.
        if (!readback.hasMatch() || deferredPtpShow_) return;
        if (readback.captured(1).compare(QStringLiteral("STOPPED"), Qt::CaseInsensitive) == 0) {
            resetPtpStartup();
            ptpRunning_ = false;
            ptpStatus_ = QStringLiteral("PTP stopped — confirmed by board");
            emit ptpStateChanged();
        } else if (!ptpStopPollTimer_.isActive()) {
            // Firmware stop is asynchronous. Poll on Qt timers without
            // extending the absolute five-second verification deadline.
            ptpStopPollTimer_.start(250);
        }
        return;
    }
    const bool rejected =
        line.contains(QStringLiteral("PTPROLE rejected"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("PTPROLE configuration unavailable"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("PTP configuration rejected"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("PTP start rejected"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("PTP unavailable"), Qt::CaseInsensitive);
    if (rejected) {
        if (line.contains(QStringLiteral("PTPROLE rejected: stop PTP before changing operating role"),
                          Qt::CaseInsensitive)) {
            // A physical role rejection is positive evidence the timing runtime
            // is active. Do not reset it to OFF, and never auto-stop a source
            // that the current Studio transaction has not proven it owns.
            ptpLastStartFailure_ = QStringLiteral("Board PTP is still RUNNING: ") + line;
            resetPtpStartup();
            ptpRunning_ = true;
            ptpStatus_ = QStringLiteral("PTP active on board; verify STOP before changing role");
            emit ptpStateChanged();
            setError(ptpLastStartFailure_);
            return;
        }
        const bool phyDown = line.contains(QStringLiteral("source_reason=ETHERNET_LINK_DOWN"),
                                           Qt::CaseInsensitive);
        const bool driverDown = line.contains(QStringLiteral("source_reason=ETHERNET_DRIVER_"),
                                              Qt::CaseInsensitive);
        const bool txInvalid = line.contains(QStringLiteral("source_reason=ETHERNET_TX_INVALID_STATE"),
                                             Qt::CaseInsensitive);
        // A preflight rejection happens BEFORE any PTP runtime is launched;
        // never send a redundant STOP that could race a later user action.
        const bool rollback = !phyDown && !driverDown &&
            (ptpStartStage_ == PtpStartStage::awaiting_start ||
             ptpStartStage_ == PtpStartStage::awaiting_runtime);
        const QString step = ptpStartPhase();
        const QString actionable = phyDown
            ? QStringLiteral("Ethernet PHY link is DOWN. Connect a live switch/cable to the ESP32-P4 Ethernet port, wait for LINK LED, then retry PTP START.")
            : driverDown
            ? QStringLiteral("ESP32-P4 Ethernet driver is not STARTED. Inspect hardware Ethernet initialization and reset the board if the driver remains stopped.")
            : txInvalid
            ? QStringLiteral("Ethernet reports link but EMAC rejected PTP transmission (ESP_ERR_INVALID_STATE). Inspect PTP ETH readback and firmware/driver state; PTP remains OFF.")
            : QStringLiteral("Board rejected PTP %1 command: %2").arg(step, line);
        appendLog(QStringLiteral("PTP"), QStringLiteral(
            "START_REJECTED step=%1 preflight_link_down=%2 driver_down=%3 tx_invalid=%4 cause=%5")
            .arg(step).arg(phyDown ? 1 : 0).arg(driverDown ? 1 : 0)
            .arg(txInvalid ? 1 : 0).arg(line));
        failPtpStartup(actionable, rollback);
        return;
    }
    if (ptpStartStage_ == PtpStartStage::awaiting_role_ack) {
        // A SHOW response cannot replace the explicit PTPROLE acceptance.
        if (line.contains(QStringLiteral("PTPROLE accepted"), Qt::CaseInsensitive)) {
            ptpStartStage_ = PtpStartStage::awaiting_role_readback;
            ptpStatus_ = QStringLiteral("Confirming PTP mode on board...");
            emit ptpStateChanged();
            // Explicit role readback is mandatory; ACK alone cannot prove
            // the firmware role. Do not rely on unrelated periodic polling.
            if (!sendCommand(QStringLiteral("PROFILE SHOW"))) {
                failPtpStartup(QStringLiteral("Could not request verified PTP role readback."), false);
                return;
            }
            ptpStartTimer_.start(5000);
        }
        return;
    }
    if (ptpStartStage_ == PtpStartStage::awaiting_role_readback) {
        const auto reply = kPtpRoleExpression.match(line);
        if (!reply.hasMatch()) return;
        // A prior PROFILE SHOW can emit old role telemetry after the ACK.
        // Wait for the selected role; fail only if it is itself still running.
        if (reply.captured(1).toUpper() != pendingPtpRole_) return;
        if (reply.captured(3) != QStringLiteral("0")) {
            failPtpStartup(QStringLiteral("Selected PTP role is still running."), false);
            return;
        }
        const QString command = pendingPtpConfig_;
        ptpStartStage_ = PtpStartStage::awaiting_config;
        ptpStatus_ = QStringLiteral("Applying PTP settings...");
        emit ptpStateChanged();
        if (!sendCommand(command)) {
            failPtpStartup(QStringLiteral("Could not queue PTP configuration."), false);
            return;
        }
        ptpStartTimer_.start(5000);
    } else if (ptpStartStage_ == PtpStartStage::awaiting_config &&
               line.contains(QStringLiteral("PTP configuration accepted"), Qt::CaseInsensitive)) {
        ptpStartStage_ = PtpStartStage::awaiting_start;
        ptpStatus_ = QStringLiteral("Starting PTP runtime...");
        emit ptpStateChanged();
        if (!sendCommand(QStringLiteral("PTP START"))) {
            failPtpStartup(QStringLiteral("Could not queue PTP START."), true);
            return;
        }
        ptpStartTimer_.start(10000);
    } else if (ptpStartStage_ == PtpStartStage::awaiting_start &&
               line.contains(QStringLiteral("PTP start accepted"), Qt::CaseInsensitive)) {
        ptpStartStage_ = PtpStartStage::awaiting_runtime;
        ptpRuntimeRoleSeen_ = false;
    ptpRuntimeRunningSeen_ = false;
        ptpStatus_ = QStringLiteral("Verifying live PTP role and runtime...");
        emit ptpStateChanged();
        if (!sendPtpShow()) {
            failPtpStartup(QStringLiteral("Could not read back PTP runtime."), true);
            return;
        }
        ptpStartTimer_.start(5000);
    } else if (ptpStartStage_ == PtpStartStage::awaiting_runtime) {
        // PTP SHOW may deliver RUNNING before periodic PTP2 role telemetry.
        // Collect both independent facts in any order, under the same bounded
        // five-second deadline. Missing evidence is NOT an immediate failure
        // and NEVER an automatic success. Wrong role remains fail-closed.
        const auto roleEvidence = kPtpMeasuredStateExpression.match(line);
        if (roleEvidence.hasMatch()) {
            if (roleEvidence.captured(1).toUpper() != pendingPtpRole_) {
                failPtpStartup(QStringLiteral("PTP runtime role differs from the selected mode."), true);
                return;
            }
            ptpRuntimeRoleSeen_ = true;
        }
        const auto readback = kPtpStatusExpression.match(line);
        if (readback.hasMatch()) {
            if (readback.captured(1).compare(QStringLiteral("RUNNING"), Qt::CaseInsensitive) == 0)
                ptpRuntimeRunningSeen_ = true;
            else {
                failPtpStartup(QStringLiteral("PTP START acknowledged but board readback is STOPPED."), true);
                return;
            }
        }
        if (!ptpRuntimeRoleSeen_ || !ptpRuntimeRunningSeen_ ||
            !ptpRunning_ || ptpRole_ != pendingPtpRole_) return;
        resetPtpStartup();
        ptpStatus_ = QStringLiteral("PTP runtime active; external timing still unqualified");
        emit ptpStateChanged();
    }
}

bool DeviceController::configurePtp(const QVariantMap& profile) {
    if (!deviceVerified_ || ptpRunning_ || ptpStarting()) {
        setError(QStringLiteral("Stop PTP and finish pending operations before changing settings."));
        return false;
    }
    if (profile.value(QStringLiteral("benchGlobalGm")).toBool() &&
        (ptpRole_ != QStringLiteral("SOURCE") ||
         !identity_.capabilities.contains(QStringLiteral("PTP-GM-BENCH-V1"),
                                          Qt::CaseInsensitive))) {
        setError(QStringLiteral("BENCH GM requires SOURCE role and PTP-GM-BENCH-V1 firmware."));
        return false;
    }
    const QString command = ptpConfigCommand(profile);
    if (command.isEmpty()) {
        setError(QStringLiteral("PTP expert profile is outside firmware safety bounds."));
        return false;
    }
    if (ptpRole_ == QStringLiteral("SOURCE")) {
        const QString contractError = sourcePtpProfileError(profile);
        if (!contractError.isEmpty()) {
            setError(contractError);
            return false;
        }
    }
    return sendCommand(command);
}

void DeviceController::clearLog() {
    logUpdateTimer_.stop();
    if (logText_.isEmpty()) return;
    logText_.clear();
    emit logTextChanged();
}

bool DeviceController::sendCommand(const QString& command) {
    return sendCommandBatch({command}, false);
}

bool DeviceController::sendQuietCommand(const QString& command) {
    return sendCommandBatch({command}, true);
}

bool DeviceController::sendCommandBatch(const QStringList& commands, const bool quiet, const bool exclusive) {
    const bool single = !exclusive && commands.size() == 1;
    const QString onlyCommand = single ? commands.constFirst() : QString{};
    const bool readback = onlyCommand == QStringLiteral("SHOW") ||
        onlyCommand == QStringLiteral("PROFILE SHOW") ||
        onlyCommand == QStringLiteral("PTP SHOW");
    const auto deferReadback = [this, &onlyCommand] {
        bool* pending = onlyCommand == QStringLiteral("SHOW") ? &deferredShow_ :
            onlyCommand == QStringLiteral("PROFILE SHOW") ? &deferredProfileShow_ : &deferredPtpShow_;
        if (!*pending) {
            *pending = true;
            appendLog(QStringLiteral("·"),
                QStringLiteral("Coalesced %1 until exclusive serial authority is released.").arg(onlyCommand));
        }
        return true;
    };
    if (profileDeploying_) {
        const bool exactlyOne = !exclusive && commands.size() == 1;
        const QString command = exactlyOne ? commands.constFirst() : QString{};
        const bool protocolReply = command.startsWith(QStringLiteral("PROFILE BIN")) ||
            ((binaryProfileStage_ == BinaryProfileStage::waiting_readback ||
              binaryProfileStage_ == BinaryProfileStage::waiting_reuse) &&
             command == QStringLiteral("PROFILE SHOW"));
        const bool safety = command == QStringLiteral("STOP") ||
            command == QStringLiteral("PTP STOP");
        if (!exactlyOne || (!protocolReply && !safety)) {
            if (readback) return deferReadback();
            setError(QStringLiteral("Device profile is being prepared; wait before changing live setpoints."));
            return false;
        }
    }
    if (scenarioStage_ != ScenarioStage::idle) {
        const bool stagingBatch = exclusive && !commands.isEmpty() &&
            commands.constFirst().startsWith(QStringLiteral("CFG BEGIN "));
        const bool safeCommand = !exclusive && commands.size() == 1 &&
            (commands.constFirst() == QStringLiteral("STOP") ||
             commands.constFirst() == QStringLiteral("PTP STOP") ||
             commands.constFirst() == QStringLiteral("HEARTBEAT") ||
             commands.constFirst().startsWith(QStringLiteral("CFG SHOW ")) ||
             commands.constFirst().startsWith(QStringLiteral("START ")));
        if (!stagingBatch && !safeCommand) {
            if (readback) return deferReadback();
            setError(QStringLiteral("SV configuration is being verified; wait before changing live setpoints."));
            return false;
        }
    }
    if (!connected_ || !ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0) {
        setError(QStringLiteral("Device is not connected."));
        return false;
    }
    const QStringList copy = commands;
    const quint64 generation = sessionGeneration_;
    const bool queued = QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, copy, quiet, exclusive, generation] {
            worker->enqueueCommands(copy, quiet, exclusive, generation);
        },
        Qt::QueuedConnection);
    if (!queued) setError(QStringLiteral("Device queue dispatcher unavailable."));
    return queued;
}

void DeviceController::flushDeferredReadbacks() {
    if (profileDeploying_ || binaryProfileStage_ != BinaryProfileStage::idle ||
        scenarioStage_ != ScenarioStage::idle ||
        !connected_ || !ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0)
        return;
    // At most one of each telemetry/readback type. No competing serial writer.
    if (std::exchange(deferredPtpShow_, false)) {
        if (!sendQuietCommand(QStringLiteral("PTP SHOW")) &&
            ptpStartStage_ == PtpStartStage::awaiting_stop_readback)
            markPtpStopUnconfirmed(QStringLiteral(
                "PTP STOP readback could not be sent. Check USB connection / Copy diagnostics."));
    }
    if (std::exchange(deferredShow_, false))
        static_cast<void>(sendQuietCommand(QStringLiteral("SHOW")));
    if (std::exchange(deferredProfileShow_, false))
        static_cast<void>(sendQuietCommand(QStringLiteral("PROFILE SHOW")));
}

void DeviceController::setSessionHeartbeatEnabled(const bool enabled) {
    if (!ioWorkerReady_ || ioWorker_ == nullptr || sessionGeneration_ == 0) return;
    const quint64 generation = sessionGeneration_;
    QMetaObject::invokeMethod(
        ioWorker_,
        [worker = ioWorker_, enabled, generation] {
            worker->setHeartbeatEnabled(enabled, generation);
        },
        Qt::QueuedConnection);
}

void DeviceController::resetScenarioStart() noexcept {
    const bool wasPending = scenarioStage_ != ScenarioStage::idle;
    scenarioTimer_.stop();
    scenarioStage_ = ScenarioStage::idle;
    scenarioLiveUpdate_ = false;
    scenarioPendingFrequencyHz_ = 0.0;
    scenarioToken_ = 0;
    scenarioHash_ = 0;
    scenarioSessionGeneration_ = 0;
    scenarioSignalGeneration_ = 0;
    scenarioBoot_.clear();
    scenarioProfileGeneration_.clear();
    if (wasPending) {
        emit scenarioStateChanged();
        emit scenarioApplicationFinished();
    }
}

void DeviceController::failScenarioStart(const QString& message) {
    if (scenarioStage_ == ScenarioStage::idle) return;
    appendLog(QStringLiteral("SVTX"), QStringLiteral(
        "TXN_FAILURE session=%1 boot=%2 token=%3 reason=%4")
        .arg(sessionGeneration_).arg(scenarioBoot_).arg(scenarioToken_).arg(message));
    // An unverified START may have armed the publisher: fail closed STOP.
    // A live setpoint failure must not silently stop an existing healthy
    // stream; the old wire state remains authoritative until readback proves
    // otherwise. Surface failure, leave operator draft pending.
    const bool wasLiveUpdate = scenarioLiveUpdate_;
    resetScenarioStart();
    if (!wasLiveUpdate) {
        appendLog(QStringLiteral("SVTX"), QStringLiteral(
            "FAIL_SAFE_STOP_REQUEST session=%1 reason=unverified_start")
            .arg(sessionGeneration_));
        static_cast<void>(sendCommand(QStringLiteral("STOP")));
    }
    setError(message);
}

void DeviceController::setRunning(const bool value) {
    if (running_ == value) return;
    appendLog(QStringLiteral("SVTX"), QStringLiteral(
        "RUNNING_EDGE previous=%1 next=%2 operator_stop=%3 session=%4 boot=%5 stop_reason=%6 last_cause=%7")
        .arg(running_ ? 1 : 0).arg(value ? 1 : 0)
        .arg(operatorStopPending_ ? 1 : 0).arg(sessionGeneration_)
        .arg(identity_.bootId).arg(svStopReason_).arg(svLastStopCause_));
    if (running_ && !value && !operatorStopPending_ &&
        connected_ && deviceVerified_ && scenarioStage_ == ScenarioStage::idle) {
        // A single forensic readback, queued *after* the observed STOP, gives
        // SVLIFE stop_seq/heartbeat age from the actual board. Do not issue
        // an automatic STOP or START and do not hide the transition in UI.
        const auto generation = sessionGeneration_;
        const auto boot = identity_.bootId;
        QTimer::singleShot(0, this, [this, generation, boot] {
            if (connected_ && deviceVerified_ && sessionGeneration_ == generation &&
                identity_.bootId == boot && !running_)
                static_cast<void>(sendQuietCommand(QStringLiteral("SHOW")));
        });
    }
    if (!value && running_ && !operatorStopPending_ && svStopReason_.isEmpty()) {
        // Do not silently turn a physical STOP into a green READY state.
        // SHOW/SVLIFE will replace this interim state with the board cause.
        svStopReason_ = QStringLiteral("AWAITING_BOARD_CAUSE");
        emit svStopReasonChanged();
    }
    running_ = value;
    emit runningChanged();
}

void DeviceController::setIdentificationState(const IdentificationState state) {
    if (identificationState_ == state) return;
    identificationState_ = state;
    emit identificationStateChanged();
}

void DeviceController::setDiscoveryState(const QString& status, const bool active) {
    if (discoveryStatus_ == status && discovering_ == active) return;
    discoveryStatus_ = status;
    discovering_ = active;
    emit discoveryChanged();
}

void DeviceController::applyIdentity(DeviceIdentity identity) {
    if (identity_ == identity) return;
    identity_ = std::move(identity);
    emit deviceIdentityChanged();
}

void DeviceController::clearIdentity() {
    if (identity_.empty()) return;
    identity_ = {};
    emit deviceIdentityChanged();
}

void DeviceController::markDeviceVerified() {
    if (deviceVerified_) return;
    deviceVerified_ = true;
    recoveryCandidatePort_.clear();
    lastIdentificationPort_ = portName_;
    setIdentificationState(IdentificationState::Verified);
    setDiscoveryState(QStringLiteral("ARStack ESP32-P4 identity verified."), false);
    emit deviceVerifiedChanged();
    emit deviceMessage(QStringLiteral("ESP32-P4 recognized. Device is ready."));

    if (ioWorkerReady_ && ioWorker_ != nullptr && sessionGeneration_ != 0) {
        const quint64 generation = sessionGeneration_;
        QMetaObject::invokeMethod(
            ioWorker_,
            [worker = ioWorker_, generation] { worker->confirmIdentity(generation); },
            Qt::QueuedConnection);
    }
    static_cast<void>(sendShow());
    static_cast<void>(sendCommand(QStringLiteral("PROFILE SHOW")));
    static_cast<void>(sendPtpShow());
}

void DeviceController::setError(const QString& message) {
    if (message.isEmpty() || lastError_ == message) return;
    lastError_ = message;
    appendLog(QStringLiteral("!"), message);
    emit lastErrorChanged();
    emit deviceMessage(message);
}

void DeviceController::appendLog(const QString& direction, const QString& line) {
    // Host UTC is for correlation with PCAP timestamps; not PTP time proof.
    const QString stamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    const QString record = QStringLiteral("[%1] %2 %3\n").arg(stamp, direction, line);
    logText_.append(record);
    constexpr qsizetype kMaxChars = 60000;
    constexpr qsizetype kKeepChars = 48000;
    if (logText_.size() > kMaxChars) logText_ = logText_.right(kKeepChars);

    // Bound disk growth while preserving the previous segment across rollover.
    // Clear in the GUI intentionally does not erase forensic data on disk.
    const QFileInfo info(diagnosticLogPath_);
    QDir directory = info.absoluteDir();
    if (directory.exists() || directory.mkpath(QStringLiteral("."))) {
        constexpr qint64 kMaxLogBytes = 8LL * 1024 * 1024;
        if (info.exists() && info.size() >= kMaxLogBytes) {
            const QString previous = diagnosticLogPath_ + QStringLiteral(".previous");
            static_cast<void>(QFile::remove(previous));
            static_cast<void>(QFile::rename(diagnosticLogPath_, previous));
        }
        QFile file(diagnosticLogPath_);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append))
            static_cast<void>(file.write(record.toUtf8()));
    }
    if (!logUpdateTimer_.isActive()) logUpdateTimer_.start();
}

bool DeviceController::openDiagnosticLogFolder() const {
    const QFileInfo info(diagnosticLogPath_);
    if (!info.absoluteDir().exists()) return false;
#ifdef Q_OS_WIN
    // DeviceController is also linked by the QtCore-only session harness.
    // Start Explorer with an argument vector, not shell interpolation.
    return QProcess::startDetached(QStringLiteral("explorer.exe"),
                                   {QDir::toNativeSeparators(info.absolutePath())});
#else
    return QProcess::startDetached(QStringLiteral("xdg-open"), {info.absolutePath()});
#endif
}

void DeviceController::recordUiDiagnostic(const QString& message) {
    if (!message.trimmed().isEmpty())
        appendLog(QStringLiteral("UI-ERROR"), message.left(1800));
}

void DeviceController::processLine(const QString& rawLine) {
    const QString line = cleanLine(rawLine);
    if (line.isEmpty()) return;
    appendLog(QStringLiteral("←"), line);

    // Explicit on-board flight recorder response. Do not poll from the
    // UI render path or treat historical reports as new lock evidence.
    const auto flightPos = line.indexOf(QStringLiteral("SVSYNC "));
    if (flightPos >= 0) {
        const QString flightLine = line.mid(flightPos).trimmed();
        if (flightLine.startsWith(QStringLiteral("SVSYNC FLIGHT "))) {
            syncFlightLatest_ = flightLine;
            // Firmware emits one bounded fault summary from its telemetry
            // task when a lock edge occurs. Surface it even if Inspect has
            // not been clicked, never as a declaration of physical PASS.
            syncDiagnostics_ = flightLine + QLatin1Char('\n') + syncDiagnostics_;
        }
        if (flightLine.startsWith(QStringLiteral("SVSYNC DIAG ")))
            syncDiagnostics_ = flightLine;
        else if (flightLine.startsWith(QStringLiteral("SVSYNC CAUSE ")) ||
                 flightLine.startsWith(QStringLiteral("SVSYNC EVENT ")))
            syncDiagnostics_ += QLatin1Char('\n') + flightLine;
        // END marks a complete snapshot, never a certification.
        if (syncDiagnostics_.size() > 6000)
            syncDiagnostics_ = syncDiagnostics_.right(6000);
        emit syncDiagnosticsChanged();
    }

    // Only the owner of the current session+boot may advance commit -> readback -> START.
    // Regexes search through ESP-IDF log prefixes; all token fields are matched strictly.
    const auto scenarioCommitted = kScenarioCommitExpression.match(line);
    const auto scenarioReadback = kScenarioReadbackExpression.match(line);
    const auto scenarioStart = kScenarioStartExpression.match(line);
    const auto scenarioMatches = [this](const QRegularExpressionMatch& m) {
        if (!m.hasMatch() || sessionGeneration_ != scenarioSessionGeneration_ ||
            scenarioBoot_.compare(m.captured(4), Qt::CaseInsensitive) != 0 ||
            scenarioProfileGeneration_ != profileGeneration_) return false;
        bool tokenOk = false, hashOk = false, generationOk = false;
        const auto token = m.captured(1).toULongLong(&tokenOk);
        const auto hash = m.captured(2).toULongLong(&hashOk, 16);
        const auto generation = m.captured(3).toULongLong(&generationOk);
        return tokenOk && hashOk && generationOk && token == scenarioToken_ &&
            hash == scenarioHash_ && generation > 0;
    };
    if (scenarioStage_ == ScenarioStage::awaiting_commit && scenarioCommitted.hasMatch()) {
        if (!scenarioMatches(scenarioCommitted)) {
            failScenarioStart(QStringLiteral("SV commit ACK identity/hash mismatch."));
        } else {
            scenarioSignalGeneration_ = scenarioCommitted.captured(3).toULongLong();
            appendLog(QStringLiteral("SVTX"), QStringLiteral(
                "CFG_COMMITTED session=%1 token=%2 signal_generation=%3")
                .arg(sessionGeneration_).arg(scenarioToken_)
                .arg(scenarioSignalGeneration_));
            scenarioStage_ = ScenarioStage::awaiting_readback;
            if (!sendCommand(QStringLiteral("CFG SHOW %1").arg(scenarioToken_)))
                failScenarioStart(QStringLiteral("Unable to request firmware-applied SV readback."));
        }
        return;
    }
    if (scenarioStage_ == ScenarioStage::awaiting_readback && scenarioReadback.hasMatch()) {
        if (!scenarioMatches(scenarioReadback) ||
            scenarioReadback.captured(3).toULongLong() != scenarioSignalGeneration_) {
            failScenarioStart(QStringLiteral("SV firmware readback differs from committed scenario."));
        } else {
            appendLog(QStringLiteral("SVTX"), QStringLiteral(
                "CFG_VERIFIED session=%1 boot=%2 token=%3 signal_generation=%4 mode=%5")
                .arg(sessionGeneration_).arg(scenarioBoot_).arg(scenarioToken_)
                .arg(scenarioSignalGeneration_)
                .arg(scenarioLiveUpdate_ ? QStringLiteral("LIVE") : QStringLiteral("START")));
            if (scenarioLiveUpdate_) {
                signalFrequencyHz_ = scenarioPendingFrequencyHz_;
                // Commit already atomically replaced one signal bank; no
                // START, STOP, phase reset or smpCnt reset is permitted.
                emit liveScenarioApplied();
                resetScenarioStart();
                return;
            }
            scenarioStage_ = ScenarioStage::awaiting_start_ack;
            const QString hash = QString::number(scenarioHash_, 16).toUpper()
                .rightJustified(16, QLatin1Char('0'));
            if (!sendCommand(QStringLiteral("START %1 %2 %3")
                             .arg(scenarioToken_).arg(scenarioBoot_).arg(hash)))
                failScenarioStart(QStringLiteral("Verified SV START command could not be dispatched."));
        }
        return;
    }
    if (line.contains(QStringLiteral("CFG REJECT")) ||
        line.contains(QStringLiteral("CFG COMMIT rejected")) ||
        line.contains(QStringLiteral("CFG READBACK rejected")) ||
        line.contains(QStringLiteral("START rejected: CFG"))) {
        if (scenarioStage_ != ScenarioStage::idle)
            failScenarioStart(QStringLiteral("Firmware rejected SV configuration/START: ") + line);
        return;
    }
    if (scenarioStart.hasMatch()) {
        bool tokenOk = false, hashOk = false, stopSeqOk = false;
        const auto token = scenarioStart.captured(1).toULongLong(&tokenOk);
        const auto hash = scenarioStart.captured(2).toULongLong(&hashOk, 16);
        const auto stopSeq = scenarioStart.captured(3).toULongLong(&stopSeqOk);
        if (scenarioStage_ != ScenarioStage::awaiting_start_ack ||
            scenarioSessionGeneration_ != sessionGeneration_ ||
            scenarioBoot_ != identity_.bootId ||
            !tokenOk || !hashOk || !stopSeqOk ||
            token != scenarioToken_ || hash != scenarioHash_) {
            if (scenarioStage_ != ScenarioStage::idle)
                failScenarioStart(QStringLiteral("Unexpected SV START ACK identity/hash."));
            return;
        }
        operatorStopPending_ = false;
        svStopSeqAtStart_ = stopSeq;
        svStopSeqAtStartValid_ = true;
        if (!svStopReason_.isEmpty()) {
            svStopReason_.clear();
            emit svStopReasonChanged();
        }
        signalFrequencyHz_ = scenarioPendingFrequencyHz_;
        // Publish verified RUNNING before clearing the pending START phase.
        // Otherwise QML can briefly see neither RUNNING nor STARTING and
        // bounce the button back to START during a successful ACK handoff.
        appendLog(QStringLiteral("SVTX"), QStringLiteral(
            "START_ACK_VERIFIED session=%1 boot=%2 token=%3 signal_generation=%4 stop_seq_at_start=%5")
            .arg(sessionGeneration_).arg(scenarioBoot_).arg(scenarioToken_)
            .arg(scenarioSignalGeneration_).arg(svStopSeqAtStart_));
        setRunning(true);
        resetScenarioStart();
        return;
    }

    // SV lifecycle acknowledgements are not PTP lifecycle acknowledgements.
    // Never report 4I+4V output RUNNING merely because PTP START succeeded.
    // Bare/unmatched START text is never evidence of a verified 8-channel START.
    if (line.contains(QStringLiteral("STOP accepted: SV transmission suppressed"), Qt::CaseInsensitive)) {
        // Firmware may emit this prefix for a forced publisher shutdown.
        // Never mislabel a fail-safe board-originated STOP as an operator ACK.
        const bool forced = line.contains(QStringLiteral("fail-closed runtime"),
                                          Qt::CaseInsensitive);
        appendLog(QStringLiteral("SVTX"), QStringLiteral(
            "%1 session=%2 boot=%3 prior_running=%4")
            .arg(forced ? QStringLiteral("RUNTIME_FAIL_SAFE_STOP_OBSERVED")
                        : QStringLiteral("STOP_ACK_OBSERVED"))
            .arg(sessionGeneration_).arg(identity_.bootId).arg(running_ ? 1 : 0));
        if (forced && svStopReason_ != QStringLiteral("RUNTIME_FAULT")) {
            svStopReason_ = QStringLiteral("RUNTIME_FAULT");
            emit svStopReasonChanged();
        }
        operatorStopPending_ = false;
        setRunning(false);
    }
    if (line.contains(QStringLiteral("Control session lease expired"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("Control session heartbeat could not rearm"), Qt::CaseInsensitive) ||
        line.contains(QStringLiteral("SV transport fault:"), Qt::CaseInsensitive)) {
        // Real firmware-originated reasons survive any later STATE=STOPPED
        // poll; the GUI must not attribute these events to the operator.
        appendLog(QStringLiteral("SVTX"), QStringLiteral(
            "FIRMWARE_STOP_CAUSE session=%1 boot=%2 detail=%3")
            .arg(sessionGeneration_).arg(identity_.bootId).arg(line));
        const QString reason = line.contains(QStringLiteral("Control session lease expired"), Qt::CaseInsensitive)
            ? QStringLiteral("LEASE_EXPIRED") : QStringLiteral("RUNTIME_FAULT");
        if (svLastStopCause_ != reason) {
            svLastStopCause_ = reason;
            emit svStopReasonChanged();
        }
        if (svStopReason_ != reason) {
            svStopReason_ = reason;
            emit svStopReasonChanged();
        }
    }

    // Board-owned latched event survives the small console log window and
    // is returned with SHOW even after the operator has restarted SV.
    static const QRegularExpression kSvLife(
        QStringLiteral("SVLIFE state=(RUNNING|STOPPED) stop_cause=([A-Z_]+) stop_seq=(\\d+)"));
    const auto svLife = kSvLife.match(line);
    if (svLife.hasMatch()) {
        bool seqOk = false;
        const auto seq = svLife.captured(3).toULongLong(&seqOk);
        if (seqOk && (seq >= lastSvStopSeq_ || !deviceVerified_)) {
            lastSvStopSeq_ = seq;
            const QString lastReason = svLife.captured(2);
            if (seq > 0U && svLastStopCause_ != lastReason) {
                svLastStopCause_ = lastReason;
                emit svStopReasonChanged();
            }
            const bool boardStopped = svLife.captured(1) == QStringLiteral("STOPPED");
            const bool stopAfterStart = !svStopSeqAtStartValid_ ||
                seq > svStopSeqAtStart_;
            if (boardStopped && !stopAfterStart && running_) {
                appendLog(QStringLiteral("SVTX"), QStringLiteral(
                    "STALE_STOP_HISTORY_IGNORED session=%1 start_seq=%2 reply_seq=%3")
                    .arg(sessionGeneration_).arg(svStopSeqAtStart_).arg(seq));
            }
            const QString effective = boardStopped && stopAfterStart &&
                lastReason != QStringLiteral("OPERATOR_STOP") &&
                lastReason != QStringLiteral("NONE") ? lastReason : QString{};
            // Unlike an unsequenced STATE line, this readback proves whether
            // STOP happened AFTER the START acknowledged by this firmware.
            if (boardStopped && stopAfterStart && running_)
                setRunning(false);
            if (svStopReason_ != effective) {
                svStopReason_ = effective;
                appendLog(QStringLiteral("SVTX"), QStringLiteral(
                    "BOARD_STOP_READBACK session=%1 boot=%2 cause=%3 seq=%4")
                    .arg(sessionGeneration_).arg(identity_.bootId).arg(lastReason).arg(seq));
                emit svStopReasonChanged();
            }
        }
    }

    auto match = kStateExpression.match(line);
    if (match.hasMatch()) {
        const bool observedRunning = match.captured(1).compare(QStringLiteral("RUNNING"), Qt::CaseInsensitive) == 0;
        // Unsolicited RUNNING telemetry may race the token-bound START ACK.
        // It is neither success nor failure evidence while the commit gate is active.
        if (scenarioStage_ == ScenarioStage::idle && running_ != observedRunning) {
            appendLog(QStringLiteral("SVTX"), QStringLiteral(
                "STATE_READBACK session=%1 boot=%2 running=%3 prior=%4 signal_generation=%5")
                .arg(sessionGeneration_).arg(identity_.bootId)
                .arg(observedRunning ? 1 : 0).arg(running_ ? 1 : 0)
                .arg(match.captured(2)));
            if (!observedRunning && running_ &&
                svStopSeqAtStartValid_ && !operatorStopPending_) {
                // STATUS is not tagged with its SHOW request generation. A
                // delayed pre-START SHOW can report STOPPED after START_ACK.
                // Wait for SVLIFE stop_seq > START_ACK stop_seq instead of
                // falsely releasing START or suppressing a working SV stream.
                appendLog(QStringLiteral("SVTX"), QStringLiteral(
                    "UNSEQUENCED_STOPPED_READBACK session=%1 start_seq=%2; awaiting SVLIFE")
                    .arg(sessionGeneration_).arg(svStopSeqAtStart_));
            } else {
                setRunning(observedRunning);
            }
        }
        signalGeneration_ = match.captured(2);
        emit telemetryChanged();
    }

    match = kTimingExpression.match(line);
    if (match.hasMatch()) {
        fps_ = match.captured(2);
        txFailures_ = match.captured(4);
        missed_ = match.captured(5);
        signalGeneration_ = match.captured(6);
        emit telemetryChanged();
    }

    match = kSignalGenerationExpression.match(line);
    if (match.hasMatch()) {
        signalGeneration_ = match.captured(1);
        emit telemetryChanged();
    }

    if (processBinaryProfileLine(line)) return;

    match = kProfileReadbackExpression.match(line);
    if (match.hasMatch() && !profileDeploying_) {
        profileGeneration_ = match.captured(1);
        emit profileStateChanged();
    }

    match = kProfileArmedExpression.match(line);
    if (match.hasMatch()) {
        profileGeneration_ = match.captured(1);
        profileDeploying_ = false;
        profileArmed_ = true;
        emit profileStateChanged();
    }

    DeviceIdentity parsedIdentity;
    if (parseIdentityLine(line, parsedIdentity)) {
        // CH343/USB serial can stay open across a soft firmware reset. A new
        // boot epoch invalidates every previously committed START, waveform
        // and PTP authority, even when the COM number remains unchanged.
        const bool sameDevice = deviceVerified_ &&
            !identity_.deviceId.isEmpty() &&
            identity_.deviceId == parsedIdentity.deviceId;
        const bool unexpectedDevice = deviceVerified_ &&
            !identity_.deviceId.isEmpty() && !sameDevice;
        if (unexpectedDevice) {
            resetScenarioStart();
            resetBinaryProfileTransfer();
            profileArmed_ = false;
            profileWasReused_ = false;
            profileDeploying_ = false;
            setRunning(false);
            emit profileStateChanged();
            setError(QStringLiteral(
                "Different ESP32-P4 identity appeared on the active serial session; "
                "closing the port instead of accepting a foreign board."));
            disconnectPort();
            return;
        }
        const bool newBoot = sameDevice && !identity_.bootId.isEmpty() &&
            identity_.bootId != parsedIdentity.bootId;
        if (newBoot) {
            resetScenarioStart();
            resetBinaryProfileTransfer();
            deferredShow_ = false;
            deferredProfileShow_ = false;
            deferredPtpShow_ = false;
            profileArmed_ = false;
            profileWasReused_ = false;
            profileDeploying_ = false;
            setRunning(false);
            resetPtpState();
            appendLog(QStringLiteral("!"), QStringLiteral(
                "Firmware reboot detected on the same COM port; old profile and START revoked."));
        }
        applyIdentity(std::move(parsedIdentity));
        if (newBoot) {
            emit profileStateChanged();
            emit ptpStateChanged();
        }
        markDeviceVerified();
    }

    match = kPtpEthernetExpression.match(line);
    if (match.hasMatch()) {
        // Only the board's ETH_EVENT stream may establish this state;
        // NEVER infer it from COM, profile selection or PTP SOURCE intent.
        const QString previousLink = ptpEthernetLink_;
        ptpEthernetDriver_ = match.captured(1).toUpper();
        ptpEthernetLink_ = match.captured(2).toUpper();
        emit ptpStateChanged();
        if (previousLink == QStringLiteral("DOWN") &&
            ptpEthernetLink_ == QStringLiteral("UP") &&
            ptpEthernetDriver_ == QStringLiteral("STARTED")) {
            emit deviceMessage(QStringLiteral(
                "Ethernet connected — PHY UP. PTP dapat di-START kembali."));
        }
    }

    match = kPtpStatusExpression.match(line);
    if (match.hasMatch()) {
        ptpAvailable_ = true;
        const bool verifyingStop = ptpStartStage_ == PtpStartStage::awaiting_stop_ack ||
            ptpStartStage_ == PtpStartStage::awaiting_stop_readback;
        // A pending STOP is not STOPPED. Ignore unsolicited/stale SHOW state
        // while waiting for the fresh post-STOP verification request. The
        // stop state machine below is the only writer of verified STOPPED.
        if (!verifyingStop) {
            ptpRunning_ = match.captured(1).compare(
                QStringLiteral("RUNNING"), Qt::CaseInsensitive) == 0;
            ptpStatus_ = ptpRunning_ ? QStringLiteral("Lab TX active") :
                QStringLiteral("Lab TX stopped");
        }
        ptpAnnounceSent_ = match.captured(2);
        ptpSyncSent_ = match.captured(3);
        ptpFollowUpSent_ = match.captured(4);
        ptpPdelayFrames_ = match.captured(5);
        ptpTxFailures_ = match.captured(6);
        emit ptpStateChanged();
    }

    match = kPtpConfigExpression.match(line);
    if (match.hasMatch()) {
        ptpAvailable_ = true;
        ptpDomain_ = match.captured(1);
        ptpTransportSpecific_ = QStringLiteral("0x%1").arg(match.captured(2).toUpper());
        ptpVlan_ = match.captured(3);
        emit ptpStateChanged();
    }

    match = kPtpRoleExpression.match(line);
    if (match.hasMatch()) {
        ptpAvailable_ = true;
        ptpRole_ = match.captured(1).toUpper();
        // PROFILE SHOW/role ACK can be an earlier queued readback. It must
        // never overwrite verified PTP STATUS running state nor clear the
        // independent PTP2 discipline/clock observations. Startup still
        // validates this role readback in advancePtpStartup().
        emit ptpStateChanged();
    }

    match = kPtpMeasuredStateExpression.match(line);
    if (match.hasMatch()) {
        ptpAvailable_ = true;
        ptpRole_ = match.captured(1).toUpper();
        ptpDiscipline_ = match.captured(2).toUpper();
        ptpSource_ = match.captured(3);
        ptpOffsetNs_ = match.captured(4);
        ptpPathDelayNs_ = match.captured(5);
        ptpJitterNs_ = match.captured(6);
        ptpFrequencyPpb_ = match.captured(7);
        ptpGlobalTraceable_ = match.captured(8) == QStringLiteral("1");
        ptpMeasuredSmpSynch_ = match.captured(9).toUpper();
        ptpRxAnnounce_ = match.captured(10);
        ptpRxSync_ = match.captured(11);
        ptpRxFollowUp_ = match.captured(12);
        ptpRxPdelay_ = match.captured(13);
        ptpPdelayRequests_ = match.captured(14);
        ptpAccepted_ = match.captured(15);
        ptpRejected_ = match.captured(16);
        emit ptpStateChanged();
    }

    match = kSmpSynchExpression.match(line);
    if (match.hasMatch()) {
        smpSynchMode_ = match.captured(1).toUpper();
        smpSynchValue_ = match.captured(2);
        smpSynchSource_ = match.captured(3).toUpper();
        smpSynchSimulated_ = match.captured(4) == QStringLiteral("1");
        smpSynchMeasured_ = match.captured(5) == QStringLiteral("1");
        emit ptpStateChanged();
    }

    if (line.contains(QStringLiteral("PTP unavailable"), Qt::CaseInsensitive)) {
        resetPtpState();
        ptpStatus_ = QStringLiteral("Not enabled in firmware");
        emit ptpStateChanged();
    }
    if (line.contains(QStringLiteral("PTP configuration accepted"), Qt::CaseInsensitive) && !ptpStarting()) {
        emit deviceMessage(QStringLiteral("PTP expert profile accepted."));
        static_cast<void>(sendPtpShow());
    }
    if (line.contains(QStringLiteral("PTP start accepted"), Qt::CaseInsensitive) && !ptpStarting()) {
        ptpStatus_ = QStringLiteral("PTP start acknowledged; requesting live status");
        static_cast<void>(sendPtpShow());
    }
    if (line.contains(QStringLiteral("PTP start rejected"), Qt::CaseInsensitive) || line.contains(QStringLiteral("PTP source readiness timeout"), Qt::CaseInsensitive)) {
        ptpLastStartFailure_ = line;
        ptpRunning_ = false;
        ptpStatus_ = QStringLiteral("PTP start failed: %1").arg(line);
        emit ptpStateChanged();
        // Preserve the exact classified first-hand board failure rather
        // than replacing it with an unhelpful generic "retry" toast.
        const QString hint = line.contains(QStringLiteral("ETHERNET_TX_INVALID_STATE"),
                                            Qt::CaseInsensitive)
            ? QStringLiteral("Ethernet MAC TX rejected by driver state; inspect PTP ETH driver/link.")
            : line.contains(QStringLiteral("ETHERNET_LINK_DOWN"), Qt::CaseInsensitive)
            ? QStringLiteral("Physical Ethernet link is DOWN; verify cable and switch LINK.")
            : QStringLiteral("PTP SOURCE did not establish real Announce + Sync + Follow_Up TX.");
        setError(hint);
    }

    advancePtpStartup(line);

    if (profileDeploying_ &&
        line.contains(QStringLiteral("PROFILE"), Qt::CaseInsensitive) &&
        line.contains(QStringLiteral("rejected"), Qt::CaseInsensitive)) {
        failBinaryProfileDeployment(QStringLiteral("Device rejected the profile."));
    }
}

void DeviceController::resetTelemetry() {
    operatorStopPending_ = false;
    lastSvStopSeq_ = 0;
    svStopSeqAtStart_ = 0;
    svStopSeqAtStartValid_ = false;
    svLastStopCause_.clear();
    if (!svStopReason_.isEmpty()) {
        svStopReason_.clear();
        emit svStopReasonChanged();
    }
    fps_ = QStringLiteral("—");
    missed_ = QStringLiteral("—");
    txFailures_ = QStringLiteral("—");
    signalGeneration_ = QStringLiteral("—");
    emit telemetryChanged();
}

void DeviceController::resetPtpState() {
    resetPtpStartup();
    ptpLastStartFailure_.clear();
    syncDiagnostics_ = QStringLiteral(
        "No current-board SV lock evidence. Select Inspect SV lock after verification.");
    syncFlightLatest_.clear();
    emit syncDiagnosticsChanged();
    ptpAvailable_ = false;
    ptpRunning_ = false;
    ptpEthernetDriver_ = QStringLiteral("UNKNOWN");
    ptpEthernetLink_ = QStringLiteral("UNKNOWN");
    ptpStatus_ = QStringLiteral("Waiting for device");
    ptpDomain_ = QStringLiteral("-");
    ptpTransportSpecific_ = QStringLiteral("-");
    ptpVlan_ = QStringLiteral("-");
    ptpAnnounceSent_ = QStringLiteral("-");
    ptpSyncSent_ = QStringLiteral("-");
    ptpFollowUpSent_ = QStringLiteral("-");
    ptpPdelayFrames_ = QStringLiteral("-");
    ptpTxFailures_ = QStringLiteral("-");
    ptpRole_ = QStringLiteral("SOURCE");
    ptpDiscipline_ = QStringLiteral("UNLOCKED");
    ptpSource_ = QStringLiteral("NONE");
    ptpOffsetNs_ = QStringLiteral("NA");
    ptpPathDelayNs_ = QStringLiteral("NA");
    ptpJitterNs_ = QStringLiteral("NA");
    ptpFrequencyPpb_ = QStringLiteral("0");
    ptpGlobalTraceable_ = false;
    ptpMeasuredSmpSynch_ = QStringLiteral("NA");
    ptpRxAnnounce_ = QStringLiteral("0");
    ptpRxSync_ = QStringLiteral("0");
    ptpRxFollowUp_ = QStringLiteral("0");
    ptpRxPdelay_ = QStringLiteral("0");
    ptpPdelayRequests_ = QStringLiteral("0");
    ptpAccepted_ = QStringLiteral("0");
    ptpRejected_ = QStringLiteral("0");
    smpSynchMode_ = QStringLiteral("AUTO");
    smpSynchValue_ = QStringLiteral("0");
    smpSynchSource_ = QStringLiteral("SAFE_DEFAULT");
    smpSynchSimulated_ = false;
    smpSynchMeasured_ = false;
}

QString DeviceController::cleanLine(const QString& rawLine) {
    QString line = rawLine;
    line.remove(kAnsiExpression);
    return line.trimmed();
}

QString DeviceController::utf8Hex(const QString& text) {
    return QString::fromLatin1(text.toUtf8().toHex());
}

QString DeviceController::compactMac(const QString& text) {
    QString out;
    out.reserve(12);
    for (const auto ch : text) {
        if (ch.isDigit() || (ch.toUpper() >= QLatin1Char('A') && ch.toUpper() <= QLatin1Char('F'))) {
            out.append(ch.toUpper());
        }
    }
    return out;
}
