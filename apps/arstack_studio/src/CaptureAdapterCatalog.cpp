// SPDX-License-Identifier: GPL-3.0-or-later
#include "CaptureAdapterCatalog.hpp"
#include <QHash>
#include <QRegularExpression>
#include <algorithm>
#include <vector>
#ifdef Q_OS_WIN
#include <winsock2.h>
#include <iphlpapi.h>
#include <ipifcons.h>
#endif

namespace {
QString nicKey(const QString& text) {
    static const QRegularExpression guid(
        QStringLiteral(R"(([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}))"));
    const auto found = guid.match(text);
    return found.hasMatch() ? found.captured(1).toUpper() : text.trimmed().toCaseFolded();
}
bool isVirtual(const QString& text) {
    const QString lower = text.toLower();
    for (const auto* token : {"virtual", "hyper-v", "vmware", "virtualbox",
                              "wintun", "loopback", "bluetooth", "tap-windows", "miniport", "vpn"}) {
        if (lower.contains(QString::fromLatin1(token))) return true;
    }
    return false;
}
struct Candidate {
    QString id, label, detail;
    int priority{3};
    bool physicalUp{false};
};
} // namespace

QVector<CaptureNicMetadata> discoverSystemCaptureNics() {
    QVector<CaptureNicMetadata> result;
#ifdef Q_OS_WIN
    ULONG size = 16384;
    std::vector<unsigned char> bytes(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
    ULONG status = GetAdaptersAddresses(
        AF_UNSPEC, GAA_FLAG_INCLUDE_ALL_INTERFACES, nullptr, list, &size);
    if (status == ERROR_BUFFER_OVERFLOW && size <= 2 * 1024 * 1024) {
        bytes.resize(size);
        list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(bytes.data());
        status = GetAdaptersAddresses(
            AF_UNSPEC, GAA_FLAG_INCLUDE_ALL_INTERFACES, nullptr, list, &size);
    }
    if (status != NO_ERROR) return result;
    for (const auto* item = list; item; item = item->Next) {
        if (!item->AdapterName) continue;
        CaptureNicMetadata nic;
        nic.interfaceKey = QString::fromLocal8Bit(item->AdapterName);
        if (item->FriendlyName) nic.alias = QString::fromWCharArray(item->FriendlyName).trimmed();
        if (item->Description) nic.hardware = QString::fromWCharArray(item->Description).trimmed();
        nic.kind = item->IfType == IF_TYPE_ETHERNET_CSMACD
            ? CaptureNicKind::Ethernet
            : item->IfType == IF_TYPE_IEEE80211
                ? CaptureNicKind::Wireless : CaptureNicKind::Other;
        nic.linkUp = item->OperStatus == IfOperStatusUp;
        nic.virtualAdapter = isVirtual(nic.alias + QLatin1Char(' ') + nic.hardware);
        result.append(nic);
    }
#endif
    return result;
}

CaptureAdapterCatalog buildCaptureAdapterCatalog(
    const QString& dumpcapList, const QVector<CaptureNicMetadata>& nics,
    const QString& preferredId) {
    CaptureAdapterCatalog result;
    QHash<QString, CaptureNicMetadata> byId;
    for (const auto& nic : nics) byId.insert(nicKey(nic.interfaceKey), nic);
    static const QRegularExpression linePattern(
        QStringLiteral(R"(^\s*\d+\.\s+(\S+)(?:\s+\((.*)\))?)"));
    QVector<Candidate> rows;
    for (const auto& line : dumpcapList.split(QLatin1Char('\n'))) {
        const auto match = linePattern.match(line);
        if (!match.hasMatch()) continue;
        const QString id = match.captured(1);
        if (id.isEmpty()) continue;
        const auto it = byId.constFind(nicKey(id));
        const bool osKnown = it != byId.constEnd();
        const CaptureNicMetadata nic = osKnown ? it.value() : CaptureNicMetadata{};
        const QString dumpcapLabel = match.captured(2).trimmed();
        const QString hardware = nic.hardware.isEmpty() ? dumpcapLabel : nic.hardware;
        const QString name = !nic.alias.isEmpty() && !hardware.isEmpty() &&
            nic.alias.compare(hardware, Qt::CaseInsensitive) != 0
            ? nic.alias + QStringLiteral(" — ") + hardware
            : !hardware.isEmpty() ? hardware : !nic.alias.isEmpty() ? nic.alias
            : QStringLiteral("Unidentified capture interface");
        const bool virtualNic = nic.virtualAdapter || (!osKnown && isVirtual(dumpcapLabel));
        const bool ethernet = osKnown && nic.kind == CaptureNicKind::Ethernet && !virtualNic;
        const bool wireless = osKnown && nic.kind == CaptureNicKind::Wireless;
        const bool physicalUp = ethernet && nic.linkUp;
        const QString type = ethernet ? QStringLiteral("Ethernet")
            : wireless ? QStringLiteral("Wi-Fi")
            : virtualNic ? QStringLiteral("Virtual") : QStringLiteral("Other");
        Candidate row;
        row.id = id;
        row.label = type + QStringLiteral(" · ") + name;
        row.detail = osKnown
            ? QStringLiteral("%1 · %2 · Npcap capture available")
                .arg(nic.linkUp ? QStringLiteral("Connected") : QStringLiteral("Disconnected"),
                     ethernet ? QStringLiteral("Wired network adapter")
                     : wireless ? QStringLiteral("Wireless network adapter")
                     : virtualNic ? QStringLiteral("Virtual network adapter")
                     : QStringLiteral("Other network adapter"))
            : QStringLiteral("Windows NIC identity unavailable; verify the capture interface.");
        row.priority = physicalUp ? 0 : ethernet ? 1 : wireless ? 2 : 3;
        row.physicalUp = physicalUp;
        rows.append(row);
    }
    std::stable_sort(rows.begin(), rows.end(),
        [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });
    for (const auto& row : rows) {
        result.ids.append(row.id);
        result.labels.append(row.label);
        result.details.append(row.detail);
        if (row.physicalUp) ++result.physicalUpCount;
    }
    const auto preferred = nicKey(preferredId);
    if (!preferredId.isEmpty()) {
        for (int i = 0; i < rows.size(); ++i) {
            if (nicKey(rows[i].id) == preferred) {
                result.suggested = i;
                return result;
            }
        }
    }
    if (result.physicalUpCount == 1) {
        for (int i = 0; i < rows.size(); ++i) {
            if (rows[i].physicalUp) {
                result.suggested = i;
                break;
            }
        }
    }
    return result;
}
