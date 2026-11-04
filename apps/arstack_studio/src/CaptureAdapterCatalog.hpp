// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QStringList>
#include <QVector>

// Npcap ID remains the capture identity. The OS metadata is presentation only.
enum class CaptureNicKind { Ethernet, Wireless, Other };
struct CaptureNicMetadata {
    QString interfaceKey;
    QString alias;
    QString hardware;
    CaptureNicKind kind{CaptureNicKind::Other};
    bool linkUp{false};
    bool virtualAdapter{false};
};
struct CaptureAdapterCatalog {
    QStringList ids, labels, details;
    int suggested{-1};
    int physicalUpCount{0};
};
QVector<CaptureNicMetadata> discoverSystemCaptureNics();
CaptureAdapterCatalog buildCaptureAdapterCatalog(
    const QString& dumpcapList, const QVector<CaptureNicMetadata>& nics,
    const QString& preferredId = {});
