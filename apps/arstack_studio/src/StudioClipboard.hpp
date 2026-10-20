// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

// GUI-only clipboard adapter. The shared session supervisor stays QtCore-only.
class StudioClipboard final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    // Return success only if the OS clipboard can read back the exact value.
    // Never claim a successful diagnostic copy merely because setText returned.
    Q_INVOKABLE bool copyText(const QString& text) const;
};
