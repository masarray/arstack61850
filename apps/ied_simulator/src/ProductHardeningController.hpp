// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <vector>

class ProductHardeningController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int workspaceIndex READ workspaceIndex WRITE setWorkspaceIndex NOTIFY stateChanged)
    Q_PROPERTY(QStringList recentEndpoints READ recentEndpoints NOTIFY stateChanged)
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

    [[nodiscard]] QStringList recentEndpoints() const;
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

    static constexpr int legacyStateSchemaVersion = 1;
    static constexpr int stateSchemaVersion = 2;
    static constexpr int maximumRecentEndpoints = 8;
    static constexpr int minimumWorkspaceIndex = 0;
    static constexpr int maximumWorkspaceIndex = 3;
    static constexpr int legacyMaximumWorkspaceIndex = 6;

    [[nodiscard]] static QString defaultStatePath();
    [[nodiscard]] static QString normalizedHost(const QString& value);
    [[nodiscard]] static QString endpointLabel(const Endpoint& endpoint);
    [[nodiscard]] static int migrateLegacyWorkspaceIndex(int value) noexcept;
    [[nodiscard]] bool loadState();
    [[nodiscard]] bool persistState();
    void resetDefaults();
    void setStateFault(const QString& message);

    QString statePath_;
    int workspaceIndex_{};
    std::vector<Endpoint> recent_;
    bool settingsHealthy_{true};
    QString settingsStatus_{QStringLiteral("Product state ready.")};
    bool npcapAvailable_{};
    QString npcapStatus_;
};
