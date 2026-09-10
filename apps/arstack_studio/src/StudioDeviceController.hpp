// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "DeviceController.hpp"

class StudioDeviceController final : public DeviceController {
    Q_OBJECT

public:
    explicit StudioDeviceController(QObject* parent = nullptr) : DeviceController(parent) {}

    Q_INVOKABLE bool start() {
        if (!deviceVerified()) {
            emit deviceMessage(QStringLiteral("Connect and verify the ARStack ESP32-P4 before Start."));
            return false;
        }
        if (protocolVersion() != QStringLiteral("1")) {
            emit deviceMessage(QStringLiteral("Firmware protocol mismatch. Install the P0 firmware before Start."));
            return false;
        }
        if (!profileArmed() || profileDeploying()) {
            emit deviceMessage(QStringLiteral("Deploy and arm a validated 4I+4V profile before Start."));
            return false;
        }
        return DeviceController::start();
    }

    Q_INVOKABLE bool deployProfile(const QVariantMap& profile) {
        if (protocolVersion() != QStringLiteral("1")) {
            emit deviceMessage(QStringLiteral("Firmware protocol mismatch. Profile deployment is blocked."));
            return false;
        }
        return DeviceController::deployProfile(profile);
    }
};
