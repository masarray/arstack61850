// SPDX-License-Identifier: GPL-3.0-or-later

#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"
#include "SmartSessionController.hpp"
#include "StudioDeviceController.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
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

    const bool valid = firmware.bundleReady() && firmware.flasherAvailable() &&
        firmware.firmwareVersion() == QStringLiteral(ARSTACK_STUDIO_VERSION) &&
        firmware.expectedProtocol() == QStringLiteral("1") &&
        firmware.firmwareSha256().size() == 64 &&
        realEspflashFormat && dashedFormat && rejectsWrongChip;
    if (!valid) {
        qCritical().noquote()
            << "Firmware bundle/probe contract: FAIL ·"
            << firmware.bundleStatus()
            << "espflash-format=" << realEspflashFormat
            << "dashed-format=" << dashedFormat
            << "wrong-chip-rejected=" << rejectsWrongChip;
        return 4;
    }
    qInfo().noquote()
        << "Firmware bundle/probe contract: PASS · real espflash P4 revision format accepted ·"
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

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ARStack61850"));
    QCoreApplication::setApplicationName(QStringLiteral("ARStack Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ARSTACK_STUDIO_VERSION));

    qmlRegisterType<SclProfileModel>("ARStack.Studio", 1, 0, "SclProfileModel");
    qmlRegisterType<StudioDeviceController>("ARStack.Studio", 1, 0, "DeviceController");
    qmlRegisterType<FirmwareManager>("ARStack.Studio", 1, 0, "FirmwareManager");
    qmlRegisterType<SmartSessionController>("ARStack.Studio", 1, 0, "SmartSessionController");

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        [] { QCoreApplication::exit(-1); },
        Qt::QueuedConnection);
    engine.loadFromModule("ARStack.Studio", "Main");

    return app.exec();
}
