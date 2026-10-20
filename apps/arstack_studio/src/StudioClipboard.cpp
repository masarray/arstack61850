// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioClipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>

bool StudioClipboard::copyText(const QString& text) const {
    if (text.trimmed().isEmpty()) return false;
    QClipboard* const clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) return false;
    // Windows may refuse an occupied native clipboard. Qt's setText() has no
    // success return, so read back its content before telling the operator
    // the diagnostics are available for pasting.
    clipboard->setText(text, QClipboard::Clipboard);
    return clipboard->text(QClipboard::Clipboard) == text;
}
