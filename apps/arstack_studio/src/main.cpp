// SPDX-License-Identifier: GPL-3.0-or-later

#include "FirmwareManager.hpp"
#include "SclProfileModel.hpp"
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
} // namespace

int main(int argc, char* argv[]) {
    if (hasArgument(argc, argv, "--check-reference-template")) {
        return checkReferenceTemplate(argc, argv);
    }

    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("ARStack61850"));
    QCoreApplication::setApplicationName(QStringLiteral("ARStack Studio"));
    QCoreApplication::setApplicationVersion(QStringLiteral(ARSTACK_STUDIO_VERSION));

    qmlRegisterType<SclProfileModel>("ARStack.Studio", 1, 0, "SclProfileModel");
    qmlRegisterType<StudioDeviceController>("ARStack.Studio", 1, 0, "DeviceController");
    qmlRegisterType<FirmwareManager>("ARStack.Studio", 1, 0, "FirmwareManager");

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
