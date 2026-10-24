// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceIoWorker.hpp"
#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"
#include "SingleInstanceGuard.hpp"
#include "SmartSessionController.hpp"
#include "StudioClipboard.hpp"
#include "StudioDeviceController.hpp"
#include "SvCaptureController.hpp"
#include "ariec61850/sampled_values/compiled_device_profile.hpp"

#include <QClipboard>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRectF>
#include <QSize>
#include <QThread>
#include <QTimer>
#include <QtQml/qqml.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <span>
#include <string_view>

#ifndef ARSTACK_STUDIO_VERSION
#define ARSTACK_STUDIO_VERSION "0.1.1"
#endif

namespace {
bool hasArgument(const int argc, char* argv[], const std::string_view wanted) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string_view{argv[i]} == wanted) return true;
    }
    return false;
}


struct UiBounds {
    QRectF area;
    bool visible{false};
};

[[nodiscard]] UiBounds findUiBounds(QQuickWindow* window, const char* name) {
    auto* item = window->findChild<QQuickItem*>(QString::fromLatin1(name));
    if (item == nullptr || item->width() < 1 || item->height() < 1) return {};
    return {QRectF{item->mapToScene(QPointF{0, 0}), QSizeF{item->width(), item->height()}},
            item->isVisible()};
}

[[nodiscard]] bool checkOperatorViewport(QQuickWindow* window, const QSize size) {
    constexpr qreal tolerance = 2.0;
    const QRectF viewport{QPointF{0, 0}, QSizeF{size}};
    const auto bar = findUiBounds(window, "studioWorkflowBar");
    const auto surface = findUiBounds(window, "studioInjectionSurface");
    const auto voltage = findUiBounds(window, "studioVoltageMatrix");
    const auto current = findUiBounds(window, "studioCurrentMatrix");
    const auto tray = findUiBounds(window, "studioTimingTray");
    const auto quality = findUiBounds(window, "studioQualityRow");
    const auto phasor = findUiBounds(window, "studioPhasorDock");
    const auto waveform = findUiBounds(window, "studioWaveformDock");
    const std::array<std::pair<const char*, UiBounds>, 6> areas{{
        {"toolbar", bar}, {"injection workspace", surface},
        {"voltage matrix", voltage}, {"current matrix", current},
        {"PTP timing tray", tray}, {"quality editor", quality}
    }};
    for (const auto& [label, item] : areas) {
        if (!item.visible || item.area.width() < 25 || item.area.height() < 15 ||
            !viewport.adjusted(-tolerance, -tolerance, tolerance, tolerance)
                 .contains(item.area.adjusted(tolerance, tolerance,
                                              -tolerance, -tolerance))) {
            qCritical().noquote() << "Studio UI viewport violation" << size << label
                                  << "bounds=" << item.area << "viewport=" << viewport;
            return false;
        }
    }
    // Real Windows evidence must contain both actual plots. Checking only
    // the left table let a 100%-empty right workspace pass every DPI gate.
    // Report visible state + bounds to distinguish hidden from zero-size docks.
    if (surface.area.width() > 470 ||
        !phasor.visible || !waveform.visible ||
        phasor.area.width() < 180 || waveform.area.width() < 180 ||
        phasor.area.height() < 170 || waveform.area.height() < 170 ||
        std::abs(phasor.area.height() - waveform.area.height()) > 24) {
        qCritical().noquote() << "Studio split width/chart balance violation" << size
                              << "surface=" << surface.area
                              << "phasor-visible=" << phasor.visible << "phasor=" << phasor.area
                              << "wave-visible=" << waveform.visible << "wave=" << waveform.area;
        return false;
    }
    // A child can pass the viewport test while overflowing its *parent*
    // (small controls were clipped by the neighbouring plot at 1366px).
    for (const auto& [label, item] : std::array<std::pair<const char*, UiBounds>, 4>{{
             {"voltage", voltage}, {"current", current},
             {"timing", tray}, {"quality", quality}
         }}) {
        if (!surface.area.adjusted(-tolerance, -tolerance, tolerance, tolerance)
                 .contains(item.area.adjusted(tolerance, tolerance, -tolerance, -tolerance))) {
            qCritical().noquote() << "Studio injection child overflows parent" << size
                                  << label << item.area << surface.area;
            return false;
        }
    }
    // Operator layout: voltage above current. Validate the new actual
    // vertical hierarchy, not the obsolete side-by-side geometry.
    if (!(voltage.area.bottom() <= current.area.top() + tolerance
          && tray.area.top() >= current.area.bottom() - tolerance
          && quality.area.top() >= tray.area.bottom() - tolerance
          && quality.area.bottom() <= surface.area.bottom() + tolerance)) {
        qCritical().noquote() << "Studio UI panel order/overlap violation" << size
                              << voltage.area << current.area << tray.area << quality.area;
        return false;
    }
    const std::array<const char*, 4> controls{{
        "studioPtpSwitch", "studioPtpRole", "studioSmpSynchMode", "studioTimingDetails"
    }};
    qreal previousRight = tray.area.left();
    for (const char* name : controls) {
        const auto control = findUiBounds(window, name);
        const qreal minimumWidth = std::string_view{name} == "studioTimingDetails" ? 30.0 : 35.0;
        if (!control.visible || control.area.width() < minimumWidth ||
            control.area.left() + tolerance < previousRight ||
            !tray.area.adjusted(-tolerance, -tolerance, tolerance, tolerance)
                 .contains(control.area.adjusted(tolerance, tolerance,
                                                 -tolerance, -tolerance))) {
            qCritical().noquote() << "Studio timing controls clipped or overlapped"
                                  << size << name << control.area << tray.area;
            return false;
        }
        previousRight = control.area.right();
    }
    qInfo().noquote() << "Studio viewport geometry: PASS" << size
                      << "toolbar=" << bar.area << "PTP=" << tray.area;
    return true;
}

class PrimaryWindowCloseFilter final : public QObject {
public:
    explicit PrimaryWindowCloseFilter(QCoreApplication* app)
        : QObject(app), app_(app) {}

    [[nodiscard]] bool closeObserved() const noexcept { return closeObserved_; }

protected:
    [[nodiscard]] bool eventFilter(QObject* watched, QEvent* event) override {
        if (event != nullptr && event->type() == QEvent::Close && app_ != nullptr) {
            closeObserved_ = true;
            // QCoreApplication::quit() can be ignored by application/window
            // policy. A primary-window close is an explicit process-exit request,
            // so terminate the main event loop deterministically. aboutToQuit()
            // remains the single cleanup boundary for serial/firmware workers.
            QCoreApplication::exit(0);
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QCoreApplication* app_{nullptr};
    bool closeObserved_{false};
};

int checkReferenceTemplate(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    SclProfileModel profiles;
    if (!profiles.loadReferenceTemplate()) {
        qCritical().noquote() << profiles.fatalError();
        return 2;
    }

    const auto profile = profiles.selectedProfile();
    const bool valid =
        profiles.referenceTemplateActive() &&
        profiles.rowCount() == 1 &&
        profile.value(QStringLiteral("compatibilityClass")).toString() == QStringLiteral("A") &&
        profile.value(QStringLiteral("deviceSupport")).toString() == QStringLiteral("ready") &&
        profile.value(QStringLiteral("svId")).toString() == QStringLiteral("ARSTACK_SV01") &&
        profile.value(QStringLiteral("publisherRate")).toULongLong() == 4000ULL &&
        profile.value(QStringLiteral("counterModulus")).toUInt() == 4000U &&
        profile.value(QStringLiteral("payloadBytes")).toULongLong() == 64ULL &&
        profile.value(QStringLiteral("channelLeafCount")).toULongLong() == 16ULL &&
        profile.value(QStringLiteral("deviceProfileSchema")).toInt() == 1 &&
        !profile.value(QStringLiteral("deviceProfileBinary")).toByteArray().isEmpty();

    if (!valid) {
        qCritical().noquote() << "4I+4V reference template regression:" << profile;
        return 3;
    }

    // An operator VLAN edit MUST change the binary sent to the ESP32, not
    // only a QML label. It must survive an SCL timing recompile and reject
    // invalid values without corrupting the active stream profile.
    if (!profiles.configureSelectedVlan(true, 123, 5) ||
        profiles.configureSelectedVlan(true, 4095, 5) ||
        profiles.configureSelectedVlan(true, 123, 8) ||
        !profiles.confirmCounterModulus(4000)) {
        qCritical().noquote() << "SV VLAN operator edit/recompile regression";
        return 4;
    }
    const auto edited = profiles.selectedProfile();
    const auto blob = edited.value(QStringLiteral("deviceProfileBinary")).toByteArray();
    ar::iec61850::sampled_values::CompiledSvDeviceProfile decoded{};
    const auto bytes = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(blob.constData()),
        static_cast<std::size_t>(blob.size())};
    if (blob.isEmpty() ||
        !ar::iec61850::sampled_values::SvDeviceProfileBinaryCodec::decode(bytes, decoded).success() ||
        !decoded.vlan_present || decoded.vlan_id != 123U || decoded.vlan_priority != 5U ||
        edited.value(QStringLiteral("vlanId")).toInt() != 123 ||
        edited.value(QStringLiteral("vlanPriority")).toInt() != 5) {
        qCritical().noquote() << "SV VLAN binary profile mismatch" << edited;
        return 5;
    }
    if (!profiles.loadReferenceTemplate() ||
        profiles.selectedProfile().value(QStringLiteral("vlanId")).toInt() != 0 ||
        profiles.selectedProfile().value(QStringLiteral("vlanPriority")).toInt() != 4) {
        qCritical().noquote() << "SV VLAN override leaked across reference reload";
        return 6;
    }
    qInfo().noquote()
        << "ARStack 4I+4V 9-2LE reference: PASS · 4000 fps · SV VLAN edit/binary/reload";
    return 0;
}

int checkFirmwareContract(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    FirmwareManager firmware;

    int espflashMajor = -1;
    int espflashMinor = -1;
    const bool realEspflashFormat = FirmwareManager::parseEsp32P4Revision(
        QStringLiteral("Chip type:         esp32p4 (revision v1.3)\n"),
        espflashMajor,
        espflashMinor) && espflashMajor == 1 && espflashMinor == 3;

    int esptoolMajor = -1;
    int esptoolMinor = -1;
    const bool dashedFormat = FirmwareManager::parseEsp32P4Revision(
        QStringLiteral("Chip type:          ESP32-P4 (revision v1.0)\n"),
        esptoolMajor,
        esptoolMinor) && esptoolMajor == 1 && esptoolMinor == 0;

    int wrongMajor = -1;
    int wrongMinor = -1;
    const bool rejectsWrongChip = !FirmwareManager::parseEsp32P4Revision(
        QStringLiteral("Chip type:         esp32s3 (revision v0.2)\n"),
        wrongMajor,
        wrongMinor);

    const bool revisionPolicy =
        FirmwareManager::supportsEsp32P4Revision(0, 0) &&
        FirmwareManager::supportsEsp32P4Revision(1, 3) &&
        FirmwareManager::supportsEsp32P4Revision(2, 99) &&
        !FirmwareManager::supportsEsp32P4Revision(3, 0) &&
        !FirmwareManager::supportsEsp32P4Revision(-1, 0) &&
        !FirmwareManager::supportsEsp32P4Revision(1, -1);

    const bool flashProgressFormat =
        FirmwareManager::parseFlashProgress(QStringLiteral("Writing 7%\rWriting 64%\r")) == 64 &&
        FirmwareManager::parseFlashProgress(QStringLiteral("no progress token")) == -1 &&
        FirmwareManager::parseFlashProgress(QStringLiteral("Writing 104%")) == -1;

    const bool recoverySelection =
        SmartSessionController::chooseRecoveryPort(
            QStringLiteral("COM7"),
            {QStringLiteral("COM3"), QStringLiteral("COM7")}) == QStringLiteral("COM7") &&
        SmartSessionController::chooseRecoveryPort(
            QString{},
            {QStringLiteral("COM3")}) == QStringLiteral("COM3") &&
        SmartSessionController::chooseRecoveryPort(
            QString{},
            {QStringLiteral("COM3"), QStringLiteral("COM4")}).isEmpty();

    const bool firmwareTimeoutPolicy =
        FirmwareManager::launchTimeoutMs() == 15000 &&
        FirmwareManager::probeTimeoutMs() == 30000 &&
        FirmwareManager::flashTimeoutMs() == 180000 &&
        FirmwareManager::resetTimeoutMs() == 20000;

    QElapsedTimer workerDeadline;
    workerDeadline.start();
    while (!firmware.workerReady() && workerDeadline.elapsed() < 1500) {
        app.processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    const bool firmwareWorkerBoundary = firmware.workerReady() && firmware.workerAffinityValid();

    const bool valid = firmware.bundleReady() && firmware.flasherAvailable() &&
        firmware.firmwareVersion() == QStringLiteral(ARSTACK_STUDIO_VERSION) &&
        firmware.expectedProtocol() == QStringLiteral("1") &&
        firmware.firmwareBuildId().size() == 16 &&
        firmware.firmwareSha256().size() == 64 &&
        realEspflashFormat && dashedFormat && rejectsWrongChip && revisionPolicy &&
        flashProgressFormat && recoverySelection && firmwareTimeoutPolicy && firmwareWorkerBoundary;
    if (!valid) {
        qCritical().noquote()
            << "Firmware bundle/probe/worker contract: FAIL ·"
            << firmware.bundleStatus()
            << "espflash-format=" << realEspflashFormat
            << "dashed-format=" << dashedFormat
            << "wrong-chip-rejected=" << rejectsWrongChip
            << "revision-policy=" << revisionPolicy
            << "progress-format=" << flashProgressFormat
            << "recovery-selection=" << recoverySelection
            << "timeout-policy=" << firmwareTimeoutPolicy
            << "worker-ready=" << firmware.workerReady()
            << "process-affinity=" << firmware.workerAffinityValid();
        return 4;
    }

    firmware.shutdown();
    firmware.shutdown();

    qInfo().noquote()
        << "Firmware bundle/probe contract: PASS · target/revision/progress/hash policy + generation-aware threaded process boundary locked ·"
        << firmware.bundleStatus();
    return 0;
}

int checkP0ControllerPolicy(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    DeviceIdentity currentIdentity;
    const QString currentLine = QStringLiteral(
        "I (412) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
        "device_id=A1B2C3D4E5F6 firmware=%1 build=0123456789abcdef boot_id=0123456789ABCDEF "
        "capabilities=SMV-4I4V,PROFILE,PROFILE-BINARY-V1,LIVE-SETPOINTS,SESSION-LEASE,PTP-P2,SMPSYNCH-AUTO")
        .arg(QStringLiteral(ARSTACK_STUDIO_VERSION));
    const bool currentParsed = DeviceController::parseIdentityLine(currentLine, currentIdentity);
    const bool currentAccepted = currentParsed &&
        DeviceController::identitySupportsCurrentContract(
            currentIdentity, QStringLiteral(ARSTACK_STUDIO_VERSION), QStringLiteral("0123456789abcdef"));

    DeviceIdentity legacyIdentity;
    const QString legacyLine = QStringLiteral(
        "I (417) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
        "device_id=A1B2C3D4E5F6 firmware=%1 boot_id=0123456789ABCDEF capabilities=SMV-4I4V,PROFILE,PROFILE-BINARY-V1,LIVE-SETPOINTS,SESSION-LEASE,PTP-P2,SMPSYNCH-AUTO")
        .arg(QStringLiteral(ARSTACK_STUDIO_VERSION));
    const bool legacyParsed = DeviceController::parseIdentityLine(legacyLine, legacyIdentity);
    const bool legacyRejectedAsCurrent = legacyParsed &&
        !DeviceController::identitySupportsCurrentContract(
            legacyIdentity, QStringLiteral(ARSTACK_STUDIO_VERSION), QStringLiteral("0123456789abcdef"));

    DeviceIdentity protocolLegacy;
    const bool protocolLegacyParsed = DeviceController::parseIdentityLine(
        QStringLiteral(
            "I (420) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=0 "
            "device_id=A1B2C3D4E5F6 firmware=0.0.9 capabilities=SMV-4I4V,LIVE-SETPOINTS"),
        protocolLegacy) &&
        !DeviceController::identitySupportsCurrentContract(
            protocolLegacy, QStringLiteral(ARSTACK_STUDIO_VERSION));

    DeviceIdentity reducedCapabilities;
    const bool capabilityFailClosed = DeviceController::parseIdentityLine(
        QStringLiteral(
            "I (425) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
            "device_id=A1B2C3D4E5F6 firmware=0.1.1 boot_id=0123456789ABCDEF "
            "capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE,SMPSYNCH-AUTO"),
        reducedCapabilities) &&
        !DeviceController::identitySupportsCurrentContract(
            reducedCapabilities, QStringLiteral(ARSTACK_STUDIO_VERSION));

    DeviceIdentity rejectedIdentity;
    const bool rejectsWrongTarget = !DeviceController::parseIdentityLine(
        QStringLiteral(
            "I (430) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-S3 protocol=1 "
            "device_id=A1B2C3D4E5F6 firmware=0.1.0 boot_id=0123456789ABCDEF "
            "capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE,PTP-P2,SMPSYNCH-AUTO"),
        rejectedIdentity);
    const bool rejectsMissingFirmware = !DeviceController::parseIdentityLine(
        QStringLiteral(
            "I (435) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
            "device_id=A1B2C3D4E5F6 boot_id=0123456789ABCDEF "
            "capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE,PTP-P2,SMPSYNCH-AUTO"),
        rejectedIdentity);
    const bool rejectsMalformedBoot = !DeviceController::parseIdentityLine(
        QStringLiteral(
            "I (440) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
            "device_id=A1B2C3D4E5F6 firmware=0.1.0 boot_id=NOT-A-BOOT-ID "
            "capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE,PTP-P2,SMPSYNCH-AUTO"),
        rejectedIdentity);

    const bool boundedIdentifyPolicy =
        DeviceController::identityMaxAttempts() == 5 &&
        DeviceController::identityRetryIntervalMs() == 1100 &&
        DeviceController::identityRetryAllowed(0) &&
        DeviceController::identityRetryAllowed(1) &&
        DeviceController::identityRetryAllowed(2) &&
        DeviceController::identityRetryAllowed(3) &&
        DeviceController::identityRetryAllowed(4) &&
        !DeviceController::identityRetryAllowed(5) &&
        !DeviceController::identityRetryAllowed(-1);

    const bool boundedProfileSyncPolicy =
        SmartSessionController::profileSyncMaxAttempts() == 3 &&
        SmartSessionController::profileSyncTimeoutMs() == 4500 &&
        SmartSessionController::profileSyncRetryAllowed(0) &&
        SmartSessionController::profileSyncRetryAllowed(1) &&
        SmartSessionController::profileSyncRetryAllowed(2) &&
        !SmartSessionController::profileSyncRetryAllowed(3) &&
        !SmartSessionController::profileSyncRetryAllowed(-1) &&
        SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("1"), QStringLiteral("2")) &&
        SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("—"), QStringLiteral("2")) &&
        !SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("2"), QStringLiteral("2")) &&
        !SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("2"), QStringLiteral("1")) &&
        !SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("2"), QStringLiteral("not-a-generation"));

    const bool workerPolicyAligned =
        DeviceIoWorker::commandQueueCapacity() == DeviceController::ioCommandQueueCapacity() &&
        DeviceIoWorker::presencePollIntervalMs() == DeviceController::ioPresencePollIntervalMs() &&
        DeviceIoWorker::identityMaxAttempts() == DeviceController::identityMaxAttempts() &&
        DeviceIoWorker::identityRetryIntervalMs() == DeviceController::identityRetryIntervalMs();

    const bool generationPolicy =
        SmartSessionController::nextSessionGeneration(0) == 1 &&
        SmartSessionController::nextSessionGeneration(41) == 42 &&
        SmartSessionController::generationIsCurrent(7, 7) &&
        !SmartSessionController::generationIsCurrent(7, 6) &&
        !SmartSessionController::generationIsCurrent(0, 0) &&
        DeviceController::workerEventIsCurrent(7, 7) &&
        !DeviceController::workerEventIsCurrent(7, 6) &&
        FirmwareManager::workerEventIsCurrent(7, 7) &&
        !FirmwareManager::workerEventIsCurrent(7, 6);

    const bool portOwnershipPolicy =
        SmartSessionController::firmwareOwnershipValid(
            SmartSessionController::PortOwner::firmwareTool, false) &&
        !SmartSessionController::firmwareOwnershipValid(
            SmartSessionController::PortOwner::firmwareTool, true) &&
        !SmartSessionController::firmwareOwnershipValid(
            SmartSessionController::PortOwner::deviceSession, false) &&
        !SmartSessionController::firmwareOwnershipValid(
            SmartSessionController::PortOwner::none, false);

    const bool s6RecoveryPolicy =
        SmartSessionController::recoveryIdentityMatches(
            QStringLiteral("A1B2C3D4E5F6"), QStringLiteral("a1b2c3d4e5f6")) &&
        !SmartSessionController::recoveryIdentityMatches(
            QStringLiteral("A1B2C3D4E5F6"), QStringLiteral("001122334455")) &&
        !SmartSessionController::recoveryIdentityMatches(QString{}, QStringLiteral("A1B2C3D4E5F6")) &&
        StudioDeviceController::controlHealthProbeIntervalMs() == 2000 &&
        StudioDeviceController::controlHealthMaxMisses() == 2;

    const QString lockTestPath = QDir(QDir::tempPath()).filePath(
        QStringLiteral("arstack-studio-lock-regression-%1.lock")
            .arg(static_cast<qulonglong>(QCoreApplication::applicationPid())));
    QFile::remove(lockTestPath);
    bool secondInstanceBlocked = false;
    {
        SingleInstanceGuard first{lockTestPath};
        SingleInstanceGuard second{lockTestPath};
        secondInstanceBlocked = first.tryAcquire() && !second.tryAcquire();
    }
    bool lockReacquiredAfterRelease = false;
    {
        SingleInstanceGuard third{lockTestPath};
        lockReacquiredAfterRelease = third.tryAcquire();
    }
    QFile::remove(lockTestPath);
    const bool singleInstancePolicy = secondInstanceBlocked && lockReacquiredAfterRelease;

    SmartSessionController detachedSupervisor;
    const bool s7IntentPolicy =
        !detachedSupervisor.startReady() &&
        !detachedSupervisor.canDeployProfile() &&
        !detachedSupervisor.liveControlReady() &&
        !detachedSupervisor.requestStart() &&
        !detachedSupervisor.requestStop() &&
        !detachedSupervisor.requestConnect() &&
        !detachedSupervisor.requestConnectPort(QStringLiteral("COM7")) &&
        !detachedSupervisor.requestDisconnect() &&
        !detachedSupervisor.requestProfileSync() &&
        !detachedSupervisor.requestSetFrequency(50.0) &&
        !detachedSupervisor.requestSetSignal(
            QStringLiteral("IA"), 1.0, 0.0, 0U, 1000.0, 100.0) &&
        !detachedSupervisor.requestSetEnabled(QStringLiteral("IA"), true) &&
        !detachedSupervisor.requestSetQuality(QStringLiteral("IA"), 0U) &&
        !detachedSupervisor.requestSetCtSaturation(false, 0.0, 0.0, 2, 100.0) &&
        !detachedSupervisor.requestZero() &&
        !detachedSupervisor.requestPtpRefresh() &&
        !detachedSupervisor.requestPtpRole(QStringLiteral("SOURCE")) &&
        !detachedSupervisor.requestSmpSynch(QStringLiteral("AUTO")) &&
        !detachedSupervisor.requestConfigurePtp({}) &&
        !detachedSupervisor.requestStartPtp() &&
        !detachedSupervisor.requestStopPtp();

    if (!currentAccepted || !legacyRejectedAsCurrent || !protocolLegacyParsed ||
        !capabilityFailClosed || !rejectsWrongTarget || !rejectsMissingFirmware ||
        !rejectsMalformedBoot || !boundedIdentifyPolicy || !boundedProfileSyncPolicy ||
        !workerPolicyAligned || !generationPolicy || !portOwnershipPolicy ||
        !s6RecoveryPolicy || !singleInstancePolicy || !s7IntentPolicy) {
        qCritical().noquote()
            << "S1/S2/S3/S4/S5/S6/S7 control-plane contract: FAIL"
            << "current=" << currentAccepted
            << "legacy=" << legacyRejectedAsCurrent
            << "protocol-legacy=" << protocolLegacyParsed
            << "capability-fail-closed=" << capabilityFailClosed
            << "wrong-target=" << rejectsWrongTarget
            << "missing-firmware=" << rejectsMissingFirmware
            << "malformed-boot=" << rejectsMalformedBoot
            << "bounded-identify=" << boundedIdentifyPolicy
            << "bounded-profile-sync=" << boundedProfileSyncPolicy
            << "worker-policy-aligned=" << workerPolicyAligned
            << "generation-policy=" << generationPolicy
            << "port-owner-policy=" << portOwnershipPolicy
            << "s6-recovery-health=" << s6RecoveryPolicy
            << "single-instance=" << singleInstancePolicy
            << "s7-intent-fail-closed=" << s7IntentPolicy;
        return 11;
    }

    StudioDeviceController device;
    QElapsedTimer workerDeadline;
    workerDeadline.start();
    while (!device.ioWorkerReady() && workerDeadline.elapsed() < 1500) {
        app.processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    const bool workerBoundaryReady = device.ioWorkerReady() && device.ioWorkerAffinityValid();
    if (!workerBoundaryReady) {
        qCritical().noquote()
            << "S4 DeviceIoWorker boundary: FAIL · ready=" << device.ioWorkerReady()
            << "serial-affinity=" << device.ioWorkerAffinityValid();
        return 13;
    }

    device.setSessionGeneration(1);
    const bool startsIdle =
        device.identificationState() == DeviceController::IdentificationState::Idle &&
        device.identifyAttempts() == 0 &&
        device.sessionGeneration() == 1 &&
        !device.controlResponsive() &&
        device.missedHealthReplies() == 0;
    if (!startsIdle) {
        qCritical().noquote() << "S6 generation/health-aware identification state: FAIL";
        return 12;
    }
    if (device.start()) {
        qCritical().noquote() << "P0 controller policy: FAIL · unverified device was allowed to START";
        return 5;
    }
    if (device.deployProfile({})) {
        qCritical().noquote() << "P0 controller policy: FAIL · incompatible/unverified device accepted deploy";
        return 6;
    }
    qInfo().noquote()
        << "P0 controller policy: PASS · S1 typed identity + S2 bounded IDENTIFY + S3 bounded profile sync + S4 threaded DeviceIoWorker + S5 generation/PortOwner/FirmwareWorker + S6 device-id recovery/health/single-instance + S7 supervisor-only operator intent boundaries + unverified START/DEPLOY fail closed";
    return 0;
}
} // namespace

int main(int argc, char* argv[]) {
    if (hasArgument(argc, argv, "--check-reference-template")) {
        return checkReferenceTemplate(argc, argv);
    }
    if (hasArgument(argc, argv, "--check-firmware-contract")) {
        return checkFirmwareContract(argc, argv);
    }
    if (hasArgument(argc, argv, "--check-p0-policy")) {
        return checkP0ControllerPolicy(argc, argv);
    }

    const bool lifecycleCheck = hasArgument(argc, argv, "--check-app-lifecycle");
    const bool operatorUiCheck = hasArgument(argc, argv, "--check-operator-ui");

    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(true);
    QIcon studioIcon;
    studioIcon.addFile(QStringLiteral(":/studio/android-chrome-192x192.png"));
    studioIcon.addFile(QStringLiteral(":/studio/android-chrome-512x512.png"));
    app.setWindowIcon(studioIcon);
    QCoreApplication::setOrganizationName(QStringLiteral("ARStack61850"));
#ifdef ARSTACK_STUDIO_ENGINEERING_PREVIEW
    QCoreApplication::setApplicationName(QStringLiteral("ARStack Studio Preview"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ARSTACK_STUDIO_VERSION "-preview"));
#else
    QCoreApplication::setApplicationName(QStringLiteral("ARStack Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ARSTACK_STUDIO_VERSION));
#endif

    if (hasArgument(argc, argv, "--check-clipboard")) {
        // Headless-board independent Windows acceptance: clipboard copy must
        // round-trip and an empty report must never be reported as copied.
        StudioClipboard clipboard;
        const QString sample = QStringLiteral(
            "ARStack Studio diagnostics\\nPTP last failure: source_reason=PDELAY_RX_TIMESTAMP_UNAVAILABLE");
        const bool copied = clipboard.copyText(sample);
        const bool matches = QGuiApplication::clipboard() != nullptr &&
            QGuiApplication::clipboard()->text(QClipboard::Clipboard) == sample;
        const bool rejectsEmpty = !clipboard.copyText(QString{});
        if (!copied || !matches || !rejectsEmpty) {
            qCritical() << "Studio native clipboard regression: FAIL"
                        << copied << matches << rejectsEmpty;
            return 18;
        }
        qInfo() << "Studio native clipboard regression: PASS";
        return 0;
    }

    // S6 ownership starts before any firmware process, QML object, or serial
    // worker exists. A second Studio process therefore cannot race for the
    // injector COM handle or bypass the supervisor's in-process PortOwner.
    SingleInstanceGuard instanceGuard;
    if (!instanceGuard.tryAcquire()) {
        qCritical().noquote()
            << "ARStack Studio is already running. Close the existing instance before opening another.";
        return SingleInstanceGuard::contentionExitCode();
    }

    FirmwareManager firmwareService;

    qmlRegisterType<SclProfileModel>("ARStack.Studio", 1, 0, "SclProfileModel");
    qmlRegisterType<StudioDeviceController>("ARStack.Studio", 1, 0, "DeviceController");
    qmlRegisterType<SvCaptureController>("ARStack.Studio", 1, 0, "SvCaptureController");
    qmlRegisterType<SmartSessionController>("ARStack.Studio", 1, 0, "SmartSessionController");
    qmlRegisterSingletonInstance("ARStack.Studio", 1, 0, "FirmwareService", &firmwareService);
    StudioClipboard clipboard;
    qmlRegisterSingletonInstance("ARStack.Studio", 1, 0, "StudioClipboard", &clipboard);

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("ARStack.Studio", "Main");

    QQuickWindow* mainWindow = nullptr;
    if (!engine.rootObjects().isEmpty()) {
        mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    }

    PrimaryWindowCloseFilter primaryCloseFilter{&app};
    if (mainWindow != nullptr) {
        mainWindow->installEventFilter(&primaryCloseFilter);
    }

    QPointer<StudioDeviceController> lifecycleDevice;
    QPointer<SmartSessionController> lifecycleSession;
    if (mainWindow != nullptr) {
        lifecycleDevice = mainWindow->findChild<StudioDeviceController*>();
        lifecycleSession = mainWindow->findChild<SmartSessionController*>();
    }
    if (mainWindow != nullptr && (lifecycleDevice.isNull() || lifecycleSession.isNull())) {
        qCritical().noquote()
            << "Application lifecycle owner discovery failed; refusing an unowned worker/session lifetime.";
        return 14;
    }

    bool lifecycleCoordinatorInvoked = false;
    bool lifecycleShutdownClean = false;
    QObject::connect(
        &app,
        &QCoreApplication::aboutToQuit,
        &app,
        [&] {
            lifecycleCoordinatorInvoked = true;
            if (!lifecycleSession.isNull()) lifecycleSession->shutdown();
            const bool deviceClean = lifecycleDevice.isNull() || lifecycleDevice->shutdown();
            const bool firmwareClean = firmwareService.shutdown();
            lifecycleShutdownClean = deviceClean && firmwareClean;
            if (!lifecycleShutdownClean) {
                qCritical().noquote()
                    << "Application shutdown required an emergency worker-retirement fallback.";
            }
        },
        Qt::DirectConnection);


    if (operatorUiCheck) {
        if (mainWindow == nullptr) {
            qCritical().noquote() << "Studio UI geometry test: no main window";
            return 19;
        }
        // Exercise desktop, supported minimum, and extended desktop layout on
        // the real rendering engine. This branch never touches a serial port.
        constexpr std::array<QSize, 5> sizes{{QSize{960, 680}, QSize{1024, 748},
            QSize{1152, 760}, QSize{1366, 800}, QSize{1920, 1000}}};
        // All five logical viewport geometries are exercised by Qt Quick.
        // One smaller native viewport captures real Windows pixels; optional
        // software screenshots of larger viewports carry no native claim.
        const bool nativeCapture = !qEnvironmentVariableIsEmpty("ARSTACK_STUDIO_QA_NATIVE_ONLY");
        const int caseCount = nativeCapture ? 1 : static_cast<int>(sizes.size());
        const auto caseSize = [sizes, nativeCapture](const int step) {
            return nativeCapture ? sizes[0] : sizes.at(static_cast<std::size_t>(step));
        };
        const QString screenshots = qEnvironmentVariable("ARSTACK_STUDIO_QA_DIR");
        if (!screenshots.isEmpty() && !QDir{}.mkpath(screenshots)) {
            qCritical().noquote() << "Cannot create GUI evidence directory" << screenshots;
            return 20;
        }
        auto step = std::make_shared<int>(0);
        auto resizeRetries = std::make_shared<int>(0);
        auto* uiTimer = new QTimer(&app);
        uiTimer->setInterval(400);
        mainWindow->resize(caseSize(0));
        QObject::connect(uiTimer, &QTimer::timeout, &app,
                         [&, step, resizeRetries, uiTimer, caseSize, caseCount, screenshots] {
            const QSize target = caseSize(*step);
            if (mainWindow->size() != target) {
                // CI's virtual Windows desktop can clamp large native
                // windows. Diagnose that instead of spinning 15 seconds.
                if (++*resizeRetries > 5) {
                    qCritical().noquote() << "Studio UI cannot reach target viewport:"
                        << "requested=" << target << "actual=" << mainWindow->size()
                        << "platform=" << QGuiApplication::platformName();
                    uiTimer->stop();
                    QCoreApplication::exit(24);
                    return;
                }
                mainWindow->resize(target);
                return;
            }
            *resizeRetries = 0;
            if (!checkOperatorViewport(mainWindow, target)) {
                uiTimer->stop();
                QCoreApplication::exit(21);
                return;
            }
            if (!screenshots.isEmpty()) {
                const QString path = QDir{screenshots}.filePath(
                    QStringLiteral("studio-%1x%2.png").arg(target.width()).arg(target.height()));
                const QImage frame = mainWindow->grabWindow();
                if (frame.isNull() || !frame.save(path)) {
                    if (nativeCapture) {
                        qCritical().noquote() << "Native GUI evidence capture failed:" << path;
                        uiTimer->stop();
                        QCoreApplication::exit(22);
                        return;
                    }
                    qWarning().noquote() << "Offscreen screenshot unavailable:" << path
                        << "(viewport bounds still checked)";
                }
            }
            ++*step;
            if (*step == caseCount) {
                uiTimer->stop();
                qInfo().noquote() << "Studio operator layout acceptance: PASS";
                QCoreApplication::exit(0);
                return;
            }
            mainWindow->resize(caseSize(*step));
        });
        uiTimer->start();
        QTimer::singleShot(15000, &app, [&app, uiTimer] {
            if (uiTimer->isActive()) {
                qCritical().noquote() << "Studio UI geometry test timed out";
                QCoreApplication::exit(23);
            }
        });
    }

    if (lifecycleCheck) {
        if (mainWindow == nullptr) {
            qCritical().noquote() << "Application lifecycle regression: main window was not created.";
            return 7;
        }

        const QPointer<QQuickWindow> lifecycleTarget{mainWindow};
        QTimer::singleShot(600, &app, [&app, &primaryCloseFilter, lifecycleTarget] {
            if (lifecycleTarget.isNull()) {
                qCritical().noquote() << "Application lifecycle regression: primary window disappeared before close exercise.";
                QCoreApplication::exit(9);
                return;
            }

            lifecycleTarget->close();
            if (!primaryCloseFilter.closeObserved()) {
                qCritical().noquote() << "Application lifecycle regression: normal primary-window close bypassed the lifetime filter.";
                QCoreApplication::exit(10);
                return;
            }
            // Do not call exit() here. The regression must prove that the real
            // primary-window close path itself terminates the process.
        });
        QTimer::singleShot(3500, &app, [] {
            qCritical().noquote() << "Application lifecycle regression: primary close was not observed in time.";
            QCoreApplication::exit(8);
        });
    }

    const int result = app.exec();
    if (!lifecycleCoordinatorInvoked || !lifecycleShutdownClean) {
        qCritical().noquote()
            << "Application lifecycle regression: shutdown coordinator did not retire all workers cleanly.";
        return result == 0 ? 15 : result;
    }
    if (lifecycleCheck && result == 0) {
        qInfo().noquote()
            << "Application lifecycle regression: PASS · close -> supervisor stop -> worker join -> process exit";
    }
    return result;
}