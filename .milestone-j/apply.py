#!/usr/bin/env python3
from pathlib import Path


def update(path: str, transform) -> None:
    file = Path(path)
    text = file.read_text(encoding="utf-8")
    updated = transform(text)
    if updated == text:
        raise RuntimeError(f"{path}: transformation made no changes")
    file.write_text(updated, encoding="utf-8")
    print(f"{path}: updated")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if text.count(old) != 1:
        raise RuntimeError(f"{label}: expected exactly one match, got {text.count(old)}")
    return text.replace(old, new, 1)


def header(text: str) -> str:
    text = replace_once(
        text,
        "#include <QHash>\n#include <QObject>",
        "#include <QByteArray>\n#include <QHash>\n#include <QObject>",
        "header QByteArray include",
    )
    text = replace_once(
        text,
        "    Q_INVOKABLE int qaBurstValues(int updates, int distinctPoints = 64);\n",
        "    Q_INVOKABLE int qaBurstValues(int updates, int distinctPoints = 64);\n"
        "    Q_INVOKABLE int qaBurstLiveValues(int updates);\n",
        "header live QA invokable",
    )
    text = replace_once(
        text,
        "    struct RuntimeInstance final {\n",
        "    struct PendingLiveUpdate final {\n"
        "        QString key;\n"
        "        QString domain;\n"
        "        QString item;\n"
        "        QString value;\n"
        "        quint64 revision{};\n"
        "        qint64 queuedAtMilliseconds{};\n"
        "    };\n\n"
        "    struct RuntimeInstance final {\n",
        "header pending live struct",
    )
    text = replace_once(
        text,
        "        quint64 startGeneration{};\n        quint64 modelRevision{};\n",
        "        quint64 startGeneration{};\n"
        "        quint64 modelRevision{};\n"
        "        quint64 liveGeneration{};\n"
        "        quint64 nextLiveRevision{};\n"
        "        quint64 lastLiveAckRevision{};\n"
        "        quint64 liveRequested{};\n"
        "        quint64 liveSent{};\n"
        "        quint64 liveCoalesced{};\n"
        "        quint64 liveRejected{};\n"
        "        qint64 liveLastAckLatencyMilliseconds{};\n"
        "        qint64 liveMaxAckLatencyMilliseconds{};\n"
        "        int liveMaxPending{};\n"
        "        bool liveFlushScheduled{};\n"
        "        QHash<QString, PendingLiveUpdate> pendingLiveUpdates;\n"
        "        QHash<quint64, qint64> liveSentAtMilliseconds;\n",
        "header runtime live fields",
    )
    text = replace_once(
        text,
        "    void processServerLine(int index, const QString& line, bool standardError);\n\n"
        "    [[nodiscard]] bool writeModelManifest(int iedIndex);",
        "    void processServerLine(int index, const QString& line, bool standardError);\n"
        "    [[nodiscard]] bool enqueueLiveUpdate(int index, const IedPointStore::PointRecord& point);\n"
        "    void scheduleLiveFlush(int index);\n"
        "    void flushLiveUpdates(int index, quint64 generation);\n"
        "    void handleLiveUpdateAck(int index, const QVariantMap& fields);\n\n"
        "    [[nodiscard]] bool writeModelManifest(int iedIndex);",
        "header live methods",
    )
    return text


def controller(text: str) -> str:
    text = replace_once(
        text,
        "bool isActiveState(const IedFleetController::RuntimeState state) {\n"
        "    return state == IedFleetController::RuntimeState::starting ||\n"
        "        state == IedFleetController::RuntimeState::running ||\n"
        "        state == IedFleetController::RuntimeState::stopping;\n"
        "}\n} // namespace\n",
        "bool isActiveState(const IedFleetController::RuntimeState state) {\n"
        "    return state == IedFleetController::RuntimeState::starting ||\n"
        "        state == IedFleetController::RuntimeState::running ||\n"
        "        state == IedFleetController::RuntimeState::stopping;\n"
        "}\n\n"
        "constexpr int kLivePendingLimit = 256;\n"
        "constexpr int kLiveInflightLimit = 256;\n"
        "constexpr int kLiveFlushBudget = 64;\n"
        "constexpr qint64 kLiveProcessBufferLimit = 64 * 1024;\n"
        "constexpr qsizetype kLivePayloadBudget = 32 * 1024;\n"
        "constexpr qsizetype kLiveValueByteLimit = 4 * 1024;\n"
        "} // namespace\n",
        "controller live constants",
    )
    text = replace_once(
        text,
        "    ++runtime->startGeneration;\n"
        "    const auto generation = runtime->startGeneration;\n"
        "    setRuntimeState(index, RuntimeState::starting);\n"
        "    runtime->process->setProgram(executable);\n"
        "    runtime->process->setArguments({\n"
        "        QStringLiteral(\"--host\"), runtime->listenAddress,\n"
        "        QStringLiteral(\"--port\"), QString::number(runtime->port),\n"
        "        QStringLiteral(\"--model-manifest\"), runtime->modelManifestPath});\n",
        "    ++runtime->startGeneration;\n"
        "    const auto generation = runtime->startGeneration;\n"
        "    runtime->liveGeneration = generation;\n"
        "    runtime->nextLiveRevision = 0;\n"
        "    runtime->lastLiveAckRevision = 0;\n"
        "    runtime->liveRequested = 0;\n"
        "    runtime->liveSent = 0;\n"
        "    runtime->liveCoalesced = 0;\n"
        "    runtime->liveRejected = 0;\n"
        "    runtime->liveLastAckLatencyMilliseconds = 0;\n"
        "    runtime->liveMaxAckLatencyMilliseconds = 0;\n"
        "    runtime->liveMaxPending = 0;\n"
        "    runtime->liveFlushScheduled = false;\n"
        "    runtime->pendingLiveUpdates.clear();\n"
        "    runtime->liveSentAtMilliseconds.clear();\n"
        "    setRuntimeState(index, RuntimeState::starting);\n"
        "    runtime->process->setProgram(executable);\n"
        "    runtime->process->setArguments({\n"
        "        QStringLiteral(\"--host\"), runtime->listenAddress,\n"
        "        QStringLiteral(\"--port\"), QString::number(runtime->port),\n"
        "        QStringLiteral(\"--model-manifest\"), runtime->modelManifestPath,\n"
        "        QStringLiteral(\"--live-stdin\"),\n"
        "        QStringLiteral(\"--live-generation\"), QString::number(runtime->liveGeneration)});\n",
        "controller start live channel",
    )
    text = replace_once(
        text,
        "    if (!writeModelManifest(selectedIedIndex_)) {\n"
        "        *point = IedPointStore::fromVariantMap(previousValue_->value);\n"
        "        previousValue_.reset();\n"
        "        emit valuesChanged();\n"
        "        emit selectionChanged();\n"
        "        return false;\n"
        "    }\n",
        "    if (!enqueueLiveUpdate(selectedIedIndex_, *point)) {\n"
        "        *point = IedPointStore::fromVariantMap(previousValue_->value);\n"
        "        previousValue_.reset();\n"
        "        emit valuesChanged();\n"
        "        emit selectionChanged();\n"
        "        return false;\n"
        "    }\n",
        "controller apply hot path",
    )
    text = replace_once(
        text,
        "    *point = previous;\n"
        "    if (!writeModelManifest(selectedIedIndex_)) {\n"
        "        *point = current;\n"
        "        return false;\n"
        "    }\n",
        "    *point = previous;\n"
        "    if (running() && !enqueueLiveUpdate(selectedIedIndex_, *point)) {\n"
        "        *point = current;\n"
        "        return false;\n"
        "    }\n",
        "controller undo hot path",
    )
    marker = "void IedFleetController::clearActivity() {\n"
    live_impl = r'''bool IedFleetController::enqueueLiveUpdate(
    const int index,
    const IedPointStore::PointRecord& point) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || runtime->state != RuntimeState::running ||
        runtime->process == nullptr || runtime->process->state() != QProcess::Running ||
        point.mmsDomain.isEmpty() || point.mmsItem.isEmpty()) {
        return false;
    }

    const auto valueBytes = point.value.toUtf8();
    if (valueBytes.size() > kLiveValueByteLimit) {
        ++runtime->liveRejected;
        appendActivity(
            QStringLiteral("Live data"),
            QStringLiteral("Value update exceeds the bounded 4 KiB live payload limit."),
            QStringLiteral("Error"),
            point.iedName);
        return false;
    }

    ++runtime->liveRequested;
    const auto revision = ++runtime->nextLiveRevision;
    const auto key = point.mmsDomain + QLatin1Char('\x1f') + point.mmsItem;
    const auto queuedAt = QDateTime::currentMSecsSinceEpoch();
    auto existing = runtime->pendingLiveUpdates.find(key);
    if (existing != runtime->pendingLiveUpdates.end()) {
        existing->domain = point.mmsDomain;
        existing->item = point.mmsItem;
        existing->value = point.value;
        existing->revision = revision;
        existing->queuedAtMilliseconds = queuedAt;
        ++runtime->liveCoalesced;
    } else {
        if (runtime->pendingLiveUpdates.size() >= kLivePendingLimit) {
            ++runtime->liveRejected;
            appendActivity(
                QStringLiteral("Live data"),
                QStringLiteral("Live update queue is saturated; the edit was rejected to keep memory bounded."),
                QStringLiteral("Error"),
                point.iedName);
            return false;
        }
        PendingLiveUpdate update;
        update.key = key;
        update.domain = point.mmsDomain;
        update.item = point.mmsItem;
        update.value = point.value;
        update.revision = revision;
        update.queuedAtMilliseconds = queuedAt;
        runtime->pendingLiveUpdates.insert(key, std::move(update));
        runtime->liveMaxPending = std::max(
            runtime->liveMaxPending,
            static_cast<int>(runtime->pendingLiveUpdates.size()));
    }
    scheduleLiveFlush(index);
    return true;
}

void IedFleetController::scheduleLiveFlush(const int index) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || runtime->liveFlushScheduled ||
        runtime->pendingLiveUpdates.isEmpty()) {
        return;
    }
    runtime->liveFlushScheduled = true;
    const auto generation = runtime->liveGeneration;
    QTimer::singleShot(4, this, [this, index, generation] {
        auto* current = runtimeAt(index);
        if (current == nullptr || current->liveGeneration != generation) return;
        current->liveFlushScheduled = false;
        flushLiveUpdates(index, generation);
    });
}

void IedFleetController::flushLiveUpdates(const int index, const quint64 generation) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || runtime->liveGeneration != generation ||
        runtime->state != RuntimeState::running || runtime->process == nullptr ||
        runtime->process->state() != QProcess::Running) {
        return;
    }
    if (runtime->pendingLiveUpdates.isEmpty()) return;
    if (runtime->process->bytesToWrite() >= kLiveProcessBufferLimit ||
        runtime->liveSentAtMilliseconds.size() >= kLiveInflightLimit) {
        scheduleLiveFlush(index);
        return;
    }

    QVector<PendingLiveUpdate> candidates;
    candidates.reserve(runtime->pendingLiveUpdates.size());
    for (auto it = runtime->pendingLiveUpdates.cbegin();
         it != runtime->pendingLiveUpdates.cend(); ++it) {
        candidates.push_back(it.value());
    }
    std::sort(
        candidates.begin(), candidates.end(),
        [](const PendingLiveUpdate& left, const PendingLiveUpdate& right) {
            return left.revision < right.revision;
        });

    QByteArray payload;
    QVector<PendingLiveUpdate> sending;
    sending.reserve(std::min(kLiveFlushBudget, candidates.size()));
    for (const auto& update : candidates) {
        if (sending.size() >= kLiveFlushBudget ||
            runtime->liveSentAtMilliseconds.size() + sending.size() >= kLiveInflightLimit) {
            break;
        }
        const auto line = QByteArrayLiteral("ARSTACK_LIVE\t1\t") +
            QByteArray::number(generation) + '\t' +
            QByteArray::number(update.revision) + '\t' +
            update.domain.toUtf8().toHex() + '\t' +
            update.item.toUtf8().toHex() + '\t' +
            update.value.toUtf8().toHex() + '\n';
        if (!payload.isEmpty() && payload.size() + line.size() > kLivePayloadBudget) break;
        payload += line;
        sending.push_back(update);
    }
    if (sending.isEmpty()) {
        scheduleLiveFlush(index);
        return;
    }

    const auto written = runtime->process->write(payload);
    if (written != payload.size()) {
        ++runtime->liveRejected;
        appendActivity(
            QStringLiteral("Live data"),
            QStringLiteral("Could not write the bounded live-update batch to the simulator process."),
            QStringLiteral("Error"),
            index >= 0 && index < ieds_.size()
                ? ieds_.at(index).toMap().value(QStringLiteral("name")).toString()
                : QString{});
        stopIed(index);
        return;
    }

    for (const auto& update : sending) {
        runtime->pendingLiveUpdates.remove(update.key);
        runtime->liveSentAtMilliseconds.insert(update.revision, update.queuedAtMilliseconds);
    }
    runtime->liveSent += static_cast<quint64>(sending.size());
    if (!runtime->pendingLiveUpdates.isEmpty()) scheduleLiveFlush(index);
}

void IedFleetController::handleLiveUpdateAck(const int index, const QVariantMap& fields) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr) return;
    bool generationOk{};
    bool revisionOk{};
    const auto generation = fields.value(QStringLiteral("generation")).toString().toULongLong(&generationOk);
    const auto revision = fields.value(QStringLiteral("revision")).toString().toULongLong(&revisionOk);
    if (!generationOk || !revisionOk || generation != runtime->liveGeneration) return;

    const bool accepted = fields.value(QStringLiteral("accepted")).toString() == QStringLiteral("true");
    const auto sent = runtime->liveSentAtMilliseconds.find(revision);
    qint64 latency{};
    if (sent != runtime->liveSentAtMilliseconds.end()) {
        latency = std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch() - sent.value());
        runtime->liveSentAtMilliseconds.erase(sent);
        runtime->liveLastAckLatencyMilliseconds = latency;
        runtime->liveMaxAckLatencyMilliseconds = std::max(
            runtime->liveMaxAckLatencyMilliseconds, latency);
    }
    if (accepted) {
        runtime->lastLiveAckRevision = std::max(runtime->lastLiveAckRevision, revision);
        qInfo().noquote() << QStringLiteral(
            "IEDSIM_LIVE_ACK generation=%1 revision=%2 latency_ms=%3 pending=%4 inflight=%5")
            .arg(generation).arg(revision).arg(latency)
            .arg(runtime->pendingLiveUpdates.size())
            .arg(runtime->liveSentAtMilliseconds.size());
    } else {
        ++runtime->liveRejected;
        appendActivity(
            QStringLiteral("Live data"),
            QStringLiteral("Server rejected live update revision %1 (%2).")
                .arg(revision)
                .arg(fields.value(QStringLiteral("reason")).toString()),
            QStringLiteral("Error"),
            index >= 0 && index < ieds_.size()
                ? ieds_.at(index).toMap().value(QStringLiteral("name")).toString()
                : QString{});
    }
    if (!runtime->pendingLiveUpdates.isEmpty()) scheduleLiveFlush(index);
}

int IedFleetController::qaBurstLiveValues(const int updates) {
    if (!qEnvironmentVariableIsSet("ARSTACK_IEDSIM_QA") || updates <= 0 ||
        !running() || selectedPointIndices_.isEmpty()) {
        return 0;
    }
    const int bounded = std::min(updates, 100'000);
    auto* runtime = runtimeAt(selectedIedIndex_);
    auto* point = pointStore_.atMutable(selectedPointIndices_.constFirst());
    if (runtime == nullptr || point == nullptr) return 0;

    int accepted{};
    const auto normalizedType = point->type.trimmed().toLower();
    for (int iteration = 0; iteration < bounded; ++iteration) {
        if (normalizedType.contains(QStringLiteral("bool"))) {
            point->value = (iteration & 1) != 0 ? QStringLiteral("true") : QStringLiteral("false");
        } else {
            point->value = QString::number(iteration);
        }
        point->origin = QStringLiteral("Live data-plane QA");
        point->changed = true;
        point->updated = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
        if (!enqueueLiveUpdate(selectedIedIndex_, *point)) break;
        ++accepted;
        emit valuesChanged();
    }
    qInfo().noquote() << QStringLiteral(
        "IEDSIM_LIVE_BURST requested=%1 accepted=%2 coalesced=%3 rejected=%4 pending=%5 pending_max=%6 final=%7")
        .arg(bounded).arg(accepted).arg(runtime->liveCoalesced).arg(runtime->liveRejected)
        .arg(runtime->pendingLiveUpdates.size()).arg(runtime->liveMaxPending).arg(point->value);
    return accepted;
}

'''
    if text.count(marker) != 1:
        raise RuntimeError("controller live implementation anchor mismatch")
    text = text.replace(marker, live_impl + marker, 1)

    text = replace_once(
        text,
        "    const auto kind = fields.value(QStringLiteral(\"kind\")).toString();\n"
        "    if (kind == QStringLiteral(\"server_ready\")) {\n",
        "    const auto kind = fields.value(QStringLiteral(\"kind\")).toString();\n"
        "    if (kind == QStringLiteral(\"live_update_ack\")) {\n"
        "        handleLiveUpdateAck(index, fields);\n"
        "        return;\n"
        "    }\n"
        "    if (kind == QStringLiteral(\"server_ready\")) {\n",
        "controller ack parser",
    )
    text = replace_once(
        text,
        "    text += QStringLiteral(\"Prepared IED profiles: %1; fleet rollbacks: %2\\n\")\n"
        "        .arg(preparedIeds_.size()).arg(fleetStartRollbackCount_);\n",
        "    text += QStringLiteral(\"Prepared IED profiles: %1; fleet rollbacks: %2\\n\")\n"
        "        .arg(preparedIeds_.size()).arg(fleetStartRollbackCount_);\n"
        "    for (int index = 0; index < static_cast<int>(runtimes_.size()); ++index) {\n"
        "        const auto* runtime = runtimeAt(index);\n"
        "        if (runtime == nullptr || runtime->liveGeneration == 0) continue;\n"
        "        text += QStringLiteral(\"  Live[%1]: gen=%2 requested=%3 sent=%4 coalesced=%5 rejected=%6 ack=%7 pending=%8 inflight=%9 maxPending=%10 maxAckMs=%11\\n\")\n"
        "            .arg(index).arg(runtime->liveGeneration).arg(runtime->liveRequested)\n"
        "            .arg(runtime->liveSent).arg(runtime->liveCoalesced).arg(runtime->liveRejected)\n"
        "            .arg(runtime->lastLiveAckRevision).arg(runtime->pendingLiveUpdates.size())\n"
        "            .arg(runtime->liveSentAtMilliseconds.size()).arg(runtime->liveMaxPending)\n"
        "            .arg(runtime->liveMaxAckLatencyMilliseconds);\n"
        "    }\n",
        "controller live diagnostics",
    )
    return text


def main_cpp(text: str) -> str:
    text = replace_once(
        text,
        "    const QCommandLineOption setFirstValueOption{\n"
        "        QStringLiteral(\"set-first-value\"),\n"
        "        QStringLiteral(\"QA: apply a value to the first runtime point after start.\"),\n"
        "        QStringLiteral(\"value\")};\n",
        "    const QCommandLineOption setFirstValueOption{\n"
        "        QStringLiteral(\"set-first-value\"),\n"
        "        QStringLiteral(\"QA: apply a value to the first runtime point after start.\"),\n"
        "        QStringLiteral(\"value\")};\n"
        "    const QCommandLineOption qaLiveBurstOption{\n"
        "        QStringLiteral(\"qa-live-burst\"),\n"
        "        QStringLiteral(\"QA: enqueue a bounded burst through the live runtime data plane.\"),\n"
        "        QStringLiteral(\"count\")};\n",
        "main live burst option",
    )
    text = replace_once(
        text,
        "        portOption,\n        setFirstValueOption,\n        exitAfterOption});\n",
        "        portOption,\n        setFirstValueOption,\n        qaLiveBurstOption,\n        exitAfterOption});\n",
        "main register live burst",
    )
    anchor = "        if (parser.isSet(screenshotOption)) {\n"
    live_schedule = r'''        if (backend != nullptr && parser.isSet(qaLiveBurstOption)) {
            bool countOk{};
            const auto requested = parser.value(qaLiveBurstOption).toInt(&countOk);
            const int count = countOk ? std::clamp(requested, 1, 100'000) : 0;
            if (count <= 0) {
                qWarning().noquote() << "--qa-live-burst requires a positive count.";
                QTimer::singleShot(0, &app, [] { QCoreApplication::exit(12); });
            } else {
                auto* const liveTimer = new QTimer{backend};
                liveTimer->setInterval(25);
                QObject::connect(liveTimer, &QTimer::timeout, backend, [backend, liveTimer, count] {
                    if (!backend->property("running").toBool()) return;
                    int accepted{};
                    const bool invoked = QMetaObject::invokeMethod(
                        backend,
                        "qaBurstLiveValues",
                        Q_RETURN_ARG(int, accepted),
                        Q_ARG(int, count));
                    liveTimer->stop();
                    liveTimer->deleteLater();
                    if (!invoked || accepted != count) {
                        qWarning().noquote()
                            << "Live burst was not fully accepted" << accepted << "of" << count;
                    }
                });
                liveTimer->start();
            }
        }
'''
    if text.count(anchor) != 1:
        raise RuntimeError("main live schedule anchor mismatch")
    text = text.replace(anchor, live_schedule + anchor, 1)
    return text


def server(text: str) -> str:
    text = replace_once(
        text,
        "#include <csignal>\n#include <cstddef>",
        "#include <csignal>\n#include <cstddef>\n#include <deque>",
        "server deque include",
    )
    text = replace_once(
        text,
        "#include <memory>\n#include <optional>",
        "#include <memory>\n#include <mutex>\n#include <optional>",
        "server mutex include",
    )
    text = replace_once(
        text,
        "#include <winsock2.h>\n#include <ws2tcpip.h>",
        "#include <winsock2.h>\n#include <ws2tcpip.h>\n#include <windows.h>",
        "server windows include",
    )
    text = replace_once(
        text,
        "struct CliOptions final {\n"
        "    std::string bind_address{\"0.0.0.0\"};\n"
        "    std::string model_manifest;\n"
        "    std::uint16_t port{102U};\n"
        "    std::uint8_t digital_input_mask{};\n"
        "    std::size_t maximum_connections{};\n"
        "    std::size_t maximum_active_connections{8U};\n"
        "};\n",
        "struct CliOptions final {\n"
        "    std::string bind_address{\"0.0.0.0\"};\n"
        "    std::string model_manifest;\n"
        "    std::uint16_t port{102U};\n"
        "    std::uint8_t digital_input_mask{};\n"
        "    std::size_t maximum_connections{};\n"
        "    std::size_t maximum_active_connections{8U};\n"
        "    std::uint64_t live_generation{};\n"
        "    bool live_stdin{};\n"
        "};\n",
        "server cli live fields",
    )
    text = replace_once(
        text,
        "        << \"  --model-manifest PATH     Host model manifest emitted by the Qt simulator.\\n\"\n",
        "        << \"  --model-manifest PATH     Host model manifest emitted by the Qt simulator.\\n\"\n"
        "        << \"  --live-stdin              Accept bounded ARSTACK_LIVE updates on stdin.\\n\"\n"
        "        << \"  --live-generation N       Runtime generation required by live updates.\\n\"\n",
        "server usage live flags",
    )
    text = replace_once(
        text,
        "        if (option == \"--host\" || option == \"--model-manifest\" ||\n"
        "            option == \"--port\" || option == \"--digital-input-mask\" ||\n"
        "            option == \"--max-connections\" || option == \"--max-active\") {\n",
        "        if (option == \"--live-stdin\") {\n"
        "            options.live_stdin = true;\n"
        "            continue;\n"
        "        }\n"
        "        if (option == \"--host\" || option == \"--model-manifest\" ||\n"
        "            option == \"--port\" || option == \"--digital-input-mask\" ||\n"
        "            option == \"--max-connections\" || option == \"--max-active\" ||\n"
        "            option == \"--live-generation\") {\n",
        "server parse live flag",
    )
    text = replace_once(
        text,
        "            } else if (option == \"--max-active\") {\n"
        "                const auto parsed = parse_u32(option, value, 64U);\n"
        "                if (parsed == 0U) {\n"
        "                    throw std::invalid_argument(\"--max-active must be 1..64.\");\n"
        "                }\n"
        "                options.maximum_active_connections = static_cast<std::size_t>(parsed);\n"
        "            } else {\n",
        "            } else if (option == \"--max-active\") {\n"
        "                const auto parsed = parse_u32(option, value, 64U);\n"
        "                if (parsed == 0U) {\n"
        "                    throw std::invalid_argument(\"--max-active must be 1..64.\");\n"
        "                }\n"
        "                options.maximum_active_connections = static_cast<std::size_t>(parsed);\n"
        "            } else if (option == \"--live-generation\") {\n"
        "                std::size_t consumed{};\n"
        "                options.live_generation = std::stoull(value, &consumed, 10);\n"
        "                if (consumed != value.size() || options.live_generation == 0U) {\n"
        "                    throw std::invalid_argument(\"--live-generation must be a positive integer.\");\n"
        "                }\n"
        "            } else {\n",
        "server parse generation",
    )
    text = replace_once(
        text,
        "    return options;\n}\n\nstruct EncodedValue final {\n",
        "    if (options.live_stdin && options.live_generation == 0U) {\n"
        "        throw std::invalid_argument(\"--live-stdin requires --live-generation.\");\n"
        "    }\n"
        "    return options;\n}\n\nstruct EncodedValue final {\n",
        "server validate live args",
    )

    manifest_anchor = "struct BrcbAssociationRuntime final {\n"
    live_types = r'''struct LiveUpdate final {
    std::uint64_t sequence{};
    std::uint64_t generation{};
    std::uint64_t revision{};
    std::string domain;
    std::string item;
    std::string value;
};

struct LiveUpdateCollection final {
    std::vector<LiveUpdate> updates;
    std::uint64_t sequence{};
    bool resync{};
};

class LiveUpdateBus final {
public:
    explicit LiveUpdateBus(const std::uint64_t generation) : generation_{generation} {}

    [[nodiscard]] bool publish(LiveUpdate update, std::uint64_t* const sequence) {
        std::scoped_lock lock{mutex_};
        if (update.generation != generation_ || update.revision <= last_revision_) return false;
        update.sequence = next_sequence_++;
        last_revision_ = update.revision;
        const auto key = update.domain + '\x1f' + update.item;
        latest_[key] = update;
        history_.push_back(update);
        while (history_.size() > kHistoryLimit) history_.pop_front();
        if (sequence != nullptr) *sequence = update.sequence;
        return true;
    }

    [[nodiscard]] LiveUpdateCollection latestSnapshot() const {
        std::scoped_lock lock{mutex_};
        LiveUpdateCollection result;
        result.sequence = next_sequence_ > 0U ? next_sequence_ - 1U : 0U;
        result.resync = true;
        result.updates.reserve(latest_.size());
        for (const auto& [key, update] : latest_) {
            static_cast<void>(key);
            result.updates.push_back(update);
        }
        std::sort(result.updates.begin(), result.updates.end(),
            [](const LiveUpdate& left, const LiveUpdate& right) {
                return left.sequence < right.sequence;
            });
        return result;
    }

    [[nodiscard]] LiveUpdateCollection collectSince(const std::uint64_t sequence) const {
        std::scoped_lock lock{mutex_};
        LiveUpdateCollection result;
        result.sequence = next_sequence_ > 0U ? next_sequence_ - 1U : 0U;
        if (sequence >= result.sequence || history_.empty()) return result;
        if (sequence + 1U < history_.front().sequence) {
            result.resync = true;
            result.updates.reserve(latest_.size());
            for (const auto& [key, update] : latest_) {
                static_cast<void>(key);
                result.updates.push_back(update);
            }
            std::sort(result.updates.begin(), result.updates.end(),
                [](const LiveUpdate& left, const LiveUpdate& right) {
                    return left.sequence < right.sequence;
                });
            return result;
        }
        result.updates.reserve(history_.size());
        for (const auto& update : history_) {
            if (update.sequence > sequence) result.updates.push_back(update);
        }
        return result;
    }

private:
    static constexpr std::size_t kHistoryLimit = 1024U;
    std::uint64_t generation_{};
    mutable std::mutex mutex_;
    std::deque<LiveUpdate> history_;
    std::unordered_map<std::string, LiveUpdate> latest_;
    std::uint64_t next_sequence_{1U};
    std::uint64_t last_revision_{};
};

struct LiveInputState final {
    std::string buffer;
    std::uint64_t last_revision{};
    bool overflow_reported{};
    bool eof{};
};

'''
    if text.count(manifest_anchor) != 1:
        raise RuntimeError("server live types anchor mismatch")
    text = text.replace(manifest_anchor, live_types + manifest_anchor, 1)

    helper_anchor = "[[nodiscard]] const mms::MmsStaticDataSetEntry* find_data_set(\n"
    live_helpers = r'''[[nodiscard]] std::optional<std::string> decode_hex(const std::string_view text) {
    if ((text.size() & 1U) != 0U || text.size() > 8U * 1024U) return std::nullopt;
    const auto nibble = [](const char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    std::string decoded;
    decoded.resize(text.size() / 2U);
    for (std::size_t index = 0U; index < decoded.size(); ++index) {
        const auto high = nibble(text[index * 2U]);
        const auto low = nibble(text[index * 2U + 1U]);
        if (high < 0 || low < 0) return std::nullopt;
        decoded[index] = static_cast<char>((high << 4) | low);
    }
    return decoded;
}

[[nodiscard]] bool apply_live_updates(
    ManifestModel& model,
    const std::span<const LiveUpdate> updates,
    std::vector<std::size_t>* const changed_value_indices) {
    bool changed{};
    for (const auto& update : updates) {
        const auto found = model.value_indices.find(object_key(update.domain, update.item));
        if (found == model.value_indices.end()) continue;
        auto& value = model.values[found->second];
        if (value.text == update.value) continue;
        const auto data = mms::MmsSimulatorManifestCodec::data(
            value.type, value.raw_type, value.normalized_type, update.value);
        if (!data.has_value()) continue;
        value.text = update.value;
        value.data = data;
        value.encoded = mms::MmsDataCodec::encode(*value.data);
        if (changed_value_indices != nullptr) changed_value_indices->push_back(found->second);
        changed = true;
    }
    if (changed) rebuild_manifest_root_values(model);
    return changed;
}

void update_live_control_state(ManifestModel& model, const LiveUpdate& update) {
    for (auto& control : model.direct_control_storage) {
        if (control.shared_state == nullptr || control.domain != update.domain ||
            control.status_item != update.item) {
            continue;
        }
        const auto normalized = update.value == "true" || update.value == "1" ||
            update.value == "on" || update.value == "TRUE";
        control.shared_state->value.store(normalized ? 1U : 0U, std::memory_order_relaxed);
    }
}

[[nodiscard]] int read_live_stdin(std::array<char, 4096U>& bytes) noexcept {
#if defined(_WIN32)
    const auto handle = ::GetStdHandle(STD_INPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) return -1;
    DWORD available{};
    if (!::PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) return -1;
    if (available == 0U) return 0;
    DWORD read{};
    const auto wanted = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), available));
    if (!::ReadFile(handle, bytes.data(), wanted, &read, nullptr)) return -1;
    return static_cast<int>(read);
#else
    fd_set read_set;
    FD_ZERO(&read_set);
    FD_SET(STDIN_FILENO, &read_set);
    timeval timeout{};
    const auto ready = ::select(STDIN_FILENO + 1, &read_set, nullptr, nullptr, &timeout);
    if (ready == 0) return 0;
    if (ready < 0) return errno == EINTR ? 0 : -1;
    const auto count = ::read(STDIN_FILENO, bytes.data(), bytes.size());
    if (count <= 0) return -1;
    return static_cast<int>(count);
#endif
}

void reject_live_update(
    const std::uint64_t generation,
    const std::uint64_t revision,
    const std::string_view reason) {
    std::osyncstream{std::cout}
        << "IEDSIM_EVENT kind=live_update_ack generation=" << generation
        << " revision=" << revision << " accepted=false reason=" << reason << '\n';
}

void drain_live_stdin(
    LiveInputState& input,
    const CliOptions& options,
    ManifestModel& model,
    LiveUpdateBus& bus) {
    if (input.eof) return;
    std::array<char, 4096U> bytes{};
    const auto count = read_live_stdin(bytes);
    if (count < 0) {
        input.eof = true;
        return;
    }
    if (count > 0) {
        input.buffer.append(bytes.data(), static_cast<std::size_t>(count));
        if (input.buffer.size() > 64U * 1024U) {
            input.buffer.clear();
            if (!input.overflow_reported) {
                input.overflow_reported = true;
                std::osyncstream{std::cerr}
                    << "IEDSIM_EVENT kind=live_input_overflow limit=65536\n";
            }
            return;
        }
    }

    std::size_t drained{};
    while (drained < 64U) {
        const auto newline = input.buffer.find('\n');
        if (newline == std::string::npos) break;
        auto line = input.buffer.substr(0U, newline);
        input.buffer.erase(0U, newline + 1U);
        ++drained;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() > 12U * 1024U) {
            reject_live_update(options.live_generation, 0U, "line-too-large");
            continue;
        }
        const auto fields = split_fields(line, '\t');
        if (fields.size() != 7U || fields[0] != "ARSTACK_LIVE" || fields[1] != "1") {
            reject_live_update(options.live_generation, 0U, "malformed");
            continue;
        }
        std::size_t generation_consumed{};
        std::size_t revision_consumed{};
        std::uint64_t generation{};
        std::uint64_t revision{};
        try {
            generation = std::stoull(fields[2], &generation_consumed, 10);
            revision = std::stoull(fields[3], &revision_consumed, 10);
        } catch (...) {
            reject_live_update(options.live_generation, 0U, "bad-revision");
            continue;
        }
        if (generation_consumed != fields[2].size() || revision_consumed != fields[3].size() ||
            generation != options.live_generation || revision == 0U || revision <= input.last_revision) {
            reject_live_update(generation, revision, "stale-generation-or-revision");
            continue;
        }
        const auto domain = decode_hex(fields[4]);
        const auto item = decode_hex(fields[5]);
        const auto value = decode_hex(fields[6]);
        if (!domain.has_value() || !item.has_value() || !value.has_value() ||
            domain->empty() || item->empty() || value->size() > 4U * 1024U) {
            reject_live_update(generation, revision, "bad-payload");
            continue;
        }
        const auto found = model.value_indices.find(object_key(*domain, *item));
        if (found == model.value_indices.end()) {
            reject_live_update(generation, revision, "unknown-object");
            continue;
        }
        auto& target = model.values[found->second];
        const auto parsed = mms::MmsSimulatorManifestCodec::data(
            target.type, target.raw_type, target.normalized_type, *value);
        if (!parsed.has_value()) {
            reject_live_update(generation, revision, "invalid-value");
            continue;
        }

        LiveUpdate update;
        update.generation = generation;
        update.revision = revision;
        update.domain = *domain;
        update.item = *item;
        update.value = *value;
        std::uint64_t sequence{};
        if (!bus.publish(update, &sequence)) {
            reject_live_update(generation, revision, "stale-generation-or-revision");
            continue;
        }
        target.text = update.value;
        target.data = parsed;
        target.encoded = mms::MmsDataCodec::encode(*target.data);
        rebuild_manifest_root_values(model);
        update_live_control_state(model, update);
        input.last_revision = revision;
        std::osyncstream{std::cout}
            << "IEDSIM_EVENT kind=live_update_ack generation=" << generation
            << " revision=" << revision << " accepted=true sequence=" << sequence << '\n';
    }
}

'''
    if text.count(helper_anchor) != 1:
        raise RuntimeError("server live helper anchor mismatch")
    text = text.replace(helper_anchor, live_helpers + helper_anchor, 1)

    text = replace_once(
        text,
        "void serve_connection(\n"
        "    const NativeSocket socket,\n"
        "    const mms::MmsStaticObjectTable& object_table,\n"
        "    const mms::MmsStaticDataSetTable& data_sets,\n"
        "    ManifestModel* const manifest_model,\n"
        "    const std::uint64_t association_id,\n"
        "    const std::string_view remote) {\n",
        "void serve_connection(\n"
        "    const NativeSocket socket,\n"
        "    const mms::MmsStaticObjectTable& object_table,\n"
        "    const mms::MmsStaticDataSetTable& data_sets,\n"
        "    ManifestModel* const manifest_model,\n"
        "    LiveUpdateBus* const live_updates,\n"
        "    std::uint64_t live_sequence,\n"
        "    const std::uint64_t association_id,\n"
        "    const std::string_view remote) {\n",
        "server serve signature",
    )
    old_refresh = r'''        if (manifest_model != nullptr && now >= next_model_refresh) {
            next_model_refresh = now + std::chrono::milliseconds{25};
            try {
                const auto changed = refresh_manifest_values(
                    *manifest_model, &changed_value_indices);
                if (changed != 0U) {
                    // Object-bank topology, callback contexts and MMS type
                    // specifications are structural and remain immutable after
                    // association setup. ManifestValue callbacks read the
                    // updated encoded payload directly, so reinitializing a
                    // composed bank here is both unnecessary and unsafe: a
                    // nested bank can copy aliases from its own storage while
                    // it is being rebuilt. Notify report runtimes only.
                    notify_brcb_changes(
                        *manifest_model,
                        changed_value_indices,
                        brcb_runtimes,
                        monotonic_ms());
                    std::osyncstream{std::cout}
                        << "IEDSIM_EVENT kind=value_sync association="
                        << association_id << " changed=" << changed
                        << " revision=" << manifest_model->revision << '\n';
                }
            } catch (const std::exception& exception) {
                std::osyncstream{std::cerr}
                    << "IEDSIM_EVENT kind=value_sync_error association="
                    << association_id << " message=" << exception.what() << '\n';
            }
        }
'''
    new_refresh = r'''        if (manifest_model != nullptr && now >= next_model_refresh) {
            next_model_refresh = now + std::chrono::milliseconds{25};
            try {
                changed_value_indices.clear();
                const auto manifest_changed = refresh_manifest_values(
                    *manifest_model, &changed_value_indices);
                bool live_changed{};
                bool resync{};
                if (live_updates != nullptr) {
                    LiveUpdateCollection collection;
                    if (manifest_changed != 0U) {
                        // A legacy manifest refresh may contain stale values for
                        // objects already changed through the hot channel. Reapply
                        // the bounded latest-state overlay before notifying reports.
                        collection = live_updates->latestSnapshot();
                    } else {
                        collection = live_updates->collectSince(live_sequence);
                    }
                    if (!collection.updates.empty()) {
                        live_changed = apply_live_updates(
                            *manifest_model, collection.updates, &changed_value_indices);
                    }
                    live_sequence = collection.sequence;
                    resync = collection.resync;
                }
                if (!changed_value_indices.empty()) {
                    std::sort(changed_value_indices.begin(), changed_value_indices.end());
                    changed_value_indices.erase(
                        std::unique(changed_value_indices.begin(), changed_value_indices.end()),
                        changed_value_indices.end());
                    notify_brcb_changes(
                        *manifest_model,
                        changed_value_indices,
                        brcb_runtimes,
                        monotonic_ms());
                }
                if (manifest_changed != 0U) {
                    std::osyncstream{std::cout}
                        << "IEDSIM_EVENT kind=value_sync association="
                        << association_id << " changed=" << manifest_changed
                        << " revision=" << manifest_model->revision << '\n';
                }
                if (live_changed) {
                    std::osyncstream{std::cout}
                        << "IEDSIM_EVENT kind=live_value_sync association="
                        << association_id << " changed=" << changed_value_indices.size()
                        << " sequence=" << live_sequence
                        << " resync=" << (resync ? "true" : "false") << '\n';
                }
            } catch (const std::exception& exception) {
                std::osyncstream{std::cerr}
                    << "IEDSIM_EVENT kind=value_sync_error association="
                    << association_id << " message=" << exception.what() << '\n';
            }
        }
'''
    text = replace_once(text, old_refresh, new_refresh, "server association hot refresh")

    text = replace_once(
        text,
        "        std::vector<WorkerSlot> workers(options.maximum_active_connections);\n"
        "        std::size_t connection_count = 0U;\n"
        "        while (!g_stop.load(std::memory_order_relaxed) &&\n",
        "        LiveUpdateBus live_updates{options.live_generation};\n"
        "        LiveInputState live_input;\n"
        "        std::vector<WorkerSlot> workers(options.maximum_active_connections);\n"
        "        std::size_t connection_count = 0U;\n"
        "        while (!g_stop.load(std::memory_order_relaxed) &&\n",
        "server main live state",
    )
    text = replace_once(
        text,
        "                connection_count < options.maximum_connections)) {\n"
        "            auto* worker = available_worker(workers);\n",
        "                connection_count < options.maximum_connections)) {\n"
        "            if (options.live_stdin) {\n"
        "                drain_live_stdin(live_input, options, manifest_model, live_updates);\n"
        "            }\n"
        "            auto* worker = available_worker(workers);\n",
        "server drain main stdin",
    )
    text = replace_once(
        text,
        "            const auto readiness = wait_socket(listener, true, 200U);\n",
        "            const auto readiness = wait_socket(\n"
        "                listener, true, options.live_stdin ? 25U : 200U);\n",
        "server live listener latency",
    )
    text = replace_once(
        text,
        "                &manifest_model,\n"
        "                &object_table,\n"
        "                &data_sets,\n",
        "                &manifest_model,\n"
        "                &live_updates,\n"
        "                &object_table,\n"
        "                &data_sets,\n",
        "server worker capture live bus",
    )
    old_local = r'''                        const auto local_object_span =
                            std::span<const mms::MmsStaticObjectEntry>{local_model.objects};
'''
    new_local = r'''                        std::uint64_t live_sequence{};
                        if (options.live_stdin) {
                            const auto overlay = live_updates.latestSnapshot();
                            std::vector<std::size_t> overlay_indices;
                            overlay_indices.reserve(overlay.updates.size());
                            static_cast<void>(apply_live_updates(
                                local_model, overlay.updates, &overlay_indices));
                            live_sequence = overlay.sequence;
                        }

                        const auto local_object_span =
                            std::span<const mms::MmsStaticObjectEntry>{local_model.objects};
'''
    text = replace_once(text, old_local, new_local, "server connection initial overlay")
    text = replace_once(
        text,
        "                        serve_connection(\n"
        "                            client,\n"
        "                            local_object_table,\n"
        "                            local_data_sets,\n"
        "                            &local_model,\n"
        "                            association_id,\n"
        "                            remote);\n",
        "                        serve_connection(\n"
        "                            client,\n"
        "                            local_object_table,\n"
        "                            local_data_sets,\n"
        "                            &local_model,\n"
        "                            options.live_stdin ? &live_updates : nullptr,\n"
        "                            live_sequence,\n"
        "                            association_id,\n"
        "                            remote);\n",
        "server manifest serve live args",
    )
    text = replace_once(
        text,
        "                        serve_connection(\n"
        "                            client,\n"
        "                            object_table,\n"
        "                            data_sets,\n"
        "                            nullptr,\n"
        "                            association_id,\n"
        "                            remote);\n",
        "                        serve_connection(\n"
        "                            client,\n"
        "                            object_table,\n"
        "                            data_sets,\n"
        "                            nullptr,\n"
        "                            nullptr,\n"
        "                            0U,\n"
        "                            association_id,\n"
        "                            remote);\n",
        "server fallback serve live args",
    )
    return text


def gui_test(text: str) -> str:
    text = replace_once(
        text,
        "                mapped_value = \"TCTR1$MX$Amp$instMag$i\\tINT32\\tNumber\\t42\"\n",
        "                mapped_value = \"TCTR1$MX$Amp$instMag$i\\tINT32\\tNumber\\t0\"\n",
        "gui test initial manifest value",
    )
    text = replace_once(
        text,
        "                    manifest_text.startswith(\"ARSTACK_IED_MODEL\\t2\\t2\\n\")\n",
        "                    manifest_text.startswith(\"ARSTACK_IED_MODEL\\t2\\t1\\n\")\n",
        "gui test no hot manifest rewrite",
    )
    text = replace_once(
        text,
        "                    \"GUI did not publish revision 2 with edited/full model leaves, reporting metadata, and configured control metadata\"\n",
        "                    \"GUI did not keep the startup manifest at revision 1 while preserving full model/report/control metadata\"\n",
        "gui test error wording",
    )
    text = replace_once(
        text,
        "            app.wait(timeout=47)\n"
        "            print(\n"
        "                \"IEDSIM_GUI_LIVE_VALUE_PASS \"\n",
        "            app.wait(timeout=47)\n"
        "            app_log.flush()\n"
        "            app_log.seek(0)\n"
        "            app_output = app_log.read()\n"
        "            if \"kind=live_update_ack\" not in app_output or \"accepted=true\" not in app_output:\n"
        "                raise RuntimeError(\"GUI edit was not acknowledged by the live runtime data plane\")\n"
        "            print(\n"
        "                \"IEDSIM_GUI_LIVE_VALUE_PASS \"\n"
        "                \"hot_delta=acknowledged manifest_hot_rewrites=0 \"\n",
        "gui test hot ack evidence",
    )
    return text


update("apps/ied_simulator/src/IedFleetController.hpp", header)
update("apps/ied_simulator/src/IedFleetController.cpp", controller)
update("apps/ied_simulator/src/main.cpp", main_cpp)
update("tools/static_ied_server.cpp", server)
update("apps/ied_simulator/test_gui_live_value.py", gui_test)
