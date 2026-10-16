// SPDX-License-Identifier: GPL-3.0-or-later
#include "StudioClipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>

void StudioClipboard::copyText(const QString& text) const {
    if (auto* clipboard = QGuiApplication::clipboard(); clipboard != nullptr)
        clipboard->setText(text);
}
