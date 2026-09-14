// SPDX-License-Identifier: GPL-3.0-or-later

#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"
#include "SingleInstanceGuard.hpp"
#include "SmartSessionController.hpp"
#include "StudioDeviceController.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>

#include <algorithm>
#include <functional>
#include <limits>

#ifndef ARSTACK_BOARD_CANDIDATE_SHA
#define ARSTACK_BOARD_CANDIDATE_SHA "UNKNOWN"
#endif

namespace {
constexpr int kReadyTimeoutMs = 30000;
constexpr int kStateTimeoutMs = 6000;
constexpr int kLiveCommitTimeoutMs = 4000;
constexpr int kProfileTransitionTimeoutMs = 1500;
constexpr double kCurrentCountsPerAmp = 1000.0;
constexpr double kVoltageCountsPerVolt = 100.0;

bool waitUntil(const std::function<bool()>& predicate, const int timeoutMs) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (predicate()) return true;
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

qulonglong numeric(const QString& text, bool* okOut = nullptr) {
    bool ok = false;
    const qulonglong value = text.trimmed().toULongLong(&ok);
    if (okOut != nullptr) *okOut = ok;
    return ok ? value : 0ULL;
}

bool writeJson(const QString& path, const QJsonObject& root) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.flush();
}

QJsonObject falseCaptureEvidence() {
    return QJsonObject{
        {QStringLiteral("durationSeconds"), 0},
        {QStringLiteral("destinationMacMatches"), false},
        {QStringLiteral("appidMatches"), false},
        {QStringLiteral("vlanPcpMatches"), false},
        {QStringLiteral("svIdMatches"), false},
        {QStringLiteral("confRevMatches"), false},
        {QStringLiteral("asduCountMatches"), false},
        {QStringLiteral("payloadBytesMatches"), false},
        {QStringLiteral("smpCntContinuous"), false},
        {QStringLiteral("counterWrapObserved"), false},
        {QStringLiteral("smpSynchAdvertised"), 0},
    };
}

class BoardAcceptance final {
public:
    BoardAcceptance(StudioDeviceController& device,
                    SmartSessionController& session,
                    SclProfileModel& profiles,
                    const int soakSeconds)
        : device_(device), session_(session), profiles_(profiles), soakSeconds_(soakSeconds) {
        QObject::connect(&device_, &StudioDeviceController::controlHealthFailed, &device_, [this] {
            ++healthReconnects_;
        });
        QObject::connect(&device_, &DeviceController::runningChanged, &device_, [this] {
            if (!device_.running()) return;
            if (expectedStartTransition_) {
                expectedStartTransition_ = false;
            } else {
                ++automaticStarts_;
            }
        });
        QObject::connect(&device_, &DeviceController::telemetryChanged, &device_, [this] {
            if (!collectTelemetry_) return;
            bool fpsOk = false;
            const int fps = device_.fps().trimmed().toInt(&fpsOk);
            if (fpsOk) {
                if (fpsMin_ < 0 || fps < fpsMin_) fpsMin_ = fps;
                if (fps > fpsMax_) fpsMax_ = fps;
                ++telemetryWindows_;
            }
            bool missedOk = false;
            const qulonglong missed = numeric(device_.missed(), &missedOk);
            if (missedOk) missedMax_ = std::max(missedMax_, missed);
            bool txOk = false;
            const qulonglong tx = numeric(device_.txFailures(), &txOk);
            if (txOk) txFailuresMax_ = std::max(txFailuresMax_, tx);
        });
    }

    int run(const QString& outputPath) {
        if (!profiles_.loadReferenceTemplate()) return fail(outputPath, QStringLiteral("Built-in 4I+4V reference profile could not be loaded."));
        const QVariantMap profile = profiles_.selectedProfile();
        if (profile.value(QStringLiteral("compatibilityClass")).toString() != QStringLiteral("A") ||
            profile.value(QStringLiteral("deviceSupport")).toString() != QStringLiteral("ready") ||
            profile.value(QStringLiteral("publisherRate")).toULongLong() != 4000ULL ||
            profile.value(QStringLiteral("payloadBytes")).toULongLong() != 64ULL ||
            profile.value(QStringLiteral("channelLeafCount")).toULongLong() != 16ULL) {
            return fail(outputPath, QStringLiteral("Reference profile does not match frozen P0 contract."));
        }

        session_.start();
        qInfo().noquote() << "BOARD RC: discovering exact frozen candidate device...";
        if (!waitReady(kReadyTimeoutMs)) {
            return fail(outputPath, QStringLiteral("Device did not reach bounded READY state: %1 · %2")
                .arg(session_.state(), session_.statusText()));
        }
        if (session_.firmwareInstallRequired() || session_.firmwareUpdateRequired()) {
            return fail(outputPath, QStringLiteral("Unexpected firmware prompt/update requirement on current-firmware board."));
        }

        deviceId_ = device_.deviceId();
        bootId_ = device_.bootId();
        port_ = device_.portName();
        if (deviceId_.isEmpty() || bootId_.size() != 16) {
            return fail(outputPath, QStringLiteral("Verified device identity is incomplete."));
        }
        deployReady_ = true;

        qInfo().noquote() << "BOARD RC: READY on" << port_ << "device_id=" << deviceId_;

        if (!runStartStopCycles(10)) {
            return fail(outputPath, QStringLiteral("Start/Stop cycle failed before 10 clean iterations."));
        }

        zero_ = session_.requestZero();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        if (!zero_) return fail(outputPath, QStringLiteral("ZERO intent was rejected."));

        if (!startAndWait()) return fail(outputPath, QStringLiteral("Could not enter RUNNING for live-edit acceptance."));
        if (!exerciseLiveEdits()) return fail(outputPath, QStringLiteral("One or more live-edit operations failed or did not advance signal generation."));
        if (!stopAndWait()) return fail(outputPath, QStringLiteral("Could not STOP after live-edit acceptance."));

        profileResync_ = resyncProfileAndWait();
        if (!profileResync_) return fail(outputPath, QStringLiteral("Profile re-sync did not prove a new generation and return to READY."));

        if (!session_.requestSmpSynch(QStringLiteral("0"))) {
            return fail(outputPath, QStringLiteral("Could not force conservative smpSynch=0 policy before soak."));
        }
        static_cast<void>(session_.requestPtpRefresh());
        if (!waitUntil([this] { return device_.smpSynchValue() == QStringLiteral("0"); }, kStateTimeoutMs)) {
            return fail(outputPath, QStringLiteral("Device did not report smpSynch=0 before soak."));
        }

        if (!startAndWait()) return fail(outputPath, QStringLiteral("Could not enter RUNNING for retained soak."));
        resetSoakStats();
        collectTelemetry_ = true;
        qInfo().noquote() << "BOARD RC: retained soak started for" << soakSeconds_ << "seconds.";

        QElapsedTimer soak;
        soak.start();
        int lastReportedSecond = -1;
        while (soak.elapsed() < soakSeconds_ * 1000LL) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
            if (!device_.running() || !device_.deviceVerified() || session_.state() != QStringLiteral("RUNNING")) {
                collectTelemetry_ = false;
                return fail(outputPath, QStringLiteral("RUNNING/verified state was lost during retained soak."));
            }
            const int elapsedSeconds = static_cast<int>(soak.elapsed() / 1000LL);
            if (elapsedSeconds != lastReportedSecond && elapsedSeconds % 60 == 0) {
                lastReportedSecond = elapsedSeconds;
                qInfo().noquote() << "BOARD RC: soak" << elapsedSeconds << "/" << soakSeconds_
                                  << "fps=" << device_.fps()
                                  << "missed=" << device_.missed()
                                  << "txFail=" << device_.txFailures();
            }
            QThread::msleep(20);
        }
        collectTelemetry_ = false;
        soakDurationSeconds_ = static_cast<int>(soak.elapsed() / 1000LL);

        if (!stopAndWait()) return fail(outputPath, QStringLiteral("Final STOP failed after retained soak."));

        const bool telemetryPass = soakDurationSeconds_ >= soakSeconds_ && telemetryWindows_ > 0 &&
            fpsMin_ >= 3999 && fpsMax_ <= 4001 && missedMax_ == 0 && txFailuresMax_ == 0 &&
            healthReconnects_ == 0 && automaticStarts_ == 0;
        if (!telemetryPass) {
            return fail(outputPath, QStringLiteral("Retained-run telemetry violated the RC2 envelope."));
        }

        QJsonObject evidence = makeEvidence();
        evidence.insert(QStringLiteral("boardRunner"), QJsonObject{
            {QStringLiteral("automatedBoardPathPassed"), soakSeconds_ >= 3600},
            {QStringLiteral("diagnosticOnly"), soakSeconds_ < 3600},
            {QStringLiteral("deviceId"), deviceId_},
            {QStringLiteral("bootId"), bootId_},
            {QStringLiteral("port"), port_},
            {QStringLiteral("telemetryWindows"), telemetryWindows_},
            {QStringLiteral("physicalCaptureStillRequired"), true},
            {QStringLiteral("studioUiObservationStillRequired"), true},
        });

        if (!writeJson(outputPath, evidence)) {
            qCritical().noquote() << "BOARD RC: could not write evidence JSON" << outputPath;
            return 9;
        }

        if (soakSeconds_ < 3600) {
            qWarning().noquote()
                << "BOARD RC diagnostic path: COMPLETE · evidence written to" << outputPath
                << "· retained soak is below 3600 seconds, so this run is intentionally NOT RC2 acceptance.";
            return 10;
        }

        qInfo().noquote()
            << "BOARD RC automated path: PASS · evidence written to" << outputPath
            << "· independent packet capture + Studio UI/orphan observation still required before RC2 final PASS.";
        return 0;
    }

private:
    bool waitReady(const int timeoutMs) {
        return waitUntil([this] {
            return session_.state() == QStringLiteral("READY") && session_.startReady() &&
                device_.deviceVerified() && device_.controlResponsive() && !device_.running();
        }, timeoutMs);
    }

    bool startAndWait() {
        expectedStartTransition_ = true;
        if (!session_.requestStart()) {
            expectedStartTransition_ = false;
            return false;
        }
        const bool running = waitUntil([this] { return device_.running(); }, kStateTimeoutMs);
        if (!running) expectedStartTransition_ = false;
        return running;
    }

    bool stopAndWait() {
        if (!session_.requestStop()) return false;
        if (!waitUntil([this] { return !device_.running(); }, kStateTimeoutMs)) return false;
        return waitReady(kReadyTimeoutMs);
    }

    bool resyncProfileAndWait() {
        const QString baseline = device_.profileGeneration().trimmed();
        if (!session_.requestProfileSync()) return false;

        const bool transactionObserved = waitUntil([this, baseline] {
            return device_.profileDeploying() ||
                session_.state() != QStringLiteral("READY") ||
                device_.profileGeneration().trimmed() != baseline;
        }, kProfileTransitionTimeoutMs);
        if (!transactionObserved) return false;
        if (!waitReady(kReadyTimeoutMs)) return false;

        return SmartSessionController::profileGenerationAdvanced(
            baseline, device_.profileGeneration().trimmed());
    }

    bool runStartStopCycles(const int count) {
        for (int i = 0; i < count; ++i) {
            if (!startAndWait()) return false;
            QThread::msleep(150);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (!stopAndWait()) return false;
            ++startStopCycles_;
            qInfo().noquote() << "BOARD RC: Start/Stop" << startStopCycles_ << "/" << count << "PASS";
        }
        return true;
    }

    bool waitSignalGenerationAdvance(const qulonglong baseline) {
        return waitUntil([this, baseline] {
            bool ok = false;
            const qulonglong observed = numeric(device_.signalGeneration(), &ok);
            return ok && observed != baseline;
        }, kLiveCommitTimeoutMs);
    }

    qulonglong currentSignalGeneration(bool* okOut = nullptr) const {
        return numeric(device_.signalGeneration(), okOut);
    }

    bool liveStep(const std::function<bool()>& submit, bool& flag) {
        bool baselineOk = false;
        const qulonglong baseline = currentSignalGeneration(&baselineOk);
        if (!submit()) return false;
        const bool advanced = baselineOk ? waitSignalGenerationAdvance(baseline)
                                         : waitUntil([this] {
                                               bool ok = false;
                                               static_cast<void>(currentSignalGeneration(&ok));
                                               return ok;
                                           }, kLiveCommitTimeoutMs);
        flag = advanced;
        signalGenerationAdvanced_ = signalGenerationAdvanced_ || advanced;
        return advanced;
    }

    bool exerciseLiveEdits() {
        if (!liveStep([this] {
                return session_.requestSetSignal(QStringLiteral("IA"), 1.0, 0.0, 0U,
                    kCurrentCountsPerAmp, kVoltageCountsPerVolt);
            }, liveMagnitude_)) return false;

        if (!liveStep([this] {
                return session_.requestSetSignal(QStringLiteral("IA"), 1.0, 15.0, 0U,
                    kCurrentCountsPerAmp, kVoltageCountsPerVolt);
            }, livePhase_)) return false;

        if (!liveStep([this] { return session_.requestSetFrequency(49.5); }, liveFrequency_)) return false;

        if (!liveStep([this] { return session_.requestSetQuality(QStringLiteral("IA"), 1U); }, liveQuality_)) return false;
        static_cast<void>(session_.requestSetQuality(QStringLiteral("IA"), 0U));

        if (!liveStep([this] {
                return session_.requestSetCtSaturation(true, 10.0, 5.0, 2, 90.0);
            }, ctSaturation_)) return false;
        static_cast<void>(session_.requestSetCtSaturation(false, 0.0, 0.0, 2, 100.0));
        static_cast<void>(session_.requestSetFrequency(50.0));
        return true;
    }

    void resetSoakStats() {
        fpsMin_ = -1;
        fpsMax_ = -1;
        missedMax_ = 0;
        txFailuresMax_ = 0;
        telemetryWindows_ = 0;
        soakDurationSeconds_ = 0;
    }

    QJsonObject makeEvidence() const {
        const QVariantMap profile = profiles_.selectedProfile();
        return QJsonObject{
            {QStringLiteral("schema"), QStringLiteral("arstack.studio.rc2.v1")},
            {QStringLiteral("candidateSha"), QStringLiteral(ARSTACK_BOARD_CANDIDATE_SHA)},
            {QStringLiteral("profile"), QJsonObject{
                {QStringLiteral("channels"), 8},
                {QStringLiteral("publisherRate"), static_cast<int>(profile.value(QStringLiteral("publisherRate")).toULongLong())},
                {QStringLiteral("asduCount"), static_cast<int>(profile.value(QStringLiteral("nofASDU")).toUInt())},
                {QStringLiteral("payloadBytes"), static_cast<int>(profile.value(QStringLiteral("payloadBytes")).toULongLong())},
                {QStringLiteral("channelLeafCount"), static_cast<int>(profile.value(QStringLiteral("channelLeafCount")).toULongLong())},
            }},
            {QStringLiteral("telemetry"), QJsonObject{
                {QStringLiteral("durationSeconds"), soakDurationSeconds_},
                {QStringLiteral("fpsMin"), fpsMin_},
                {QStringLiteral("fpsMax"), fpsMax_},
                {QStringLiteral("missed"), static_cast<qint64>(missedMax_)},
                {QStringLiteral("txFailures"), static_cast<qint64>(txFailuresMax_)},
                {QStringLiteral("healthReconnects"), healthReconnects_},
                {QStringLiteral("automaticStarts"), automaticStarts_},
            }},
            {QStringLiteral("operations"), QJsonObject{
                {QStringLiteral("deployReady"), deployReady_},
                {QStringLiteral("startStopCycles"), startStopCycles_},
                {QStringLiteral("zero"), zero_},
                {QStringLiteral("liveMagnitude"), liveMagnitude_},
                {QStringLiteral("livePhase"), livePhase_},
                {QStringLiteral("liveFrequency"), liveFrequency_},
                {QStringLiteral("liveQuality"), liveQuality_},
                {QStringLiteral("ctSaturation"), ctSaturation_},
                {QStringLiteral("profileResync"), profileResync_},
                {QStringLiteral("signalGenerationAdvanced"), signalGenerationAdvanced_},
                // Fail closed: only independent packet evidence may change this to false.
                {QStringLiteral("smpCntRestartOnLiveEdit"), true},
            }},
            {QStringLiteral("capture"), falseCaptureEvidence()},
            {QStringLiteral("operator"), QJsonObject{
                // Headless runner cannot prove GUI responsiveness or Studio process cleanup.
                {QStringLiteral("uiResponsive"), false},
                {QStringLiteral("orphanWorkerObserved"), true},
            }},
        };
    }

    int fail(const QString& outputPath, const QString& reason) {
        qCritical().noquote() << "BOARD RC: FAIL ·" << reason;
        QJsonObject evidence = makeEvidence();
        evidence.insert(QStringLiteral("boardRunner"), QJsonObject{
            {QStringLiteral("automatedBoardPathPassed"), false},
            {QStringLiteral("failure"), reason},
            {QStringLiteral("deviceId"), deviceId_},
            {QStringLiteral("bootId"), bootId_},
            {QStringLiteral("port"), port_},
        });
        static_cast<void>(writeJson(outputPath, evidence));
        return 8;
    }

    StudioDeviceController& device_;
    SmartSessionController& session_;
    SclProfileModel& profiles_;
    int soakSeconds_{3600};
    QString deviceId_;
    QString bootId_;
    QString port_;
    bool expectedStartTransition_{false};
    bool collectTelemetry_{false};
    bool deployReady_{false};
    int startStopCycles_{0};
    bool zero_{false};
    bool liveMagnitude_{false};
    bool livePhase_{false};
    bool liveFrequency_{false};
    bool liveQuality_{false};
    bool ctSaturation_{false};
    bool profileResync_{false};
    bool signalGenerationAdvanced_{false};
    int healthReconnects_{0};
    int automaticStarts_{0};
    int fpsMin_{-1};
    int fpsMax_{-1};
    qulonglong missedMax_{0};
    qulonglong txFailuresMax_{0};
    int telemetryWindows_{0};
    int soakDurationSeconds_{0};
};

int contractCheck() {
    const QString sha = QStringLiteral(ARSTACK_BOARD_CANDIDATE_SHA);
    static const QRegularExpression fullSha{QStringLiteral("^[0-9a-f]{40}$")};
    if (!fullSha.match(sha).hasMatch()) {
        qCritical().noquote() << "Board acceptance contract: invalid frozen candidate SHA" << sha;
        return 3;
    }
    qInfo().noquote() << "Board acceptance contract: PASS · frozen candidate" << sha;
    return 0;
}
} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ARStack61850"));
    QCoreApplication::setApplicationName(QStringLiteral("ARStack Board Acceptance"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Physical ESP32-P4 acceptance runner for frozen ARStack Studio P0 RC."));
    parser.addHelpOption();
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Write machine-readable RC2 evidence JSON."), QStringLiteral("file"),
        QStringLiteral("rc2-board-evidence.json"));
    QCommandLineOption soakOption(QStringLiteral("soak-seconds"),
        QStringLiteral("Retained RUNNING duration. Values below 3600 are diagnostic-only and cannot complete RC2."),
        QStringLiteral("seconds"), QStringLiteral("3600"));
    QCommandLineOption contractOption(QStringLiteral("check-contract"),
        QStringLiteral("Validate the tool is pinned to a 40-character frozen candidate SHA without touching hardware."));
    parser.addOption(outputOption);
    parser.addOption(soakOption);
    parser.addOption(contractOption);
    parser.process(app);

    if (parser.isSet(contractOption)) return contractCheck();

    bool soakOk = false;
    const int soakSeconds = parser.value(soakOption).toInt(&soakOk);
    if (!soakOk || soakSeconds < 1) {
        qCritical().noquote() << "--soak-seconds must be a positive integer.";
        return 2;
    }
    if (soakSeconds < 3600) {
        qWarning().noquote() << "Diagnostic board run only:" << soakSeconds
                             << "seconds is below the 3600-second RC2 acceptance minimum.";
    }

    SingleInstanceGuard instanceGuard;
    if (!instanceGuard.tryAcquire()) {
        qCritical().noquote() << "ARStack Studio or another board acceptance process is already running.";
        return SingleInstanceGuard::contentionExitCode();
    }

    SclProfileModel profiles;
    StudioDeviceController device;
    FirmwareManager firmware;
    SmartSessionController session;
    session.setDevice(&device);
    session.setProfiles(&profiles);
    session.setFirmware(&firmware);

    BoardAcceptance runner(device, session, profiles, soakSeconds);
    const int result = runner.run(parser.value(outputOption));
    firmware.shutdown();
    return result;
}
