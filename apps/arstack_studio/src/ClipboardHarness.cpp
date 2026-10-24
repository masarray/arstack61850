// SPDX-License-Identifier: GPL-3.0-or-later
// Native GUI clipboard contract: exercise the SAME production adapter as Studio.
#include "StudioClipboard.hpp"

#include <QClipboard>
#include <QGuiApplication>
#include <QString>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    StudioClipboard clipboard;
    auto* osClipboard = QGuiApplication::clipboard();
    if (osClipboard == nullptr) return 2;

    if (clipboard.copyText(QString{})) return 3; // no fabricated success
    constexpr auto kHeader = "ARStack Studio field diagnostics";
    const QString evidence = QString::fromUtf8(
        "ARStack Studio field diagnostics\n"
        "Installed firmware: df73fea0b68e7f47\n"
        "PTP START error: PTPROLE rejected: stop PTP before changing operating role\n"
        "SVSYNC event: ETHERNET_LINK_LOSS\n");
    if (!evidence.startsWith(QString::fromLatin1(kHeader))) return 4;
    if (!clipboard.copyText(evidence)) return 5;
    if (osClipboard->text(QClipboard::Clipboard) != evidence) return 6;

    const QString second = QString::fromUtf8(
        "PTP diagnostic: SOURCE running\n"
        "MAC phase ±2 us\n"
        "SVSYNC ACQUIRED\n");
    if (!clipboard.copyText(second)) return 7;
    if (osClipboard->text(QClipboard::Clipboard) != second) return 8;
    if (osClipboard->text(QClipboard::Clipboard) == evidence) return 9;
    return 0;
}
