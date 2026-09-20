// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProductHardeningController.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
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

QString absolutePath(const QString& path) {
    return QFileInfo(path).absoluteFilePath();
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
            !state.recentEndpoints().isEmpty() || !state.recentResources().isEmpty() ||
            state.automaticReconnectOnStartup()) {
            std::cerr << "Safe default product state contract failed.\n";
            return 3;
        }

        state.setWorkspaceIndex(2);
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

        const auto beforeEndpointDedupe = state.recentEndpoints().size();
        if (!state.rememberEndpoint(QStringLiteral("192.0.2.9"), 109) ||
            state.recentEndpoints().size() != beforeEndpointDedupe ||
            state.lastHost() != QStringLiteral("192.0.2.9")) {
            std::cerr << "Recent endpoint dedupe contract failed.\n";
            return 6;
        }

        for (int index = 1; index <= 10; ++index) {
            if (!state.rememberResource(temp.filePath(QStringLiteral("model-%1.scd").arg(index)))) {
                std::cerr << "Could not persist recent engineering resource.\n";
                return 7;
            }
        }
        if (state.recentResources().size() != 8 ||
            state.recentResourcePath(0) != absolutePath(temp.filePath(QStringLiteral("model-10.scd")))) {
            std::cerr << "Recent resource bound/order contract failed.\n";
            return 8;
        }

        const auto beforeResourceDedupe = state.recentResources().size();
        if (!state.rememberResource(temp.filePath(QStringLiteral("model-9.scd"))) ||
            state.recentResources().size() != beforeResourceDedupe ||
            state.recentResourcePath(0) != absolutePath(temp.filePath(QStringLiteral("model-9.scd"))) ||
            !state.recentResourceUrl(0).isLocalFile()) {
            std::cerr << "Recent resource dedupe/url contract failed.\n";
            return 9;
        }

        state.setWorkspaceIndex(99);
        if (state.workspaceIndex() != 2 || state.rememberEndpoint(QString{}, 102) ||
            state.rememberEndpoint(QStringLiteral("relay.local"), 0) ||
            state.rememberResource(QStringLiteral("relative-model.scd")) ||
            state.rememberResource(QStringLiteral("https://example.invalid/model.scd"))) {
            std::cerr << "Invalid workspace/endpoint/resource did not fail closed.\n";
            return 10;
        }
    }

    {
        ProductHardeningController restored(path);
        if (!restored.settingsHealthy() || restored.workspaceIndex() != 2 ||
            restored.recentEndpoints().size() != 8 || restored.lastHost() != QStringLiteral("192.0.2.9") ||
            restored.lastPort() != 109 || restored.recentResources().size() != 8 ||
            restored.recentResourcePath(0) != absolutePath(temp.filePath(QStringLiteral("model-9.scd"))) ||
            restored.automaticReconnectOnStartup()) {
            std::cerr << "Restart persistence contract failed.\n";
            return 11;
        }
#ifdef Q_OS_WIN
        if (!restored.npcapRequired() || restored.rawEthernetReady() != restored.npcapAvailable() ||
            !restored.npcapStatus().contains(QStringLiteral("Npcap"), Qt::CaseInsensitive) ||
            (!restored.npcapAvailable() &&
             !restored.npcapStatus().contains(QStringLiteral("Install Npcap"), Qt::CaseInsensitive))) {
            std::cerr << "Windows Npcap readiness/guidance contract failed.\n";
            return 12;
        }
        std::cout << "WINDOWS_RUNTIME_READINESS_PASS"
                  << " npcap_required=true"
                  << " npcap_available=" << (restored.npcapAvailable() ? "true" : "false")
                  << " guidance=pass"
                  << " raw_ethernet_ready=" << (restored.rawEthernetReady() ? "true" : "false")
                  << '\n';
#else
        if (restored.npcapRequired() || !restored.npcapAvailable() || !restored.rawEthernetReady()) {
            std::cerr << "Non-Windows Npcap readiness contract failed.\n";
            return 12;
        }
#endif
    }

    {
        QJsonObject legacy;
        legacy.insert(QStringLiteral("schema"), 1);
        legacy.insert(QStringLiteral("workspaceIndex"), 6);
        legacy.insert(QStringLiteral("recentEndpoints"),
                      QJsonArray{endpoint(QStringLiteral("legacy.local"), 102)});
        if (!writeJson(path, legacy)) return 20;

        ProductHardeningController migrated(path);
        if (!migrated.settingsHealthy() || migrated.workspaceIndex() != 3 ||
            migrated.lastHost() != QStringLiteral("legacy.local") ||
            !migrated.recentResources().isEmpty() ||
            !migrated.settingsStatus().contains(QStringLiteral("migrated"), Qt::CaseInsensitive)) {
            std::cerr << "Legacy seven-workspace state migration failed.\n";
            return 21;
        }

        QFile migratedFile(path);
        if (!migratedFile.open(QIODevice::ReadOnly)) return 22;
        const auto migratedDocument = QJsonDocument::fromJson(migratedFile.readAll());
        if (!migratedDocument.isObject() ||
            migratedDocument.object().value(QStringLiteral("schema")).toInt(-1) != 3 ||
            migratedDocument.object().value(QStringLiteral("workspaceIndex")).toInt(-1) != 3 ||
            !migratedDocument.object().value(QStringLiteral("recentResources")).isArray()) {
            std::cerr << "Migrated legacy state was not rewritten as schema 3.\n";
            return 23;
        }
    }

    {
        QJsonObject p1;
        p1.insert(QStringLiteral("schema"), 2);
        p1.insert(QStringLiteral("workspaceIndex"), 1);
        p1.insert(QStringLiteral("recentEndpoints"),
                  QJsonArray{endpoint(QStringLiteral("p1.local"), 102)});
        if (!writeJson(path, p1)) return 24;

        ProductHardeningController migrated(path);
        if (!migrated.settingsHealthy() || migrated.workspaceIndex() != 1 ||
            migrated.lastHost() != QStringLiteral("p1.local") ||
            !migrated.recentResources().isEmpty() ||
            !migrated.settingsStatus().contains(QStringLiteral("migrated"), Qt::CaseInsensitive)) {
            std::cerr << "P1 schema-2 state migration failed.\n";
            return 25;
        }

        QFile migratedFile(path);
        if (!migratedFile.open(QIODevice::ReadOnly)) return 26;
        const auto migratedDocument = QJsonDocument::fromJson(migratedFile.readAll());
        if (!migratedDocument.isObject() ||
            migratedDocument.object().value(QStringLiteral("schema")).toInt(-1) != 3 ||
            !migratedDocument.object().value(QStringLiteral("recentResources")).isArray()) {
            std::cerr << "P1 state was not rewritten as schema 3.\n";
            return 27;
        }
    }

    {
        QFile corrupt(path);
        if (!corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate) || corrupt.write("{not-json") < 0 ||
            !corrupt.flush()) {
            std::cerr << "Could not create corrupt product state.\n";
            return 30;
        }
        // QSaveFile replaces the target during commit. Windows correctly refuses
        // that replacement while another writer still owns an open handle to the
        // target, unlike POSIX unlink/rename semantics. Close the deliberate
        // corruption writer before exercising the application's crash recovery so
        // the QA measures recovery from malformed persisted bytes, not an external
        // file-lock held by the test itself.
        corrupt.close();

        ProductHardeningController damaged(path);
        if (damaged.settingsHealthy() || damaged.workspaceIndex() != 0 ||
            !damaged.recentEndpoints().isEmpty() || !damaged.recentResources().isEmpty()) {
            std::cerr << "Corrupt state was not ignored fail-closed.\n";
            return 31;
        }
        damaged.setWorkspaceIndex(3);
        if (!damaged.settingsHealthy() || !damaged.rememberEndpoint(QStringLiteral("recovered.local"), 102) ||
            !damaged.rememberResource(temp.filePath(QStringLiteral("recovered.scd")))) {
            std::cerr << "Crash-safe state recovery failed.\n";
            return 32;
        }
        ProductHardeningController recovered(path);
        if (!recovered.settingsHealthy() || recovered.workspaceIndex() != 3 ||
            recovered.lastHost() != QStringLiteral("recovered.local") ||
            recovered.recentResourcePath(0) != absolutePath(temp.filePath(QStringLiteral("recovered.scd")))) {
            std::cerr << "Recovered product state did not survive restart.\n";
            return 33;
        }
    }

    {
        QJsonObject unsupported;
        unsupported.insert(QStringLiteral("schema"), 99);
        unsupported.insert(QStringLiteral("workspaceIndex"), 1);
        unsupported.insert(QStringLiteral("recentEndpoints"), QJsonArray{});
        unsupported.insert(QStringLiteral("recentResources"), QJsonArray{});
        if (!writeJson(path, unsupported)) return 40;
        ProductHardeningController rejected(path);
        if (rejected.settingsHealthy() || rejected.workspaceIndex() != 0 ||
            !rejected.recentEndpoints().isEmpty() || !rejected.recentResources().isEmpty()) {
            std::cerr << "Unsupported schema was not rejected.\n";
            return 41;
        }
    }

    {
        QJsonArray tooMany;
        for (int index = 0; index < 9; ++index) {
            tooMany.append(endpoint(QStringLiteral("198.51.100.%1").arg(index + 1), 102));
        }
        QJsonObject unbounded;
        unbounded.insert(QStringLiteral("schema"), 3);
        unbounded.insert(QStringLiteral("workspaceIndex"), 0);
        unbounded.insert(QStringLiteral("recentEndpoints"), tooMany);
        unbounded.insert(QStringLiteral("recentResources"), QJsonArray{});
        if (!writeJson(path, unbounded)) return 42;
        ProductHardeningController rejected(path);
        if (rejected.settingsHealthy() || !rejected.recentEndpoints().isEmpty() ||
            !rejected.recentResources().isEmpty()) {
            std::cerr << "Unbounded persisted endpoint recents were not rejected.\n";
            return 43;
        }
    }

    {
        QJsonArray tooManyResources;
        for (int index = 0; index < 9; ++index) {
            tooManyResources.append(absolutePath(temp.filePath(QStringLiteral("oversize-%1.scd").arg(index))));
        }
        QJsonObject unbounded;
        unbounded.insert(QStringLiteral("schema"), 3);
        unbounded.insert(QStringLiteral("workspaceIndex"), 0);
        unbounded.insert(QStringLiteral("recentEndpoints"), QJsonArray{});
        unbounded.insert(QStringLiteral("recentResources"), tooManyResources);
        if (!writeJson(path, unbounded)) return 44;
        ProductHardeningController rejected(path);
        if (rejected.settingsHealthy() || !rejected.recentEndpoints().isEmpty() ||
            !rejected.recentResources().isEmpty()) {
            std::cerr << "Unbounded persisted resource recents were not rejected.\n";
            return 45;
        }
    }

    std::cout << "PRODUCT_HARDENING_PASS"
              << " state=atomic"
              << " workspace_restore=2"
              << " endpoint_recent=8"
              << " resource_recent=8"
              << " endpoint_dedupe=pass"
              << " resource_dedupe=pass"
              << " bounded_recent=8"
              << " legacy_workspace_migration=pass"
              << " p1_schema_migration=pass"
              << " crash_recovery=pass"
              << " auto_reconnect=false"
              << " npcap_policy=explicit\n";
    std::cout << "PRODUCT_HARDENING_NEGATIVE_PASS"
              << " corrupt_state=ignored"
              << " unsupported_schema=rejected"
              << " invalid_workspace=rejected"
              << " invalid_endpoint=rejected"
              << " invalid_resource=rejected"
              << " unbounded_endpoint_recent=rejected"
              << " unbounded_resource_recent=rejected\n";
    return 0;
}
