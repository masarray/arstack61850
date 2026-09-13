// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "IedActivityModel.hpp"
#include "IedPointStore.hpp"

#include "ariec61850/scl/model.hpp"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QThreadPool>
#include <QUrl>
#include <QVariantList>
#include <QVector>
#include <QtQmlIntegration/qqmlintegration.h>

#include <memory>
#include <optional>
#include <vector>

class IedFleetController : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool imported READ imported NOTIFY modelChanged)
    Q_PROPERTY(bool importing READ importing NOTIFY modelChanged)
    Q_PROPERTY(bool running READ running NOTIFY runtimeChanged)
    Q_PROPERTY(bool starting READ starting NOTIFY runtimeChanged)
    Q_PROPERTY(bool anyRunning READ anyRunning NOTIFY runtimeChanged)
    Q_PROPERTY(int runningCount READ runningCount NOTIFY runtimeChanged)
    Q_PROPERTY(QString sourceName READ sourceName NOTIFY modelChanged)
    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY modelChanged)
    Q_PROPERTY(QString modelStatus READ modelStatus NOTIFY modelChanged)
    Q_PROPERTY(QString fatalError READ fatalError NOTIFY modelChanged)
    Q_PROPERTY(QString listenAddress READ listenAddress WRITE setListenAddress NOTIFY configurationChanged)
    Q_PROPERTY(QStringList availableAddresses READ availableAddresses NOTIFY networkInterfacesChanged)
    Q_PROPERTY(QVariantList networkAddresses READ networkAddresses NOTIFY networkInterfacesChanged)
    Q_PROPERTY(int port READ port WRITE setPort NOTIFY configurationChanged)
    Q_PROPERTY(bool gooseEnabled READ gooseEnabled WRITE setGooseEnabled NOTIFY configurationChanged)
    Q_PROPERTY(bool fileServiceEnabled READ fileServiceEnabled WRITE setFileServiceEnabled NOTIFY configurationChanged)
    Q_PROPERTY(QString fileFolder READ fileFolder WRITE setFileFolder NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList ieds READ ieds NOTIFY modelChanged)
    Q_PROPERTY(int selectedIedIndex READ selectedIedIndex WRITE selectIed NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedIed READ selectedIed NOTIFY selectionChanged)
    Q_PROPERTY(QString endpointConflict READ endpointConflict NOTIFY configurationChanged)
    Q_PROPERTY(QVariantList values READ values NOTIFY valuesChanged)
    Q_PROPERTY(int selectedValueIndex READ selectedValueIndex WRITE selectValue NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedValue READ selectedValue NOTIFY selectionChanged)
    Q_PROPERTY(QVariantList activity READ activity NOTIFY activityChanged)
    Q_PROPERTY(IedActivityModel* activityModel READ activityModel CONSTANT)
    Q_PROPERTY(int logicalDeviceCount READ logicalDeviceCount NOTIFY modelChanged)
    Q_PROPERTY(int dataObjectCount READ dataObjectCount NOTIFY modelChanged)
    Q_PROPERTY(int dataAttributeCount READ dataAttributeCount NOTIFY modelChanged)
    Q_PROPERTY(int dataSetCount READ dataSetCount NOTIFY modelChanged)
    Q_PROPERTY(int reportCount READ reportCount NOTIFY modelChanged)
    Q_PROPERTY(int gooseCount READ gooseCount NOTIFY modelChanged)
    Q_PROPERTY(qint64 lastImportWorkerMilliseconds READ lastImportWorkerMilliseconds NOTIFY modelChanged)
    Q_PROPERTY(qint64 lastGuiApplyMilliseconds READ lastGuiApplyMilliseconds NOTIFY modelChanged)
    Q_PROPERTY(int preparedPointCount READ preparedPointCount NOTIFY modelChanged)
    Q_PROPERTY(int preparedIedCount READ preparedIedCount NOTIFY modelChanged)
    Q_PROPERTY(int fleetStartRollbackCount READ fleetStartRollbackCount NOTIFY runtimeChanged)
    Q_PROPERTY(bool clearPending READ clearPending NOTIFY modelChanged)

public:
    enum class RuntimeState {
        ready,
        starting,
        running,
        stopping,
        failed,
    };

    explicit IedFleetController(QObject* parent = nullptr);
    ~IedFleetController() override;

    [[nodiscard]] bool imported() const noexcept;
    [[nodiscard]] bool importing() const noexcept;
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool starting() const noexcept;
    [[nodiscard]] bool anyRunning() const noexcept;
    [[nodiscard]] int runningCount() const noexcept;
    [[nodiscard]] QString sourceName() const;
    [[nodiscard]] QString sourcePath() const;
    [[nodiscard]] QString modelStatus() const;
    [[nodiscard]] QString fatalError() const;
    [[nodiscard]] QString listenAddress() const;
    [[nodiscard]] QStringList availableAddresses() const;
    [[nodiscard]] QVariantList networkAddresses() const;
    [[nodiscard]] int port() const noexcept;
    [[nodiscard]] bool gooseEnabled() const noexcept;
    [[nodiscard]] bool fileServiceEnabled() const noexcept;
    [[nodiscard]] QString fileFolder() const;
    [[nodiscard]] QVariantList ieds() const;
    [[nodiscard]] int selectedIedIndex() const noexcept;
    [[nodiscard]] QVariantMap selectedIed() const;
    [[nodiscard]] QString endpointConflict() const;
    [[nodiscard]] QVariantList values() const;

    // C++ model/view fast path. The canonical high-cardinality state is typed;
    // QVariant maps are materialized only for compatibility/QML boundaries.
    [[nodiscard]] int valueCount() const noexcept { return selectedPointIndices_.size(); }
    [[nodiscard]] const IedPointStore::PointRecord* valueRecord(const int sourceIndex) const noexcept {
        if (sourceIndex < 0 || sourceIndex >= selectedPointIndices_.size()) return nullptr;
        return pointStore_.at(selectedPointIndices_.at(sourceIndex));
    }
    [[nodiscard]] bool hasPreparedValueIndex() const noexcept {
        return preparedValueIndexIed_ == selectedIedIndex_ &&
            preparedPointCount_ == selectedPointIndices_.size();
    }
    [[nodiscard]] const QVariantList& navigationIndexView() const noexcept {
        return navigationIndex_;
    }
    [[nodiscard]] const QVector<int>& valueScopeIndices(
        const QString& logicalDevice,
        const QString& logicalNode) const noexcept {
        static const QVector<int> empty;
        if (!hasPreparedValueIndex()) return empty;
        const auto found = valueScopeIndex_.constFind(
            logicalDevice + QLatin1Char('\x1f') + logicalNode);
        return found == valueScopeIndex_.cend() ? empty : found.value();
    }
    [[nodiscard]] int selectedValueIndex() const noexcept;
    [[nodiscard]] QVariantMap selectedValue() const;
    [[nodiscard]] QVariantList activity() const;
    [[nodiscard]] IedActivityModel* activityModel() noexcept { return &activity_; }
    [[nodiscard]] const IedActivityModel* activityModel() const noexcept { return &activity_; }
    [[nodiscard]] int logicalDeviceCount() const noexcept;
    [[nodiscard]] int dataObjectCount() const noexcept;
    [[nodiscard]] int dataAttributeCount() const noexcept;
    [[nodiscard]] int dataSetCount() const noexcept;
    [[nodiscard]] int reportCount() const noexcept;
    [[nodiscard]] int gooseCount() const noexcept;
    [[nodiscard]] qint64 lastImportWorkerMilliseconds() const noexcept {
        return lastImportWorkerMilliseconds_;
    }
    [[nodiscard]] qint64 lastGuiApplyMilliseconds() const noexcept {
        return lastGuiApplyMilliseconds_;
    }
    [[nodiscard]] int preparedPointCount() const noexcept { return preparedPointCount_; }
    [[nodiscard]] int preparedIedCount() const noexcept {
        return static_cast<int>(preparedIeds_.size());
    }
    [[nodiscard]] int fleetStartRollbackCount() const noexcept { return fleetStartRollbackCount_; }
    [[nodiscard]] bool clearPending() const noexcept { return clearPending_; }

    void setListenAddress(const QString& value);
    void setPort(int value);
    void setGooseEnabled(bool value);
    void setFileServiceEnabled(bool value);
    void setFileFolder(const QString& value);

    Q_INVOKABLE bool loadFile(const QUrl& fileUrl);
    Q_INVOKABLE bool loadFileAsync(const QUrl& fileUrl);
    Q_INVOKABLE bool addFile(const QUrl& fileUrl);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectIed(int index);
    Q_INVOKABLE void selectValue(int index);

    // Compatibility entry points operate on the currently selected IED.
    Q_INVOKABLE bool startSimulation();
    Q_INVOKABLE void stopSimulation();

    // Fleet operations keep each IED on an independent local address/port.
    Q_INVOKABLE bool startIed(int index);
    Q_INVOKABLE void stopIed(int index);
    Q_INVOKABLE int startAllSimulations();
    Q_INVOKABLE void stopAllSimulations();
    Q_INVOKABLE bool configureIedEndpoint(int index, const QString& address, int port);
    Q_INVOKABLE void setIedEnabled(int index, bool enabled);
    Q_INVOKABLE bool autoAssignIedAddresses();
    Q_INVOKABLE void refreshNetworkInterfaces();

    Q_INVOKABLE bool applySelectedValue(
        const QString& value,
        const QString& quality,
        const QString& origin);
    Q_INVOKABLE bool undoLastChange();
    Q_INVOKABLE void clearActivity();
    Q_INVOKABLE void copyDiagnostics();
    Q_INVOKABLE QString diagnosticsText() const;
    Q_INVOKABLE int qaBurstValues(int updates, int distinctPoints = 64);
    Q_INVOKABLE int qaBurstLiveValues(int updates);

signals:
    void modelChanged();
    void configurationChanged();
    void runtimeChanged();
    void networkInterfacesChanged();
    void selectionChanged();
    void valuesChanged();
    void activityChanged();

private:
    // Commissioning projections inspect the already parsed SCL document in place.
    // They are UI/application views only and never own a second parser or protocol state.
    friend class IedCommissioningModel;

    struct LoadedDocument final {
        QString path;
        ar::iec61850::scl::SclDocument document;
    };

    struct PendingAsyncImport final {
        QString path;
        quint64 generation{};
    };

    struct PreparedIedProjection final {
        QVector<int> pointIndices;
        QVariantList navigationIndex;
        QHash<QString, QVector<int>> valueScopeIndex;
    };

    struct AsyncImportResult final {
        QString path;
        quint64 generation{};
        std::optional<ar::iec61850::scl::SclDocument> document;
        QVariantList ieds;
        IedPointStore pointStore;
        QVector<int> selectedPointIndices;
        QVariantList navigationIndex;
        QHash<QString, QVector<int>> valueScopeIndex;
        QVector<PreparedIedProjection> preparedIeds;
        QString error;
        qint64 parserMilliseconds{};
        qint64 preparationMilliseconds{};
        qint64 elapsedMilliseconds{};
        int logicalDeviceCount{};
        int dataObjectCount{};
        int dataAttributeCount{};
        int dataSetCount{};
        int reportCount{};
        int gooseCount{};
        int preparedPointCount{};
        int preparedIedCount{};
    };

    struct PendingLiveUpdate final {
        QString key;
        QString domain;
        QString item;
        QString value;
        quint64 revision{};
        qint64 queuedAtMilliseconds{};
    };

    struct RuntimeInstance final {
        QString key;
        QString listenAddress;
        int port{102};
        bool enabled{true};
        RuntimeState state{RuntimeState::ready};
        std::unique_ptr<QProcess> process;
        QByteArray standardOutputBuffer;
        QByteArray standardErrorBuffer;
        bool standardOutputDrainScheduled{};
        bool standardErrorDrainScheduled{};
        bool standardOutputOverflowReported{};
        bool standardErrorOverflowReported{};
        QString modelManifestPath;
        quint64 startGeneration{};
        quint64 modelRevision{};
        quint64 liveGeneration{};
        quint64 nextLiveRevision{};
        quint64 lastLiveAckRevision{};
        quint64 liveRequested{};
        quint64 liveSent{};
        quint64 liveCoalesced{};
        quint64 liveRejected{};
        qint64 liveLastAckLatencyMilliseconds{};
        qint64 liveMaxAckLatencyMilliseconds{};
        int liveMaxPending{};
        bool liveFlushScheduled{};
        QHash<QString, PendingLiveUpdate> pendingLiveUpdates;
        QHash<quint64, qint64> liveSentAtMilliseconds;
    };

    struct ValueSnapshot final {
        int iedIndex{-1};
        int valueIndex{-1};
        QVariantMap value;
    };

    bool importFile(const QUrl& fileUrl, bool append);
    void launchAsyncImport(PendingAsyncImport request);
    void finishAsyncImport(const std::shared_ptr<AsyncImportResult>& result);
    void rebuildPresentation();
    void rebuildRuntimeInstances(const QHash<QString, QVariantMap>& previousConfigurations);
    void rebuildValues();
    [[nodiscard]] bool adoptPreparedIed(int iedIndex);
    void seedRuntimeValues(int iedIndex);
    void updateIedRuntimePresentation(int index);
    void connectRuntimeSignals(int index);
    void setRuntimeState(int index, RuntimeState state);
    void performClear();
    void handleFleetStartReady(int index);
    void handleFleetStartFailure(int index, const QString& reason);
    void stopAllProcessesBlocking();

    void appendActivity(
        const QString& category,
        const QString& message,
        const QString& severity = QStringLiteral("Info"),
        const QString& iedName = QString{});
    void consumeServerOutput(
        int index,
        QByteArray& buffer,
        const QByteArray& bytes,
        bool standardError);
    void processServerLine(int index, const QString& line, bool standardError);
    [[nodiscard]] bool enqueueLiveUpdate(int index, const IedPointStore::PointRecord& point);
    void scheduleLiveFlush(int index);
    void flushLiveUpdates(int index, quint64 generation);
    void handleLiveUpdateAck(int index, const QVariantMap& fields);

    [[nodiscard]] bool writeModelManifest(int iedIndex);
    void removeModelManifests();
    [[nodiscard]] QString manifestPathFor(int iedIndex) const;
    [[nodiscard]] QString serverExecutable() const;
    [[nodiscard]] bool isLocalAddress(const QString& address) const;
    [[nodiscard]] QString endpointConflictFor(int index, bool includeReadyPeers) const;
    [[nodiscard]] QString fleetEndpointConflict() const;
    [[nodiscard]] QStringList bindableIpv4Addresses() const;
    [[nodiscard]] RuntimeInstance* runtimeAt(int index) noexcept;
    [[nodiscard]] const RuntimeInstance* runtimeAt(int index) const noexcept;
    [[nodiscard]] QString runtimeStateText(RuntimeState state) const;

    std::vector<LoadedDocument> documents_;
    std::vector<std::unique_ptr<RuntimeInstance>> runtimes_;
    QVariantList ieds_;
    IedPointStore pointStore_;
    QVector<int> selectedPointIndices_;
    QVariantList navigationIndex_;
    QHash<QString, QVector<int>> valueScopeIndex_;
    QVector<PreparedIedProjection> preparedIeds_;
    QSet<QString> seededRuntimeIeds_;
    QSet<int> fleetStartMembers_;
    QSet<int> fleetStartReady_;
    IedActivityModel activity_;
    std::optional<ValueSnapshot> previousValue_;
    QThreadPool importPool_;
    std::optional<PendingAsyncImport> pendingAsyncImport_;
    QString sourceName_;
    QString sourcePath_;
    QString fatalError_;
    QString defaultListenAddress_{QStringLiteral("0.0.0.0")};
    QString fileFolder_;
    quint64 asyncImportGeneration_{};
    qint64 lastImportWorkerMilliseconds_{};
    qint64 lastGuiApplyMilliseconds_{};
    int preparedPointCount_{};
    int preparedValueIndexIed_{-1};
    int fleetStartRollbackCount_{};
    int defaultPort_{102};
    int selectedIedIndex_{-1};
    int selectedValueIndex_{-1};
    int logicalDeviceCount_{};
    int dataObjectCount_{};
    int dataAttributeCount_{};
    int dataSetCount_{};
    int reportCount_{};
    int gooseCount_{};
    bool asyncImportRunning_{};
    bool gooseEnabled_{};
    bool fileServiceEnabled_{};
    bool clearPending_{};
    bool fleetStartPending_{};
};
