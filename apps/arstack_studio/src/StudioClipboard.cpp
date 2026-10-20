// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioClipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>

bool StudioClipboard::copyText(const QString& text) const {
    if (text.isEmpty()) return false;
    auto* clipboard = QGuiApplication::clipboard();
    if (clipboard == nullptr) return false;
    clipboard->setText(text, QClipboard::Clipboard);
    return clipboard->text(QClipboard::Clipboard) == text;
}
