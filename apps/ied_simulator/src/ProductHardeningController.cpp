// SPDX-License-Identifier: GPL-3.0-or-later
#include "ProductHardeningController.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLibrary>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace {
constexpr qint64 maximumStateBytes = 64 * 1024;
}

ProductHardeningController::ProductHardeningController(QObject* parent)
    : ProductHardeningController(defaultStatePath(), parent) {}

ProductHardeningController::ProductHardeningController(QString statePath, QObject* parent)
    : QObject(parent), statePath_(std::move(statePath)) {
    (void)loadState();
    refreshRuntimeReadiness();
}

QString ProductHardeningController::defaultStatePath() {
    auto directory = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (directory.isEmpty()) {
        directory = QDir::home().filePath(QStringLiteral(".arstack61850"));
    }
    return QDir(directory).filePath(QStringLiteral("product-state-v1.json"));
}

QString ProductHardeningController::normalizedHost(const QString& value) {
    auto host = value.trimmed();
    if (host.startsWith(QLatin1Char('[')) && host.endsWith(QLatin1Char(']')) && host.size() > 2) {
        host = host.mid(1, host.size() - 2);
    }
    if (host.isEmpty() || host.size() > 255) return {};
    for (const auto character : host) {
        if (character.isSpace() || character.category() == QChar::Other_Control) return {};
    }
    return host;
}

QString ProductHardeningController::endpointLabel(const Endpoint& endpoint) {
    const auto host = endpoint.host.contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]").arg(endpoint.host)
        : endpoint.host;
    return QStringLiteral("%1:%2").arg(host).arg(endpoint.port);
}

int ProductHardeningController::migrateLegacyWorkspaceIndex(const int value) noexcept {
    switch (value) {
    case 0: // IED Connection
    case 1: // Reports
    case 2: // Files
    case 3: // Settings
        return 1; // IED Browser
    case 4:
        return 0; // File / SCL resource workspace
    case 5:
        return 2; // IED Simulator
    case 6:
        return 3; // Sniffer
    default:
        return 0;
    }
}

QStringList ProductHardeningController::recentEndpoints() const {
    QStringList result;
    result.reserve(static_cast<qsizetype>(recent_.size()));
    for (const auto& endpoint : recent_) result.push_back(endpointLabel(endpoint));
    return result;
}

QString ProductHardeningController::lastHost() const {
    return recent_.empty() ? QString{} : recent_.front().host;
}

int ProductHardeningController::lastPort() const noexcept {
    return recent_.empty() ? 102 : recent_.front().port;
}

bool ProductHardeningController::npcapRequired() const noexcept {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

void ProductHardeningController::resetDefaults() {
    workspaceIndex_ = 0;
    recent_.clear();
}

void ProductHardeningController::setStateFault(const QString& message) {
    settingsHealthy_ = false;
    settingsStatus_ = message;
}

bool ProductHardeningController::loadState() {
    resetDefaults();
    QFile file(statePath_);
    if (!file.exists()) {
        settingsHealthy_ = true;
        settingsStatus_ = QStringLiteral("No persisted state yet; safe defaults active.");
        emit stateChanged();
        return true;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        setStateFault(QStringLiteral("Persisted state ignored: %1").arg(file.errorString()));
        emit stateChanged();
        return false;
    }
    if (file.size() < 0 || file.size() > maximumStateBytes) {
        setStateFault(QStringLiteral("Persisted state ignored: file exceeds 64 KiB bound."));
        emit stateChanged();
        return false;
    }

    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setStateFault(QStringLiteral("Persisted state ignored: malformed JSON."));
        emit stateChanged();
        return false;
    }

    const auto object = document.object();
    const auto schema = object.value(QStringLiteral("schema")).toInt(-1);
    if (schema != stateSchemaVersion && schema != legacyStateSchemaVersion) {
        setStateFault(QStringLiteral("Persisted state ignored: unsupported schema."));
        emit stateChanged();
        return false;
    }

    const auto storedWorkspace = object.value(QStringLiteral("workspaceIndex")).toInt(-1);
    if (schema == legacyStateSchemaVersion) {
        if (storedWorkspace < minimumWorkspaceIndex || storedWorkspace > legacyMaximumWorkspaceIndex) {
            setStateFault(QStringLiteral("Persisted state ignored: legacy workspace index is outside bounds."));
            emit stateChanged();
            return false;
        }
    } else if (storedWorkspace < minimumWorkspaceIndex || storedWorkspace > maximumWorkspaceIndex) {
        setStateFault(QStringLiteral("Persisted state ignored: workspace index is outside bounds."));
        emit stateChanged();
        return false;
    }
    const auto workspace = schema == legacyStateSchemaVersion
        ? migrateLegacyWorkspaceIndex(storedWorkspace)
        : storedWorkspace;

    const auto recent = object.value(QStringLiteral("recentEndpoints"));
    if (!recent.isArray() || recent.toArray().size() > maximumRecentEndpoints) {
        setStateFault(QStringLiteral("Persisted state ignored: recent endpoint list is invalid or unbounded."));
        emit stateChanged();
        return false;
    }

    std::vector<Endpoint> loaded;
    loaded.reserve(static_cast<std::size_t>(recent.toArray().size()));
    for (const auto& value : recent.toArray()) {
        if (!value.isObject()) {
            setStateFault(QStringLiteral("Persisted state ignored: malformed recent endpoint."));
            emit stateChanged();
            return false;
        }
        const auto endpointObject = value.toObject();
        const auto host = normalizedHost(endpointObject.value(QStringLiteral("host")).toString());
        const auto port = endpointObject.value(QStringLiteral("port")).toInt(0);
        if (host.isEmpty() || port < 1 || port > 65'535) {
            setStateFault(QStringLiteral("Persisted state ignored: invalid recent endpoint."));
            emit stateChanged();
            return false;
        }
        loaded.push_back({host, port});
    }

    workspaceIndex_ = workspace;
    recent_ = std::move(loaded);
    settingsHealthy_ = true;
    settingsStatus_ = schema == legacyStateSchemaVersion
        ? QStringLiteral("Legacy product state migrated to the four-workspace shell.")
        : QStringLiteral("Persisted product state restored safely.");

    if (schema == legacyStateSchemaVersion) {
        if (!persistState()) {
            emit stateChanged();
            return false;
        }
        settingsStatus_ = QStringLiteral("Legacy product state migrated to the four-workspace shell.");
    }

    emit stateChanged();
    return true;
}

bool ProductHardeningController::persistState() {
    const QFileInfo info(statePath_);
    if (!QDir().mkpath(info.absolutePath())) {
        setStateFault(QStringLiteral("Unable to create product-state directory."));
        return false;
    }

    QJsonArray recent;
    for (const auto& endpoint : recent_) {
        QJsonObject item;
        item.insert(QStringLiteral("host"), endpoint.host);
        item.insert(QStringLiteral("port"), endpoint.port);
        recent.append(item);
    }

    QJsonObject object;
    object.insert(QStringLiteral("schema"), stateSchemaVersion);
    object.insert(QStringLiteral("workspaceIndex"), workspaceIndex_);
    object.insert(QStringLiteral("recentEndpoints"), recent);
    const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);

    QSaveFile output(statePath_);
    if (!output.open(QIODevice::WriteOnly)) {
        setStateFault(QStringLiteral("Atomic state write failed to open: %1").arg(output.errorString()));
        return false;
    }
    if (output.write(payload) != payload.size()) {
        output.cancelWriting();
        setStateFault(QStringLiteral("Atomic state write was incomplete: %1").arg(output.errorString()));
        return false;
    }
    if (!output.commit()) {
        setStateFault(QStringLiteral("Atomic state commit failed: %1").arg(output.errorString()));
        return false;
    }

    settingsHealthy_ = true;
    settingsStatus_ = QStringLiteral("Product state saved atomically.");
    return true;
}

void ProductHardeningController::setWorkspaceIndex(const int value) {
    if (value < minimumWorkspaceIndex || value > maximumWorkspaceIndex) {
        settingsStatus_ = QStringLiteral("Rejected invalid workspace index.");
        emit stateChanged();
        return;
    }
    if (workspaceIndex_ == value) return;
    workspaceIndex_ = value;
    (void)persistState();
    emit stateChanged();
}

bool ProductHardeningController::rememberEndpoint(const QString& hostValue, const int port) {
    const auto host = normalizedHost(hostValue);
    if (host.isEmpty() || port < 1 || port > 65'535) {
        settingsStatus_ = QStringLiteral("Rejected invalid MMS endpoint.");
        emit stateChanged();
        return false;
    }

    const auto duplicate = std::find_if(recent_.begin(), recent_.end(), [&](const Endpoint& endpoint) {
        return endpoint.port == port && endpoint.host.compare(host, Qt::CaseInsensitive) == 0;
    });
    if (duplicate != recent_.end()) recent_.erase(duplicate);
    recent_.insert(recent_.begin(), {host, port});
    if (recent_.size() > static_cast<std::size_t>(maximumRecentEndpoints)) {
        recent_.resize(static_cast<std::size_t>(maximumRecentEndpoints));
    }
    const auto persisted = persistState();
    emit stateChanged();
    return persisted;
}

void ProductHardeningController::clearRecentEndpoints() {
    if (recent_.empty()) return;
    recent_.clear();
    (void)persistState();
    emit stateChanged();
}

QString ProductHardeningController::recentHost(const int index) const {
    if (index < 0 || index >= static_cast<int>(recent_.size())) return {};
    return recent_[static_cast<std::size_t>(index)].host;
}

int ProductHardeningController::recentPort(const int index) const {
    if (index < 0 || index >= static_cast<int>(recent_.size())) return 0;
    return recent_[static_cast<std::size_t>(index)].port;
}

bool ProductHardeningController::reload() {
    return loadState();
}

void ProductHardeningController::refreshRuntimeReadiness() {
#ifdef Q_OS_WIN
    QLibrary wpcap(QStringLiteral("wpcap"));
    QLibrary packet(QStringLiteral("Packet"));
    const bool wpcapReady = wpcap.load();
    const bool packetReady = packet.load();
    if (wpcapReady) wpcap.unload();
    if (packetReady) packet.unload();
    npcapAvailable_ = wpcapReady && packetReady;
    npcapStatus_ = npcapAvailable_
        ? QStringLiteral("Npcap runtime detected; raw Ethernet workspaces are available.")
        : QStringLiteral("Npcap runtime not detected. Install Npcap before using Windows GOOSE monitor/publisher raw Ethernet features.");
#else
    npcapAvailable_ = true;
    npcapStatus_ = QStringLiteral("Npcap is not required on this platform.");
#endif
    emit runtimeReadinessChanged();
}
