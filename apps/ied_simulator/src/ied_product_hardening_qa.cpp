// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProductHardeningController.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <iostream>

namespace {
bool writeJson(const QString& path, const QJsonObject& object) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return file.write(QJsonDocument(object).toJson(QJsonDocument::Compact)) >= 0;
}

QJsonObject endpoint(const QString& host, const int port) {
    return {{QStringLiteral("host"), host}, {QStringLiteral("port"), port}};
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid()) {
        std::cerr << "Could not create temporary product-state directory.\n";
        return 2;
    }
    const auto path = temp.filePath(QStringLiteral("product-state-v1.json"));

    {
        ProductHardeningController state(path);
        if (!state.settingsHealthy() || state.workspaceIndex() != 0 ||
            !state.recentEndpoints().isEmpty() || state.automaticReconnectOnStartup()) {
            std::cerr << "Safe default product state contract failed.\n";
            return 3;
        }
        state.setWorkspaceIndex(4);
        for (int index = 1; index <= 10; ++index) {
            if (!state.rememberEndpoint(QStringLiteral("192.0.2.%1").arg(index), 100 + index)) {
                std::cerr << "Could not persist recent endpoint.\n";
                return 4;
            }
        }
        if (state.recentEndpoints().size() != 8 || state.lastHost() != QStringLiteral("192.0.2.10") ||
            state.lastPort() != 110) {
            std::cerr << "Recent endpoint bound/order contract failed.\n";
            return 5;
        }
        const auto beforeDedupe = state.recentEndpoints().size();
        if (!state.rememberEndpoint(QStringLiteral("192.0.2.9"), 109) ||
            state.recentEndpoints().size() != beforeDedupe || state.lastHost() != QStringLiteral("192.0.2.9")) {
            std::cerr << "Recent endpoint dedupe contract failed.\n";
            return 6;
        }
        state.setWorkspaceIndex(99);
        if (state.workspaceIndex() != 4 || state.rememberEndpoint(QString{}, 102) ||
            state.rememberEndpoint(QStringLiteral("relay.local"), 0)) {
            std::cerr << "Invalid workspace/endpoint did not fail closed.\n";
            return 7;
        }
    }

    {
        ProductHardeningController restored(path);
        if (!restored.settingsHealthy() || restored.workspaceIndex() != 4 ||
            restored.recentEndpoints().size() != 8 || restored.lastHost() != QStringLiteral("192.0.2.9") ||
            restored.lastPort() != 109 || restored.automaticReconnectOnStartup()) {
            std::cerr << "Restart persistence contract failed.\n";
            return 8;
        }
#ifdef Q_OS_WIN
        if (!restored.npcapRequired() || restored.rawEthernetReady() != restored.npcapAvailable()) {
            std::cerr << "Windows Npcap readiness contract failed.\n";
            return 9;
        }
#else
        if (restored.npcapRequired() || !restored.npcapAvailable() || !restored.rawEthernetReady()) {
            std::cerr << "Non-Windows Npcap readiness contract failed.\n";
            return 9;
        }
#endif
    }

    {
        QFile corrupt(path);
        if (!corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate) || corrupt.write("{not-json") < 0) {
            std::cerr << "Could not create corrupt product state.\n";
            return 10;
        }
        ProductHardeningController damaged(path);
        if (damaged.settingsHealthy() || damaged.workspaceIndex() != 0 ||
            !damaged.recentEndpoints().isEmpty()) {
            std::cerr << "Corrupt state was not ignored fail-closed.\n";
            return 11;
        }
        damaged.setWorkspaceIndex(6);
        if (!damaged.settingsHealthy() || !damaged.rememberEndpoint(QStringLiteral("recovered.local"), 102)) {
            std::cerr << "Crash-safe state recovery failed.\n";
            return 12;
        }
        ProductHardeningController recovered(path);
        if (!recovered.settingsHealthy() || recovered.workspaceIndex() != 6 ||
            recovered.lastHost() != QStringLiteral("recovered.local")) {
            std::cerr << "Recovered product state did not survive restart.\n";
            return 13;
        }
    }

    {
        QJsonObject unsupported;
        unsupported.insert(QStringLiteral("schema"), 99);
        unsupported.insert(QStringLiteral("workspaceIndex"), 1);
        unsupported.insert(QStringLiteral("recentEndpoints"), QJsonArray{});
        if (!writeJson(path, unsupported)) return 14;
        ProductHardeningController rejected(path);
        if (rejected.settingsHealthy() || rejected.workspaceIndex() != 0 ||
            !rejected.recentEndpoints().isEmpty()) {
            std::cerr << "Unsupported schema was not rejected.\n";
            return 15;
        }
    }

    {
        QJsonArray tooMany;
        for (int index = 0; index < 9; ++index) {
            tooMany.append(endpoint(QStringLiteral("198.51.100.%1").arg(index + 1), 102));
        }
        QJsonObject unbounded;
        unbounded.insert(QStringLiteral("schema"), 1);
        unbounded.insert(QStringLiteral("workspaceIndex"), 0);
        unbounded.insert(QStringLiteral("recentEndpoints"), tooMany);
        if (!writeJson(path, unbounded)) return 16;
        ProductHardeningController rejected(path);
        if (rejected.settingsHealthy() || !rejected.recentEndpoints().isEmpty()) {
            std::cerr << "Unbounded persisted recents were not rejected.\n";
            return 17;
        }
    }

    std::cout << "PRODUCT_HARDENING_PASS"
              << " state=atomic"
              << " workspace_restore=4"
              << " recent=8"
              << " dedupe=pass"
              << " bounded_recent=8"
              << " crash_recovery=pass"
              << " auto_reconnect=false"
              << " npcap_policy=explicit\n";
    std::cout << "PRODUCT_HARDENING_NEGATIVE_PASS"
              << " corrupt_state=ignored"
              << " unsupported_schema=rejected"
              << " invalid_workspace=rejected"
              << " invalid_endpoint=rejected"
              << " unbounded_recent=rejected\n";
    return 0;
}
