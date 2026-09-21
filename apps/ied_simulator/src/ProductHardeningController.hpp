// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

class ProductHardeningController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int workspaceIndex READ workspaceIndex WRITE setWorkspaceIndex NOTIFY stateChanged)
    Q_PROPERTY(int browserNavigationWidth READ browserNavigationWidth WRITE setBrowserNavigationWidth NOTIFY stateChanged)
    Q_PROPERTY(QStringList recentEndpoints READ recentEndpoints NOTIFY stateChanged)
    Q_PROPERTY(QStringList recentResources READ recentResources NOTIFY stateChanged)
    Q_PROPERTY(QStringList recentDiscoveredIeds READ recentDiscoveredIeds NOTIFY stateChanged)
    Q_PROPERTY(QString lastHost READ lastHost NOTIFY stateChanged)
    Q_PROPERTY(int lastPort READ lastPort NOTIFY stateChanged)
    Q_PROPERTY(QString statePath READ statePath CONSTANT)
    Q_PROPERTY(bool settingsHealthy READ settingsHealthy NOTIFY stateChanged)
    Q_PROPERTY(QString settingsStatus READ settingsStatus NOTIFY stateChanged)
    Q_PROPERTY(bool automaticReconnectOnStartup READ automaticReconnectOnStartup CONSTANT)
    Q_PROPERTY(bool npcapRequired READ npcapRequired CONSTANT)
    Q_PROPERTY(bool npcapAvailable READ npcapAvailable NOTIFY runtimeReadinessChanged)
    Q_PROPERTY(bool rawEthernetReady READ rawEthernetReady NOTIFY runtimeReadinessChanged)
    Q_PROPERTY(QString npcapStatus READ npcapStatus NOTIFY runtimeReadinessChanged)

public:
    explicit ProductHardeningController(QObject* parent = nullptr);
    explicit ProductHardeningController(QString statePath, QObject* parent = nullptr);

    [[nodiscard]] int workspaceIndex() const noexcept { return workspaceIndex_; }
    void setWorkspaceIndex(int value);
    [[nodiscard]] int browserNavigationWidth() const noexcept { return browserNavigationWidth_; }
    void setBrowserNavigationWidth(int value);

    [[nodiscard]] QStringList recentEndpoints() const;
    [[nodiscard]] QStringList recentResources() const;
    [[nodiscard]] QStringList recentDiscoveredIeds() const;
    [[nodiscard]] QString lastHost() const;
    [[nodiscard]] int lastPort() const noexcept;
    [[nodiscard]] QString statePath() const { return statePath_; }
    [[nodiscard]] bool settingsHealthy() const noexcept { return settingsHealthy_; }
    [[nodiscard]] QString settingsStatus() const { return settingsStatus_; }
    [[nodiscard]] bool automaticReconnectOnStartup() const noexcept { return false; }
    [[nodiscard]] bool npcapRequired() const noexcept;
    [[nodiscard]] bool npcapAvailable() const noexcept { return npcapAvailable_; }
    [[nodiscard]] bool rawEthernetReady() const noexcept { return !npcapRequired() || npcapAvailable_; }
    [[nodiscard]] QString npcapStatus() const { return npcapStatus_; }

    Q_INVOKABLE bool rememberEndpoint(const QString& host, int port);
    Q_INVOKABLE void clearRecentEndpoints();
    Q_INVOKABLE QString recentHost(int index) const;
    Q_INVOKABLE int recentPort(int index) const;

    Q_INVOKABLE bool rememberDiscoveredIed(const QString& iedName, const QString& host, int port);
    Q_INVOKABLE void clearRecentDiscoveredIeds();
    Q_INVOKABLE QString recentDiscoveredIedName(int index) const;
    Q_INVOKABLE QString recentDiscoveredIedHost(int index) const;
    Q_INVOKABLE int recentDiscoveredIedPort(int index) const;

    Q_INVOKABLE bool rememberResource(const QString& path);
    Q_INVOKABLE void clearRecentResources();
    Q_INVOKABLE QString recentResourcePath(int index) const;
    Q_INVOKABLE QUrl recentResourceUrl(int index) const;
    Q_INVOKABLE bool recentResourceExists(int index) const;

    Q_INVOKABLE bool reload();
    Q_INVOKABLE void refreshRuntimeReadiness();

signals:
    void stateChanged();
    void runtimeReadinessChanged();

private:
    struct Endpoint final {
        QString host;
        int port{102};
    };

    struct DiscoveredIed final {
        QString iedName;
        Endpoint endpoint;
    };

    static constexpr int legacyStateSchemaVersion = 1;
    static constexpr int previousStateSchemaVersion = 2;
    static constexpr int fileHomeStateSchemaVersion = 3;
    static constexpr int browserLayoutStateSchemaVersion = 4;
    static constexpr int stateSchemaVersion = 5;
    static constexpr int maximumRecentEndpoints = 8;
    static constexpr int maximumRecentResources = 8;
    static constexpr int maximumRecentDiscoveredIeds = 8;
    static constexpr int maximumResourcePathCharacters = 4096;
    static constexpr int maximumIedNameCharacters = 255;
    static constexpr int minimumWorkspaceIndex = 0;
    static constexpr int maximumWorkspaceIndex = 3;
    static constexpr int legacyMaximumWorkspaceIndex = 6;
    static constexpr int defaultBrowserNavigationWidth = 320;
    static constexpr int minimumBrowserNavigationWidth = 240;
    static constexpr int maximumBrowserNavigationWidth = 520;

    [[nodiscard]] static QString defaultStatePath();
    [[nodiscard]] static QString normalizedHost(const QString& value);
    [[nodiscard]] static QString normalizedIedName(const QString& value);
    [[nodiscard]] static QString normalizedResourcePath(const QString& value);
    [[nodiscard]] static QString endpointLabel(const Endpoint& endpoint);
    [[nodiscard]] static QString discoveredIedLabel(const DiscoveredIed& ied);
    [[nodiscard]] static int migrateLegacyWorkspaceIndex(int value) noexcept;
    [[nodiscard]] bool loadState();
    [[nodiscard]] bool persistState();
    void resetDefaults();
    void setStateFault(const QString& message);

    QString statePath_;
    int workspaceIndex_{};
    int browserNavigationWidth_{defaultBrowserNavigationWidth};
    std::vector<Endpoint> recent_;
    std::vector<QString> recentResources_;
    std::vector<DiscoveredIed> recentDiscoveredIeds_;
    bool settingsHealthy_{true};
    QString settingsStatus_{QStringLiteral("Product state ready.")};
    bool npcapAvailable_{};
    QString npcapStatus_;
};
