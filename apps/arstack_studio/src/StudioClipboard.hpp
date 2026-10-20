// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

// GUI-only clipboard adapter. The shared session supervisor stays QtCore-only.
class StudioClipboard final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    // Returns true only when the host clipboard contains the requested snapshot.
    // Empty snapshots and unavailable/busy clipboard never report success.
    Q_INVOKABLE bool copyText(const QString& text) const;
};
