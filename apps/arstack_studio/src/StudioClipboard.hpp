// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

// GUI-only clipboard adapter. The shared session supervisor stays QtCore-only.
class StudioClipboard final : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void copyText(const QString& text) const;
};
