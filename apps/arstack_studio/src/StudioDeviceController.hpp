// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeviceController.hpp"

#include <QRegularExpression>

#ifndef ARSTACK_STUDIO_VERSION
#define ARSTACK_STUDIO_VERSION "0.1.0"
#endif

class StudioDeviceController : public DeviceController {
    Q_OBJECT

public:
    explicit StudioDeviceController(QObject* parent = nullptr) : DeviceController(parent) {}

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

    Q_INVOKABLE bool start() {
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
        return DeviceController::start();
    }

    Q_INVOKABLE bool deployProfile(const QVariantMap& profile) {
        if (protocolVersion() != QStringLiteral("1") || !currentFirmwareIdentitySeen()) {
            emit deviceMessage(QStringLiteral("Firmware update required before profile synchronization."));
            return false;
        }
        return DeviceController::deployProfile(profile);
    }
};
