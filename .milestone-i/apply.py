from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def replace_once(text: str, old: str, new: str, label: str) -> str:
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{label}: expected one anchor, found {count}")
    return text.replace(old, new, 1)


def write(path: str, transform):
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    updated = transform(text)
    if updated == text:
        print(f"{path}: already applied")
        return
    target.write_text(updated, encoding="utf-8")
    print(f"{path}: updated")


def header(text: str) -> str:
    if "preparedIedCount READ preparedIedCount" in text:
        return text
    text = replace_once(
        text,
        "    Q_PROPERTY(int preparedPointCount READ preparedPointCount NOTIFY modelChanged)\n",
        "    Q_PROPERTY(int preparedPointCount READ preparedPointCount NOTIFY modelChanged)\n"
        "    Q_PROPERTY(int preparedIedCount READ preparedIedCount NOTIFY modelChanged)\n"
        "    Q_PROPERTY(int fleetStartRollbackCount READ fleetStartRollbackCount NOTIFY runtimeChanged)\n"
        "    Q_PROPERTY(bool clearPending READ clearPending NOTIFY modelChanged)\n",
        "properties")
    text = replace_once(
        text,
        "    [[nodiscard]] int preparedPointCount() const noexcept { return preparedPointCount_; }\n",
        "    [[nodiscard]] int preparedPointCount() const noexcept { return preparedPointCount_; }\n"
        "    [[nodiscard]] int preparedIedCount() const noexcept {\n"
        "        return static_cast<int>(preparedIeds_.size());\n"
        "    }\n"
        "    [[nodiscard]] int fleetStartRollbackCount() const noexcept { return fleetStartRollbackCount_; }\n"
        "    [[nodiscard]] bool clearPending() const noexcept { return clearPending_; }\n",
        "getters")
    text = replace_once(
        text,
        "    Q_INVOKABLE QString diagnosticsText() const;\n",
        "    Q_INVOKABLE QString diagnosticsText() const;\n"
        "    Q_INVOKABLE int qaBurstValues(int updates, int distinctPoints = 64);\n",
        "qa burst declaration")
    text = replace_once(
        text,
        "    struct AsyncImportResult final {\n",
        "    struct PreparedIedProjection final {\n"
        "        QVector<int> pointIndices;\n"
        "        QVariantList navigationIndex;\n"
        "        QHash<QString, QVector<int>> valueScopeIndex;\n"
        "    };\n\n"
        "    struct AsyncImportResult final {\n",
        "prepared projection")
    text = replace_once(
        text,
        "        QHash<QString, QVector<int>> valueScopeIndex;\n        QString error;\n",
        "        QHash<QString, QVector<int>> valueScopeIndex;\n"
        "        QVector<PreparedIedProjection> preparedIeds;\n"
        "        QString error;\n",
        "async prepared vector")
    text = replace_once(
        text,
        "        int preparedPointCount{};\n    };\n",
        "        int preparedPointCount{};\n"
        "        int preparedIedCount{};\n"
        "    };\n",
        "async count")
    text = replace_once(
        text,
        "    void rebuildValues();\n    void seedRuntimeValues(int iedIndex);\n",
        "    void rebuildValues();\n"
        "    [[nodiscard]] bool adoptPreparedIed(int iedIndex);\n"
        "    void seedRuntimeValues(int iedIndex);\n",
        "adopt helper")
    text = replace_once(
        text,
        "    void setRuntimeState(int index, RuntimeState state);\n    void stopAllProcessesBlocking();\n",
        "    void setRuntimeState(int index, RuntimeState state);\n"
        "    void performClear();\n"
        "    void handleFleetStartReady(int index);\n"
        "    void handleFleetStartFailure(int index, const QString& reason);\n"
        "    void stopAllProcessesBlocking();\n",
        "runtime helpers")
    text = replace_once(
        text,
        "    QHash<QString, QVector<int>> valueScopeIndex_;\n    QSet<QString> seededRuntimeIeds_;\n",
        "    QHash<QString, QVector<int>> valueScopeIndex_;\n"
        "    QVector<PreparedIedProjection> preparedIeds_;\n"
        "    QSet<QString> seededRuntimeIeds_;\n"
        "    QSet<int> fleetStartMembers_;\n"
        "    QSet<int> fleetStartReady_;\n",
        "controller stores")
    text = replace_once(
        text,
        "    int preparedValueIndexIed_{-1};\n    int defaultPort_{102};\n",
        "    int preparedValueIndexIed_{-1};\n"
        "    int fleetStartRollbackCount_{};\n"
        "    int defaultPort_{102};\n",
        "rollback counter")
    text = replace_once(
        text,
        "    bool fileServiceEnabled_{};\n};\n",
        "    bool fileServiceEnabled_{};\n"
        "    bool clearPending_{};\n"
        "    bool fleetStartPending_{};\n"
        "};\n",
        "state flags")
    return text


def async_cpp(text: str) -> str:
    if "prepared_ieds=%7" in text:
        return text
    text = replace_once(
        text,
        "    valueScopeIndex_.clear();\n    preparedValueIndexIed_ = -1;\n",
        "    valueScopeIndex_.clear();\n    preparedIeds_.clear();\n    preparedValueIndexIed_ = -1;\n",
        "async clear prepared")
    pattern = re.compile(r"            if \(!result->ieds\.isEmpty\(\)\) \{.*?            \}\n            result->preparationMilliseconds = preparationTimer\.elapsed\(\);", re.S)
    replacement = '''            result->preparedIeds.resize(result->ieds.size());
            for (int iedIndex = 0; iedIndex < result->ieds.size(); ++iedIndex) {
                const auto preparedIed = result->ieds.at(iedIndex).toMap();
                const auto preparedName = preparedIed.value(QStringLiteral("name")).toString();

                ar::iec61850::simulation::IedSimulatorProfileFromSclOptions options;
                options.ied_name = preparedName.toStdString();
                options.runtime_ied_name = options.ied_name;
                const auto built = ar::iec61850::simulation::IedSimulatorProfileBuilder::build(
                    document, options);

                auto& projection = result->preparedIeds[iedIndex];
                std::vector<const ar::iec61850::simulation::IedSimulatorPoint*> points;
                points.reserve(built.profile.point_count());
                for (const auto& device : built.profile.logical_devices) {
                    for (const auto& node : device.logical_nodes) {
                        for (const auto& point : node.points) points.push_back(&point);
                    }
                }
                std::stable_sort(
                    points.begin(), points.end(),
                    [](const auto* left, const auto* right) {
                        return left->source_order < right->source_order;
                    });

                result->pointStore.reserve(
                    result->pointStore.size() + static_cast<qsizetype>(points.size()));
                projection.pointIndices.reserve(static_cast<qsizetype>(points.size()));
                projection.valueScopeIndex.reserve(
                    static_cast<qsizetype>(built.profile.logical_node_count()));
                std::set<QString> seenScopes;
                int sourceIndex{};
                for (const auto* point : points) {
                    const auto logicalDevice = qstring(point->logical_device);
                    const auto logicalNode = qstring(point->logical_node);
                    const auto scopeKey =
                        logicalDevice + QLatin1Char('\\x1f') + logicalNode;
                    projection.valueScopeIndex[scopeKey].push_back(sourceIndex);
                    if (seenScopes.insert(scopeKey).second) {
                        QVariantMap navigationEntry;
                        navigationEntry.insert(QStringLiteral("logicalDevice"), logicalDevice);
                        navigationEntry.insert(QStringLiteral("logicalNode"), logicalNode);
                        projection.navigationIndex.push_back(std::move(navigationEntry));
                    }

                    const auto pointIndex = result->pointStore.insertIfMissing(
                        IedPointStore::fromSimulatorPoint(*point));
                    projection.pointIndices.push_back(pointIndex);
                    ++sourceIndex;
                }
                if (iedIndex == 0) {
                    result->selectedPointIndices = projection.pointIndices;
                    result->navigationIndex = projection.navigationIndex;
                    result->valueScopeIndex = projection.valueScopeIndex;
                    result->preparedPointCount = static_cast<int>(points.size());
                }
            }
            result->preparedIedCount = static_cast<int>(result->preparedIeds.size());
            result->preparationMilliseconds = preparationTimer.elapsed();'''
    text, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f"worker profile block: expected one anchor, found {count}")
    text = replace_once(
        text,
        "            valueScopeIndex_ = std::move(result->valueScopeIndex);\n            logicalDeviceCount_ = result->logicalDeviceCount;\n",
        "            valueScopeIndex_ = std::move(result->valueScopeIndex);\n"
        "            preparedIeds_ = std::move(result->preparedIeds);\n"
        "            logicalDeviceCount_ = result->logicalDeviceCount;\n",
        "adopt prepared IEDs")
    old_seed = '''            preparedValueIndexIed_ = selectedIedIndex_;
            seededRuntimeIeds_.clear();
            if (selectedIedIndex_ >= 0) {
                seededRuntimeIeds_.insert(
                    ieds_.at(selectedIedIndex_).toMap().value(QStringLiteral("sessionKey")).toString());
            }
            rebuildRuntimeInstances({});

            lastImportWorkerMilliseconds_ = result->elapsedMilliseconds;
            preparedPointCount_ = result->preparedPointCount;
            lastGuiApplyMilliseconds_ = applyTimer.elapsed();'''
    new_seed = '''            preparedValueIndexIed_ = selectedIedIndex_;
            seededRuntimeIeds_.clear();
            for (int index = 0; index < preparedIeds_.size() && index < ieds_.size(); ++index) {
                seededRuntimeIeds_.insert(
                    ieds_.at(index).toMap().value(QStringLiteral("sessionKey")).toString());
            }
            rebuildRuntimeInstances({});

            lastImportWorkerMilliseconds_ = result->elapsedMilliseconds;
            preparedPointCount_ = result->preparedPointCount;
            if (selectedIedIndex_ >= 0) adoptPreparedIed(selectedIedIndex_);
            lastGuiApplyMilliseconds_ = applyTimer.elapsed();'''
    text = replace_once(text, old_seed, new_seed, "finish seed cache")
    old_log = '''            qInfo().noquote() << QStringLiteral(
                "IEDSIM_IMPORT_PATH worker_ms=%1 parser_ms=%2 prepare_ms=%3 gui_apply_ms=%4 points=%5 scopes=%6 typed_store=1")
                .arg(lastImportWorkerMilliseconds_)
                .arg(result->parserMilliseconds)
                .arg(result->preparationMilliseconds)
                .arg(lastGuiApplyMilliseconds_)
                .arg(preparedPointCount_)
                .arg(valueScopeIndex_.size());
            appendActivity(
                QStringLiteral("Importer"),
                QStringLiteral(
                    "%1 parsed and indexed into typed point storage on the bounded worker in %2 ms; GUI adoption took %3 ms.")
                    .arg(sourceName_)
                    .arg(lastImportWorkerMilliseconds_)
                    .arg(lastGuiApplyMilliseconds_),
                QStringLiteral("Success"));'''
    new_log = '''            qInfo().noquote() << QStringLiteral(
                "IEDSIM_IMPORT_PATH worker_ms=%1 parser_ms=%2 prepare_ms=%3 gui_apply_ms=%4 points=%5 scopes=%6 typed_store=1 prepared_ieds=%7")
                .arg(lastImportWorkerMilliseconds_)
                .arg(result->parserMilliseconds)
                .arg(result->preparationMilliseconds)
                .arg(lastGuiApplyMilliseconds_)
                .arg(preparedPointCount_)
                .arg(valueScopeIndex_.size())
                .arg(result->preparedIedCount);
            appendActivity(
                QStringLiteral("Importer"),
                QStringLiteral(
                    "%1 parsed and prebuilt %4 IED profile%5 into typed point storage on the bounded worker in %2 ms; GUI adoption took %3 ms.")
                    .arg(sourceName_)
                    .arg(lastImportWorkerMilliseconds_)
                    .arg(lastGuiApplyMilliseconds_)
                    .arg(result->preparedIedCount)
                    .arg(result->preparedIedCount == 1 ? QString{} : QStringLiteral("s")),
                QStringLiteral("Success"));'''
    return replace_once(text, old_log, new_log, "import evidence")


def controller_cpp(text: str) -> str:
    if "int IedFleetController::qaBurstValues" in text:
        return text
    text = replace_once(
        text,
        "            selectedPointIndices_.clear();\n            seededRuntimeIeds_.clear();\n        }\n        navigationIndex_.clear();\n",
        "            selectedPointIndices_.clear();\n        }\n"
        "        preparedIeds_.clear();\n"
        "        seededRuntimeIeds_.clear();\n"
        "        fleetStartPending_ = false;\n"
        "        fleetStartMembers_.clear();\n"
        "        fleetStartReady_.clear();\n"
        "        fleetStartRollbackCount_ = 0;\n"
        "        navigationIndex_.clear();\n",
        "sync import invalidation")
    old_clear = '''void IedFleetController::clear() {
    stopAllProcessesBlocking();
    removeModelManifests();'''
    new_clear = '''void IedFleetController::clear() {
    if (anyRunning()) {
        if (clearPending_) return;
        clearPending_ = true;
        appendActivity(
            QStringLiteral("Workspace"),
            QStringLiteral("Stopping active IEDs before clearing the engineering model."),
            QStringLiteral("Info"));
        stopAllSimulations();
        emit modelChanged();
        return;
    }
    performClear();
}

void IedFleetController::performClear() {
    clearPending_ = false;
    removeModelManifests();'''
    text = replace_once(text, old_clear, new_clear, "nonblocking clear")
    text = replace_once(
        text,
        "    valueScopeIndex_.clear();\n    seededRuntimeIeds_.clear();\n    previousValue_.reset();\n",
        "    valueScopeIndex_.clear();\n"
        "    preparedIeds_.clear();\n"
        "    seededRuntimeIeds_.clear();\n"
        "    fleetStartPending_ = false;\n"
        "    fleetStartMembers_.clear();\n"
        "    fleetStartReady_.clear();\n"
        "    fleetStartRollbackCount_ = 0;\n"
        "    previousValue_.reset();\n",
        "clear caches")
    text = replace_once(
        text,
        "    previousValue_.reset();\n    rebuildValues();\n    emit selectionChanged();\n",
        "    previousValue_.reset();\n"
        "    if (!adoptPreparedIed(normalized)) rebuildValues();\n"
        "    emit valuesChanged();\n"
        "    emit selectionChanged();\n",
        "prepared switch")
    pattern = re.compile(r"int IedFleetController::startAllSimulations\(\) \{.*?\n\}\n\nvoid IedFleetController::stopAllSimulations", re.S)
    replacement = '''int IedFleetController::startAllSimulations() {
    if (!imported()) return 0;
    const auto fleetConflict = fleetEndpointConflict();
    if (!fleetConflict.isEmpty()) {
        appendActivity(QStringLiteral("Network"), fleetConflict, QStringLiteral("Error"));
        return 0;
    }
    if (fleetStartPending_) {
        appendActivity(
            QStringLiteral("Fleet"),
            QStringLiteral("A fleet start is already in progress."),
            QStringLiteral("Warning"));
        return 0;
    }

    QVector<int> candidates;
    for (int index = 0; index < ieds_.size(); ++index) {
        const auto* runtime = runtimeAt(index);
        if (runtime == nullptr || !runtime->enabled) continue;
        if (!isLocalAddress(runtime->listenAddress)) {
            appendActivity(
                QStringLiteral("Network"),
                QStringLiteral("%1 is not currently assigned to this computer.")
                    .arg(runtime->listenAddress),
                QStringLiteral("Error"),
                ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
            return 0;
        }
        if (!isActiveState(runtime->state)) candidates.push_back(index);
    }

    if (candidates.isEmpty()) return 0;
    fleetStartPending_ = true;
    fleetStartMembers_.clear();
    fleetStartReady_.clear();
    for (const int index : candidates) fleetStartMembers_.insert(index);

    int started{};
    for (const int index : candidates) {
        if (startIed(index)) {
            ++started;
            continue;
        }
        handleFleetStartFailure(index, QStringLiteral("An enabled IED could not be launched."));
        return 0;
    }
    appendActivity(
        QStringLiteral("Fleet"),
        QStringLiteral("Starting %1 IED endpoint%2.")
            .arg(started)
            .arg(started == 1 ? QString{} : QStringLiteral("s")),
        QStringLiteral("Info"));
    return started;
}

void IedFleetController::stopAllSimulations'''
    text, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f"fleet start: expected one anchor, found {count}")
    marker = "void IedFleetController::clearActivity() {\n"
    qa = '''int IedFleetController::qaBurstValues(const int updates, const int distinctPoints) {
    if (!qEnvironmentVariableIsSet("ARSTACK_IEDSIM_QA") || updates <= 0 ||
        selectedPointIndices_.isEmpty()) {
        return 0;
    }
    const int boundedUpdates = std::min(updates, 100'000);
    const int boundedDistinct = std::clamp(
        distinctPoints, 1, std::min(4'096, static_cast<int>(selectedPointIndices_.size())));
    const auto updated = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
    int applied{};
    for (int iteration = 0; iteration < boundedUpdates; ++iteration) {
        const int sourceIndex = iteration % boundedDistinct;
        auto* point = pointStore_.atMutable(selectedPointIndices_.at(sourceIndex));
        if (point == nullptr) continue;
        point->value = QString::number(iteration);
        point->origin = QStringLiteral("Responsiveness QA");
        point->changed = true;
        point->updated = updated;
        ++applied;
        emit valuesChanged();
    }
    return applied;
}

'''
    text = replace_once(text, marker, qa + marker, "QA burst implementation")
    text = replace_once(
        text,
        "        .arg(gooseCount_)\n        .arg(pointStore_.size());\n    text += QStringLiteral(\"\\nRecent activity (newest first):\\n\");\n",
        "        .arg(gooseCount_)\n"
        "        .arg(pointStore_.size());\n"
        "    text += QStringLiteral(\"Prepared IED profiles: %1; fleet rollbacks: %2\\n\")\n"
        "        .arg(preparedIeds_.size()).arg(fleetStartRollbackCount_);\n"
        "    text += QStringLiteral(\"\\nRecent activity (newest first):\\n\");\n",
        "diagnostics")
    text = replace_once(
        text,
        "void IedFleetController::rebuildValues() {\n",
        '''bool IedFleetController::adoptPreparedIed(const int iedIndex) {
    if (iedIndex < 0 || iedIndex >= preparedIeds_.size()) return false;
    const auto& projection = preparedIeds_.at(iedIndex);
    selectedPointIndices_ = projection.pointIndices;
    navigationIndex_ = projection.navigationIndex;
    valueScopeIndex_ = projection.valueScopeIndex;
    preparedValueIndexIed_ = iedIndex;
    preparedPointCount_ = static_cast<int>(selectedPointIndices_.size());
    selectedValueIndex_ = selectedPointIndices_.isEmpty() ? -1 : 0;
    return true;
}

void IedFleetController::rebuildValues() {
''',
        "adopt implementation")
    text = replace_once(
        text,
        "void IedFleetController::rebuildValues() {\n    selectedPointIndices_.clear();\n    selectedValueIndex_ = -1;\n",
        "void IedFleetController::rebuildValues() {\n"
        "    selectedPointIndices_.clear();\n"
        "    navigationIndex_.clear();\n"
        "    valueScopeIndex_.clear();\n"
        "    preparedValueIndexIed_ = -1;\n"
        "    preparedPointCount_ = 0;\n"
        "    selectedValueIndex_ = -1;\n",
        "fallback metadata clear")
    text = replace_once(
        text,
        "    if (!sessionKey.isEmpty() && seededRuntimeIeds_.contains(sessionKey)) return;\n\n    const int documentIndex",
        "    if (!sessionKey.isEmpty() && seededRuntimeIeds_.contains(sessionKey)) return;\n\n"
        "    if (iedIndex >= 0 && iedIndex < preparedIeds_.size()) {\n"
        "        if (!sessionKey.isEmpty()) seededRuntimeIeds_.insert(sessionKey);\n"
        "        return;\n"
        "    }\n\n"
        "    const int documentIndex",
        "seed prepared guard")
    old_error = '''        const bool expectedStop = current->state == RuntimeState::stopping;
        setRuntimeState(index, expectedStop ? RuntimeState::ready : RuntimeState::failed);
        if (!expectedStop) {
            appendActivity(
                QStringLiteral("Server"),
                QStringLiteral("MMS server process error %1: %2")
                    .arg(static_cast<int>(error))
                    .arg(current->process->errorString()),
                QStringLiteral("Error"),
                ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
        }'''
    new_error = '''        const bool failedDuringFleetStart = current->state == RuntimeState::starting;
        const bool expectedStop = current->state == RuntimeState::stopping;
        setRuntimeState(index, expectedStop ? RuntimeState::ready : RuntimeState::failed);
        if (!expectedStop) {
            const auto reason = QStringLiteral("MMS server process error %1: %2")
                .arg(static_cast<int>(error))
                .arg(current->process->errorString());
            appendActivity(
                QStringLiteral("Server"),
                reason,
                QStringLiteral("Error"),
                ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
            if (failedDuringFleetStart) handleFleetStartFailure(index, reason);
        }'''
    text = replace_once(text, old_error, new_error, "fleet process error")
    text = replace_once(
        text,
        "            const bool wasActive = isActiveState(current->state);\n            setRuntimeState(index, RuntimeState::ready);\n",
        "            const bool failedDuringFleetStart = current->state == RuntimeState::starting;\n"
        "            const bool wasActive = isActiveState(current->state);\n"
        "            setRuntimeState(index, RuntimeState::ready);\n",
        "fleet early-exit state")
    text = replace_once(
        text,
        "            if (wasActive) {\n                appendActivity(\n                    QStringLiteral(\"Server\"),\n                    QStringLiteral(\"MMS endpoint stopped (exit %1).\").arg(exitCode),\n                    exitCode == 0 ? QStringLiteral(\"Info\") : QStringLiteral(\"Warning\"),\n                    ieds_.at(index).toMap().value(QStringLiteral(\"name\")).toString());\n            }\n",
        "            if (wasActive) {\n"
        "                appendActivity(\n"
        "                    QStringLiteral(\"Server\"),\n"
        "                    QStringLiteral(\"MMS endpoint stopped (exit %1).\").arg(exitCode),\n"
        "                    exitCode == 0 ? QStringLiteral(\"Info\") : QStringLiteral(\"Warning\"),\n"
        "                    ieds_.at(index).toMap().value(QStringLiteral(\"name\")).toString());\n"
        "            }\n"
        "            if (failedDuringFleetStart) {\n"
        "                handleFleetStartFailure(\n"
        "                    index, QStringLiteral(\"Endpoint exited before listener readiness was confirmed.\"));\n"
        "            }\n",
        "fleet early-exit rollback")
    old_state = '''void IedFleetController::setRuntimeState(const int index, const RuntimeState state) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || runtime->state == state) return;
    runtime->state = state;
    updateIedRuntimePresentation(index);
    emit runtimeChanged();
    emit modelChanged();
    if (index == selectedIedIndex_) emit selectionChanged();
}
'''
    new_state = '''void IedFleetController::setRuntimeState(const int index, const RuntimeState state) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || runtime->state == state) return;
    runtime->state = state;
    updateIedRuntimePresentation(index);
    emit runtimeChanged();
    emit modelChanged();
    if (index == selectedIedIndex_) emit selectionChanged();

    if (clearPending_ && !anyRunning()) {
        QTimer::singleShot(0, this, [this] {
            if (clearPending_ && !anyRunning()) performClear();
        });
    }
}

void IedFleetController::handleFleetStartReady(const int index) {
    if (!fleetStartPending_ || !fleetStartMembers_.contains(index)) return;
    fleetStartReady_.insert(index);
    if (fleetStartReady_.size() != fleetStartMembers_.size()) return;

    const int readyCount = fleetStartReady_.size();
    fleetStartPending_ = false;
    fleetStartMembers_.clear();
    fleetStartReady_.clear();
    appendActivity(
        QStringLiteral("Fleet"),
        QStringLiteral("%1 IED endpoint%2 reached listener-ready state.")
            .arg(readyCount)
            .arg(readyCount == 1 ? QString{} : QStringLiteral("s")),
        QStringLiteral("Success"));
}

void IedFleetController::handleFleetStartFailure(const int index, const QString& reason) {
    if (!fleetStartPending_ || !fleetStartMembers_.contains(index)) return;

    const auto failedName = index >= 0 && index < ieds_.size()
        ? ieds_.at(index).toMap().value(QStringLiteral("name")).toString()
        : QStringLiteral("IED");
    const auto members = fleetStartMembers_.values();
    fleetStartPending_ = false;
    fleetStartMembers_.clear();
    fleetStartReady_.clear();
    ++fleetStartRollbackCount_;
    appendActivity(
        QStringLiteral("Fleet"),
        QStringLiteral("Fleet start rolled back after %1 failed: %2").arg(failedName, reason),
        QStringLiteral("Error"),
        failedName);
    for (const int member : members) {
        const auto* runtime = runtimeAt(member);
        if (runtime != nullptr && isActiveState(runtime->state)) stopIed(member);
    }
    emit runtimeChanged();
}
'''
    text = replace_once(text, old_state, new_state, "fleet helpers")
    text = replace_once(
        text,
        "    if (kind == QStringLiteral(\"server_ready\")) {\n        setRuntimeState(index, RuntimeState::running);\n",
        "    if (kind == QStringLiteral(\"server_ready\")) {\n"
        "        setRuntimeState(index, RuntimeState::running);\n"
        "        handleFleetStartReady(index);\n",
        "fleet ready")
    return text


write("apps/ied_simulator/src/IedFleetController.hpp", header)
write("apps/ied_simulator/src/IedFleetControllerAsync.cpp", async_cpp)
write("apps/ied_simulator/src/IedFleetController.cpp", controller_cpp)
