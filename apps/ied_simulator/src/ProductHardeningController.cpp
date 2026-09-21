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
#include <QUrl>

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

QString ProductHardeningController::normalizedIedName(const QString& value) {
    const auto name = value.trimmed();
    if (name.isEmpty() || name.size() > maximumIedNameCharacters) return {};
    for (const auto character : name) {
        if (character.category() == QChar::Other_Control) return {};
    }
    return name;
}

QString ProductHardeningController::normalizedResourcePath(const QString& value) {
    const auto candidate = value.trimmed();
    if (candidate.isEmpty() || candidate.size() > maximumResourcePathCharacters) return {};

    QString path;
    const QUrl url(candidate);
    if (url.isValid() && url.isLocalFile()) {
        path = url.toLocalFile();
    } else {
        if (!QDir::isAbsolutePath(candidate)) return {};
        path = candidate;
    }

    for (const auto character : path) {
        if (character.category() == QChar::Other_Control) return {};
    }

    path = QDir::cleanPath(path);
    if (path.isEmpty() || path.size() > maximumResourcePathCharacters || !QDir::isAbsolutePath(path)) return {};
    return QFileInfo(path).absoluteFilePath();
}

QString ProductHardeningController::endpointLabel(const Endpoint& endpoint) {
    const auto host = endpoint.host.contains(QLatin1Char(':'))
        ? QStringLiteral("[%1]").arg(endpoint.host)
        : endpoint.host;
    return QStringLiteral("%1:%2").arg(host).arg(endpoint.port);
}

QString ProductHardeningController::discoveredIedLabel(const DiscoveredIed& ied) {
    const auto endpoint = endpointLabel(ied.endpoint);
    return ied.iedName.isEmpty()
        ? endpoint
        : QStringLiteral("%1 (%2)").arg(ied.iedName, endpoint);
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

QStringList ProductHardeningController::recentResources() const {
    QStringList result;
    result.reserve(static_cast<qsizetype>(recentResources_.size()));
    for (const auto& path : recentResources_) result.push_back(path);
    return result;
}

QStringList ProductHardeningController::recentDiscoveredIeds() const {
    QStringList result;
    result.reserve(static_cast<qsizetype>(recentDiscoveredIeds_.size()));
    for (const auto& ied : recentDiscoveredIeds_) result.push_back(discoveredIedLabel(ied));
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
    browserNavigationWidth_ = defaultBrowserNavigationWidth;
    recent_.clear();
    recentResources_.clear();
    recentDiscoveredIeds_.clear();
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
    if (schema != stateSchemaVersion &&
        schema != browserLayoutStateSchemaVersion &&
        schema != fileHomeStateSchemaVersion &&
        schema != previousStateSchemaVersion &&
        schema != legacyStateSchemaVersion) {
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

    std::vector<QString> loadedResources;
    if (schema == stateSchemaVersion ||
        schema == browserLayoutStateSchemaVersion ||
        schema == fileHomeStateSchemaVersion) {
        const auto resources = object.value(QStringLiteral("recentResources"));
        if (!resources.isArray() || resources.toArray().size() > maximumRecentResources) {
            setStateFault(QStringLiteral("Persisted state ignored: recent resource list is invalid or unbounded."));
            emit stateChanged();
            return false;
        }
        loadedResources.reserve(static_cast<std::size_t>(resources.toArray().size()));
        for (const auto& value : resources.toArray()) {
            if (!value.isString()) {
                setStateFault(QStringLiteral("Persisted state ignored: malformed recent engineering resource."));
                emit stateChanged();
                return false;
            }
            const auto path = normalizedResourcePath(value.toString());
            if (path.isEmpty()) {
                setStateFault(QStringLiteral("Persisted state ignored: invalid recent engineering resource."));
                emit stateChanged();
                return false;
            }
            loadedResources.push_back(path);
        }
    }

    std::vector<DiscoveredIed> loadedDiscoveredIeds;
    if (schema == stateSchemaVersion) {
        const auto discovered = object.value(QStringLiteral("recentDiscoveredIeds"));
        if (!discovered.isArray() || discovered.toArray().size() > maximumRecentDiscoveredIeds) {
            setStateFault(QStringLiteral("Persisted state ignored: discovered IED history is invalid or unbounded."));
            emit stateChanged();
            return false;
        }
        loadedDiscoveredIeds.reserve(static_cast<std::size_t>(discovered.toArray().size()));
        for (const auto& value : discovered.toArray()) {
            if (!value.isObject()) {
                setStateFault(QStringLiteral("Persisted state ignored: malformed discovered IED history."));
                emit stateChanged();
                return false;
            }
            const auto item = value.toObject();
            const auto rawName = item.value(QStringLiteral("iedName")).toString().trimmed();
            const auto iedName = normalizedIedName(rawName);
            const auto host = normalizedHost(item.value(QStringLiteral("host")).toString());
            const auto port = item.value(QStringLiteral("port")).toInt(0);
            if ((!rawName.isEmpty() && iedName.isEmpty()) ||
                host.isEmpty() || port < 1 || port > 65'535) {
                setStateFault(QStringLiteral("Persisted state ignored: invalid discovered IED history entry."));
                emit stateChanged();
                return false;
            }
            loadedDiscoveredIeds.push_back({iedName, {host, port}});
        }
    } else {
        loadedDiscoveredIeds.reserve(loaded.size());
        for (const auto& endpoint : loaded) {
            loadedDiscoveredIeds.push_back({{}, endpoint});
        }
    }

    int browserNavigationWidth = defaultBrowserNavigationWidth;
    if (schema == stateSchemaVersion || schema == browserLayoutStateSchemaVersion) {
        browserNavigationWidth = object.value(QStringLiteral("browserNavigationWidth")).toInt(-1);
        if (browserNavigationWidth < minimumBrowserNavigationWidth ||
            browserNavigationWidth > maximumBrowserNavigationWidth) {
            setStateFault(QStringLiteral("Persisted state ignored: Browser navigation width is outside bounds."));
            emit stateChanged();
            return false;
        }
    }

    workspaceIndex_ = workspace;
    browserNavigationWidth_ = browserNavigationWidth;
    recent_ = std::move(loaded);
    recentResources_ = std::move(loadedResources);
    recentDiscoveredIeds_ = std::move(loadedDiscoveredIeds);
    settingsHealthy_ = true;

    if (schema == legacyStateSchemaVersion) {
        settingsStatus_ = QStringLiteral("Legacy product state migrated to the four-workspace File/Home schema.");
    } else if (schema == previousStateSchemaVersion) {
        settingsStatus_ = QStringLiteral("Product state migrated to File/Home engineering-resource history.");
    } else if (schema == fileHomeStateSchemaVersion) {
        settingsStatus_ = QStringLiteral("Product state migrated to persistent Browser layout state.");
    } else if (schema == browserLayoutStateSchemaVersion) {
        settingsStatus_ = QStringLiteral("Product state migrated to identity-aware discovered IED history.");
    } else {
        settingsStatus_ = QStringLiteral("Persisted product state restored safely.");
    }

    if (schema != stateSchemaVersion) {
        if (!persistState()) {
            emit stateChanged();
            return false;
        }
        settingsStatus_ = schema == legacyStateSchemaVersion
            ? QStringLiteral("Legacy product state migrated to the four-workspace File/Home schema.")
            : schema == previousStateSchemaVersion
                ? QStringLiteral("Product state migrated to File/Home engineering-resource history.")
                : schema == fileHomeStateSchemaVersion
                    ? QStringLiteral("Product state migrated to persistent Browser layout state.")
                    : QStringLiteral("Product state migrated to identity-aware discovered IED history.");
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

    QJsonArray resources;
    for (const auto& path : recentResources_) resources.append(path);

    QJsonArray discoveredIeds;
    for (const auto& ied : recentDiscoveredIeds_) {
        QJsonObject item;
        item.insert(QStringLiteral("iedName"), ied.iedName);
        item.insert(QStringLiteral("host"), ied.endpoint.host);
        item.insert(QStringLiteral("port"), ied.endpoint.port);
        discoveredIeds.append(item);
    }

    QJsonObject object;
    object.insert(QStringLiteral("schema"), stateSchemaVersion);
    object.insert(QStringLiteral("workspaceIndex"), workspaceIndex_);
    object.insert(QStringLiteral("browserNavigationWidth"), browserNavigationWidth_);
    object.insert(QStringLiteral("recentEndpoints"), recent);
    object.insert(QStringLiteral("recentResources"), resources);
    object.insert(QStringLiteral("recentDiscoveredIeds"), discoveredIeds);
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

void ProductHardeningController::setBrowserNavigationWidth(const int value) {
    if (value < minimumBrowserNavigationWidth || value > maximumBrowserNavigationWidth) {
        settingsStatus_ = QStringLiteral("Rejected invalid Browser navigation width.");
        emit stateChanged();
        return;
    }
    if (browserNavigationWidth_ == value) return;
    browserNavigationWidth_ = value;
    (void)persistState();
    emit stateChanged();
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

bool ProductHardeningController::rememberDiscoveredIed(
    const QString& iedNameValue,
    const QString& hostValue,
    const int port) {
    const auto rawName = iedNameValue.trimmed();
    const auto iedName = normalizedIedName(rawName);
    const auto host = normalizedHost(hostValue);
    if ((!rawName.isEmpty() && iedName.isEmpty()) ||
        host.isEmpty() || port < 1 || port > 65'535) {
        settingsStatus_ = QStringLiteral("Rejected invalid discovered IED history entry.");
        emit stateChanged();
        return false;
    }

    const auto duplicate = std::find_if(
        recentDiscoveredIeds_.begin(),
        recentDiscoveredIeds_.end(),
        [&](const DiscoveredIed& ied) {
            return ied.endpoint.port == port &&
                   ied.endpoint.host.compare(host, Qt::CaseInsensitive) == 0;
        });
    if (duplicate != recentDiscoveredIeds_.end()) recentDiscoveredIeds_.erase(duplicate);
    recentDiscoveredIeds_.insert(recentDiscoveredIeds_.begin(), {iedName, {host, port}});
    if (recentDiscoveredIeds_.size() > static_cast<std::size_t>(maximumRecentDiscoveredIeds)) {
        recentDiscoveredIeds_.resize(static_cast<std::size_t>(maximumRecentDiscoveredIeds));
    }
    const auto persisted = persistState();
    emit stateChanged();
    return persisted;
}

void ProductHardeningController::clearRecentDiscoveredIeds() {
    if (recentDiscoveredIeds_.empty()) return;
    recentDiscoveredIeds_.clear();
    (void)persistState();
    emit stateChanged();
}

QString ProductHardeningController::recentDiscoveredIedName(const int index) const {
    if (index < 0 || index >= static_cast<int>(recentDiscoveredIeds_.size())) return {};
    return recentDiscoveredIeds_[static_cast<std::size_t>(index)].iedName;
}

QString ProductHardeningController::recentDiscoveredIedHost(const int index) const {
    if (index < 0 || index >= static_cast<int>(recentDiscoveredIeds_.size())) return {};
    return recentDiscoveredIeds_[static_cast<std::size_t>(index)].endpoint.host;
}

int ProductHardeningController::recentDiscoveredIedPort(const int index) const {
    if (index < 0 || index >= static_cast<int>(recentDiscoveredIeds_.size())) return 0;
    return recentDiscoveredIeds_[static_cast<std::size_t>(index)].endpoint.port;
}

bool ProductHardeningController::rememberResource(const QString& pathValue) {
    const auto path = normalizedResourcePath(pathValue);
    if (path.isEmpty()) {
        settingsStatus_ = QStringLiteral("Rejected invalid engineering-resource path.");
        emit stateChanged();
        return false;
    }

#ifdef Q_OS_WIN
    constexpr auto pathCaseSensitivity = Qt::CaseInsensitive;
#else
    constexpr auto pathCaseSensitivity = Qt::CaseSensitive;
#endif

    const auto duplicate = std::find_if(recentResources_.begin(), recentResources_.end(), [&](const QString& existing) {
        return existing.compare(path, pathCaseSensitivity) == 0;
    });
    if (duplicate != recentResources_.end()) recentResources_.erase(duplicate);
    recentResources_.insert(recentResources_.begin(), path);
    if (recentResources_.size() > static_cast<std::size_t>(maximumRecentResources)) {
        recentResources_.resize(static_cast<std::size_t>(maximumRecentResources));
    }

    const auto persisted = persistState();
    emit stateChanged();
    return persisted;
}

void ProductHardeningController::clearRecentResources() {
    if (recentResources_.empty()) return;
    recentResources_.clear();
    (void)persistState();
    emit stateChanged();
}

QString ProductHardeningController::recentResourcePath(const int index) const {
    if (index < 0 || index >= static_cast<int>(recentResources_.size())) return {};
    return recentResources_[static_cast<std::size_t>(index)];
}

QUrl ProductHardeningController::recentResourceUrl(const int index) const {
    const auto path = recentResourcePath(index);
    return path.isEmpty() ? QUrl{} : QUrl::fromLocalFile(path);
}

bool ProductHardeningController::recentResourceExists(const int index) const {
    const auto path = recentResourcePath(index);
    return !path.isEmpty() && QFileInfo::exists(path) && QFileInfo(path).isFile();
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
