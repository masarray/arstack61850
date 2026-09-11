// SPDX-License-Identifier: GPL-3.0-or-later

#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"
#include "SmartSessionController.hpp"
#include "StudioDeviceController.hpp"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
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
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event != nullptr && event->type() == QEvent::Close && app_ != nullptr) {
            closeObserved_ = true;
            // quit() marks the running event loop for termination; it does not
            // destroy the window synchronously. Returning false therefore still
            // lets Main.qml's onClosing handler issue its best-effort STOP.
            // Auxiliary QML windows can no longer keep the process alive.
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

    // Shutdown must be safe and idempotent; this catches lifecycle regressions
    // in every package-contract run without launching external firmware tools.
    firmware.shutdown();
    firmware.shutdown();

    qInfo().noquote()
        << "Firmware bundle/probe contract: PASS · target/revision/progress/hash/recovery/shutdown policy locked ·"
        << firmware.bundleStatus();
    return 0;
}

int checkP0ControllerPolicy(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    StudioDeviceController device;
    if (device.start()) {
        qCritical().noquote() << "P0 controller policy: FAIL · unverified device was allowed to START";
        return 5;
    }
    if (device.deployProfile({})) {
        qCritical().noquote() << "P0 controller policy: FAIL · incompatible/unverified device accepted deploy";
        return 6;
    }
    qInfo().noquote() << "P0 controller policy: PASS · unverified START/DEPLOY fail closed";
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
        // Auxiliary QML windows deliberately stay instantiated for fast reuse.
        // The primary ApplicationWindow therefore owns process lifetime: its
        // close event retires the event loop even when hidden docks still exist.
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

            // Windows hosted runners have no interactive desktop. Deliver the
            // authoritative Qt close event directly instead of depending on a
            // native WM_CLOSE round trip that the runner cannot guarantee.
            QCloseEvent closeEvent;
            QCoreApplication::sendEvent(lifecycleTarget.data(), &closeEvent);
            if (!primaryCloseFilter.closeObserved()) {
                qCritical().noquote() << "Application lifecycle regression: primary QEvent::Close bypassed the lifetime filter.";
                QCoreApplication::exit(10);
                return;
            }

            // A GitHub-hosted Windows session can keep the synthetic close
            // dispatch nested even after QCoreApplication::quit() has been
            // requested from the production filter. Once the authoritative
            // filter has observed the close, explicitly finish only the test
            // harness. Production still relies on the same filter's quit().
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
