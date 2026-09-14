// SPDX-License-Identifier: GPL-3.0-or-later

#include "DeviceIoWorker.hpp"
#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"
#include "SmartSessionController.hpp"
#include "StudioDeviceController.hpp"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QThread>
#include <QTimer>
#include <QtQml/qqml.h>

#include <algorithm>
#include <string_view>

#ifndef ARSTACK_STUDIO_VERSION
#define ARSTACK_STUDIO_VERSION "0.1.0"
#endif

namespace {
bool hasArgument(const int argc, char* argv[], const std::string_view wanted) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] != nullptr && std::string_view{argv[i]} == wanted) return true;
    }
    return false;
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
            app_->quit();
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
        profile.value(QStringLiteral("channelLeafCount")).toULongLong() == 16ULL;

    if (!valid) {
        qCritical().noquote() << "4I+4V reference template regression:" << profile;
        return 3;
    }

    qInfo().noquote()
        << "ARStack 4I+4V 9-2LE reference template: PASS · Class A · ready · 4000 fps · 64 B";
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

    const bool valid = firmware.bundleReady() && firmware.flasherAvailable() &&
        firmware.firmwareVersion() == QStringLiteral(ARSTACK_STUDIO_VERSION) &&
        firmware.expectedProtocol() == QStringLiteral("1") &&
        firmware.firmwareSha256().size() == 64 &&
        realEspflashFormat && dashedFormat && rejectsWrongChip && revisionPolicy &&
        flashProgressFormat && recoverySelection;
    if (!valid) {
        qCritical().noquote()
            << "Firmware bundle/probe contract: FAIL ·"
            << firmware.bundleStatus()
            << "espflash-format=" << realEspflashFormat
            << "dashed-format=" << dashedFormat
            << "wrong-chip-rejected=" << rejectsWrongChip
            << "revision-policy=" << revisionPolicy
            << "progress-format=" << flashProgressFormat
            << "recovery-selection=" << recoverySelection;
        return 4;
    }

    firmware.shutdown();
    firmware.shutdown();

    qInfo().noquote()
        << "Firmware bundle/probe contract: PASS · target/revision/progress/hash/recovery/shutdown policy locked ·"
        << firmware.bundleStatus();
    return 0;
}

int checkP0ControllerPolicy(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    DeviceIdentity currentIdentity;
    // Match the real ESP-IDF serial framing. The semantic parser must extract
    // the contract from the log prefix without making the human log itself the
    // source of product state.
    const QString currentLine = QStringLiteral(
        "I (412) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
        "device_id=A1B2C3D4E5F6 firmware=%1 boot_id=0123456789ABCDEF "
        "capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE")
        .arg(QStringLiteral(ARSTACK_STUDIO_VERSION));
    const bool currentParsed = DeviceController::parseIdentityLine(currentLine, currentIdentity);
    const bool currentAccepted = currentParsed &&
        DeviceController::identitySupportsCurrentContract(
            currentIdentity, QStringLiteral(ARSTACK_STUDIO_VERSION));

    DeviceIdentity legacyIdentity;
    const QString legacyLine = QStringLiteral(
        "I (417) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
        "device_id=A1B2C3D4E5F6 firmware=%1 capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE")
        .arg(QStringLiteral(ARSTACK_STUDIO_VERSION));
    const bool legacyParsed = DeviceController::parseIdentityLine(legacyLine, legacyIdentity);
    const bool legacyRejectedAsCurrent = legacyParsed &&
        !DeviceController::identitySupportsCurrentContract(
            legacyIdentity, QStringLiteral(ARSTACK_STUDIO_VERSION));

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
            "device_id=A1B2C3D4E5F6 firmware=0.1.0 boot_id=0123456789ABCDEF "
            "capabilities=SMV-4I4V,LIVE-SETPOINTS"),
        reducedCapabilities) &&
        !DeviceController::identitySupportsCurrentContract(
            reducedCapabilities, QStringLiteral(ARSTACK_STUDIO_VERSION));

    DeviceIdentity rejectedIdentity;
    const bool rejectsWrongTarget = !DeviceController::parseIdentityLine(
        QStringLiteral(
            "I (430) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-S3 protocol=1 "
            "device_id=A1B2C3D4E5F6 firmware=0.1.0 boot_id=0123456789ABCDEF "
            "capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE"),
        rejectedIdentity);
    const bool rejectsMissingFirmware = !DeviceController::parseIdentityLine(
        QStringLiteral(
            "I (435) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
            "device_id=A1B2C3D4E5F6 boot_id=0123456789ABCDEF "
            "capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE"),
        rejectedIdentity);
    const bool rejectsMalformedBoot = !DeviceController::parseIdentityLine(
        QStringLiteral(
            "I (440) ar_smv_ctrl: ARSTACK identity product=SMV-INJECTOR target=ESP32-P4 protocol=1 "
            "device_id=A1B2C3D4E5F6 firmware=0.1.0 boot_id=NOT-A-BOOT-ID "
            "capabilities=SMV-4I4V,LIVE-SETPOINTS,SESSION-LEASE"),
        rejectedIdentity);

    const bool boundedIdentifyPolicy =
        DeviceController::identityMaxAttempts() == 3 &&
        DeviceController::identityRetryIntervalMs() == 650 &&
        DeviceController::identityRetryAllowed(0) &&
        DeviceController::identityRetryAllowed(1) &&
        DeviceController::identityRetryAllowed(2) &&
        !DeviceController::identityRetryAllowed(3) &&
        !DeviceController::identityRetryAllowed(-1);

    const bool boundedProfileSyncPolicy =
        SmartSessionController::profileSyncMaxAttempts() == 2 &&
        SmartSessionController::profileSyncTimeoutMs() == 2500 &&
        SmartSessionController::profileSyncRetryAllowed(0) &&
        SmartSessionController::profileSyncRetryAllowed(1) &&
        !SmartSessionController::profileSyncRetryAllowed(2) &&
        !SmartSessionController::profileSyncRetryAllowed(-1) &&
        SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("1"), QStringLiteral("2")) &&
        SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("—"), QStringLiteral("2")) &&
        !SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("2"), QStringLiteral("2")) &&
        !SmartSessionController::profileGenerationAdvanced(
            QStringLiteral("2"), QStringLiteral("not-a-generation"));

    const bool workerPolicyAligned =
        DeviceIoWorker::commandQueueCapacity() == DeviceController::ioCommandQueueCapacity() &&
        DeviceIoWorker::presencePollIntervalMs() == DeviceController::ioPresencePollIntervalMs() &&
        DeviceIoWorker::identityMaxAttempts() == DeviceController::identityMaxAttempts() &&
        DeviceIoWorker::identityRetryIntervalMs() == DeviceController::identityRetryIntervalMs();

    if (!currentAccepted || !legacyRejectedAsCurrent || !protocolLegacyParsed ||
        !capabilityFailClosed || !rejectsWrongTarget || !rejectsMissingFirmware ||
        !rejectsMalformedBoot || !boundedIdentifyPolicy || !boundedProfileSyncPolicy ||
        !workerPolicyAligned) {
        qCritical().noquote()
            << "S1/S2/S3/S4 control-plane contract: FAIL"
            << "current=" << currentAccepted
            << "legacy=" << legacyRejectedAsCurrent
            << "protocol-legacy=" << protocolLegacyParsed
            << "capability-fail-closed=" << capabilityFailClosed
            << "wrong-target=" << rejectsWrongTarget
            << "missing-firmware=" << rejectsMissingFirmware
            << "malformed-boot=" << rejectsMalformedBoot
            << "bounded-identify=" << boundedIdentifyPolicy
            << "bounded-profile-sync=" << boundedProfileSyncPolicy
            << "worker-policy-aligned=" << workerPolicyAligned;
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

    const bool startsIdle =
        device.identificationState() == DeviceController::IdentificationState::Idle &&
        device.identifyAttempts() == 0;
    if (!startsIdle) {
        qCritical().noquote() << "S2 identification state: FAIL · controller did not start Idle";
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
        << "P0 controller policy: PASS · S1 typed identity + S2 bounded IDENTIFY + S3 bounded profile sync + S4 threaded DeviceIoWorker ownership + unverified START/DEPLOY fail closed";
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

    QGuiApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(true);
    QCoreApplication::setOrganizationName(QStringLiteral("ARStack61850"));
    QCoreApplication::setApplicationName(QStringLiteral("ARStack Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ARSTACK_STUDIO_VERSION));

    FirmwareManager firmwareService;
    QObject::connect(
        &app,
        &QCoreApplication::aboutToQuit,
        &firmwareService,
        &FirmwareManager::shutdown,
        Qt::DirectConnection);

    qmlRegisterType<SclProfileModel>("ARStack.Studio", 1, 0, "SclProfileModel");
    qmlRegisterType<StudioDeviceController>("ARStack.Studio", 1, 0, "DeviceController");
    qmlRegisterType<SmartSessionController>("ARStack.Studio", 1, 0, "SmartSessionController");
    qmlRegisterSingletonInstance("ARStack.Studio", 1, 0, "FirmwareService", &firmwareService);

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

            QCloseEvent closeEvent;
            QCoreApplication::sendEvent(lifecycleTarget.data(), &closeEvent);
            if (!primaryCloseFilter.closeObserved()) {
                qCritical().noquote() << "Application lifecycle regression: primary QEvent::Close bypassed the lifetime filter.";
                QCoreApplication::exit(10);
                return;
            }

            QCoreApplication::exit(0);
        });
        QTimer::singleShot(3500, &app, [] {
            qCritical().noquote() << "Application lifecycle regression: primary close was not observed in time.";
            QCoreApplication::exit(8);
        });
    }

    const int result = app.exec();
    if (lifecycleCheck && result == 0) {
        qInfo().noquote() << "Application lifecycle regression: PASS · primary window close reached the lifetime filter";
    }
    return result;
}
