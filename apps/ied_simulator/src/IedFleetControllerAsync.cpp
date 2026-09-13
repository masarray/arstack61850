// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedFleetController.hpp"

#include "ariec61850/scl/parser.hpp"

#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <exception>
#include <filesystem>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace {
QString qstring(const std::string& value) {
    return QString::fromStdString(value);
}
} // namespace

bool IedFleetController::importing() const noexcept {
    return asyncImportRunning_ || pendingAsyncImport_.has_value();
}

bool IedFleetController::loadFileAsync(const QUrl& fileUrl) {
    const auto path = fileUrl.toLocalFile();
    if (path.isEmpty()) {
        fatalError_ = QStringLiteral("Choose a local SCL, CID, SCD, IID, or ICD file.");
        emit modelChanged();
        return false;
    }

    if (anyRunning()) {
        appendActivity(
            QStringLiteral("Workspace"),
            QStringLiteral("Stop active IEDs before loading another engineering model."),
            QStringLiteral("Warning"));
        return false;
    }

    const auto generation = ++asyncImportGeneration_;
    PendingAsyncImport request{path, generation};

    sourcePath_ = path;
    sourceName_ = QFileInfo(path).fileName();
    fatalError_.clear();

    if (asyncImportRunning_) {
        pendingAsyncImport_ = std::move(request);
        appendActivity(
            QStringLiteral("Importer"),
            QStringLiteral("Queued %1; the older in-flight import will be discarded when it finishes.")
                .arg(sourceName_),
            QStringLiteral("Info"));
        emit modelChanged();
        return true;
    }

    // The interactive async path is replacement-only. Clear the old model now
    // so Start cannot launch stale IED state while a new SCL is still parsing.
    clear();
    sourcePath_ = path;
    sourceName_ = QFileInfo(path).fileName();
    fatalError_.clear();
    navigationIndex_.clear();
    valueScopeIndex_.clear();
    preparedIeds_.clear();
    preparedValueIndexIed_ = -1;
    lastImportWorkerMilliseconds_ = 0;
    lastGuiApplyMilliseconds_ = 0;
    preparedPointCount_ = 0;
    appendActivity(
        QStringLiteral("Importer"),
        QStringLiteral("Loading %1 on the bounded background importer.").arg(sourceName_),
        QStringLiteral("Info"));
    launchAsyncImport(std::move(request));
    emit modelChanged();
    return true;
}

void IedFleetController::launchAsyncImport(PendingAsyncImport request) {
    importPool_.setMaxThreadCount(1);
    importPool_.setExpiryTimeout(-1);
    asyncImportRunning_ = true;

    QPointer<IedFleetController> guard{this};
    importPool_.start([guard, request = std::move(request)]() mutable {
        auto result = std::make_shared<AsyncImportResult>();
        result->path = request.path;
        result->generation = request.generation;

        QElapsedTimer totalTimer;
        totalTimer.start();
        try {
            QElapsedTimer parserTimer;
            parserTimer.start();
            result->document.emplace(ar::iec61850::scl::SclParser{}.load(
                std::filesystem::path{request.path.toStdWString()}));
            result->parserMilliseconds = parserTimer.elapsed();

            // Parser output, profile expansion, typed canonical points, LD/LN
            // navigation and per-LN source indexes are built on the one bounded
            // worker. GUI completion only moves ownership of these containers.
            QElapsedTimer preparationTimer;
            preparationTimer.start();
            const auto& document = *result->document;

            result->dataSetCount = static_cast<int>(document.data_sets.size());
            result->reportCount = static_cast<int>(document.report_controls.size());
            result->gooseCount = static_cast<int>(document.goose_streams.size());

            std::set<QString> logicalDevices;
            std::set<QString> dataObjects;
            std::set<QString> dataAttributes;
            const auto collectEntry = [&](const ar::iec61850::scl::SclDataSetEntry& entry) {
                const auto ld = qstring(entry.ied_name) + QLatin1Char('/') + qstring(entry.ld_inst);
                logicalDevices.insert(ld);
                const auto logicalNode =
                    qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst);
                const auto object = ld + QLatin1Char('/') + logicalNode +
                    QLatin1Char('/') + qstring(entry.do_name);
                dataObjects.insert(object);
                dataAttributes.insert(object + QLatin1Char('/') + qstring(entry.da_name));
            };
            if (!document.model_entries.empty()) {
                for (const auto& entry : document.model_entries) collectEntry(entry);
            } else {
                for (const auto& dataSet : document.data_sets) {
                    for (const auto& entry : dataSet.entries) collectEntry(entry);
                }
                for (const auto& stream : document.goose_streams) {
                    for (const auto& entry : stream.entries) collectEntry(entry);
                }
                for (const auto& report : document.report_controls) {
                    for (const auto& entry : report.entries) collectEntry(entry);
                }
            }
            result->logicalDeviceCount = static_cast<int>(logicalDevices.size());
            result->dataObjectCount = static_cast<int>(dataObjects.size());
            result->dataAttributeCount = static_cast<int>(dataAttributes.size());

            for (const auto& ied : document.ieds) {
                const auto name = qstring(ied.name);
                QVariantMap item;
                item.insert(QStringLiteral("name"), name);
                item.insert(QStringLiteral("manufacturer"), qstring(ied.manufacturer));
                item.insert(QStringLiteral("type"), qstring(ied.type));
                item.insert(QStringLiteral("configVersion"), qstring(ied.config_version));
                item.insert(QStringLiteral("documentIndex"), 0);
                item.insert(QStringLiteral("sourcePath"), result->path);
                item.insert(
                    QStringLiteral("sessionKey"),
                    result->path + QLatin1Char('\x1f') + name);
                result->ieds.push_back(item);
            }

            if (result->ieds.isEmpty()) {
                QVariantMap fallback;
                const auto name = QFileInfo(result->path).completeBaseName();
                fallback.insert(QStringLiteral("name"), name);
                fallback.insert(QStringLiteral("manufacturer"), QStringLiteral("SCL model"));
                fallback.insert(QStringLiteral("type"), QStringLiteral("IED"));
                fallback.insert(QStringLiteral("configVersion"), QString{});
                fallback.insert(QStringLiteral("documentIndex"), 0);
                fallback.insert(QStringLiteral("sourcePath"), result->path);
                fallback.insert(
                    QStringLiteral("sessionKey"),
                    result->path + QLatin1Char('\x1f') + name);
                result->ieds.push_back(fallback);
            }

            result->preparedIeds.resize(result->ieds.size());
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
                        logicalDevice + QLatin1Char('\x1f') + logicalNode;
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
            result->preparationMilliseconds = preparationTimer.elapsed();
        } catch (const std::exception& error) {
            result->document.reset();
            result->error = QString::fromUtf8(error.what());
        } catch (...) {
            result->document.reset();
            result->error = QStringLiteral("Unexpected failure while parsing the engineering model.");
        }
        result->elapsedMilliseconds = totalTimer.elapsed();

        if (guard.isNull()) return;
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, result = std::move(result)] {
                if (!guard.isNull()) guard->finishAsyncImport(result);
            },
            Qt::QueuedConnection);
    });
}

void IedFleetController::finishAsyncImport(
    const std::shared_ptr<AsyncImportResult>& result) {
    asyncImportRunning_ = false;

    // A clear(), synchronous load, or newer async request changes sourcePath_
    // and/or the generation. In all of those cases this completed result is
    // intentionally stale and must never overwrite the newer workspace state.
    const bool latest = result != nullptr &&
        result->generation == asyncImportGeneration_ &&
        sourcePath_ == result->path;

    if (latest) {
        if (result->document.has_value()) {
            QElapsedTimer applyTimer;
            applyTimer.start();

            documents_.push_back(LoadedDocument{result->path, std::move(*result->document)});
            fatalError_.clear();
            previousValue_.reset();

            // These are move/adoption operations. No per-point QVariant map is
            // reconstructed on the GUI thread for the interactive Open path.
            ieds_ = std::move(result->ieds);
            pointStore_ = std::move(result->pointStore);
            selectedPointIndices_ = std::move(result->selectedPointIndices);
            navigationIndex_ = std::move(result->navigationIndex);
            valueScopeIndex_ = std::move(result->valueScopeIndex);
            preparedIeds_ = std::move(result->preparedIeds);
            logicalDeviceCount_ = result->logicalDeviceCount;
            dataObjectCount_ = result->dataObjectCount;
            dataAttributeCount_ = result->dataAttributeCount;
            dataSetCount_ = result->dataSetCount;
            reportCount_ = result->reportCount;
            gooseCount_ = result->gooseCount;
            selectedIedIndex_ = ieds_.isEmpty() ? -1 : 0;
            selectedValueIndex_ = selectedPointIndices_.isEmpty() ? -1 : 0;
            preparedValueIndexIed_ = selectedIedIndex_;
            seededRuntimeIeds_.clear();
            for (int index = 0; index < preparedIeds_.size() && index < ieds_.size(); ++index) {
                seededRuntimeIeds_.insert(
                    ieds_.at(index).toMap().value(QStringLiteral("sessionKey")).toString());
            }
            rebuildRuntimeInstances({});

            lastImportWorkerMilliseconds_ = result->elapsedMilliseconds;
            preparedPointCount_ = result->preparedPointCount;
            const bool selectedProjectionReady =
                selectedIedIndex_ < 0 || adoptPreparedIed(selectedIedIndex_);
            lastGuiApplyMilliseconds_ = applyTimer.elapsed();

            if (!selectedProjectionReady) {
                fatalError_ = QStringLiteral(
                    "The bounded importer returned an incomplete prepared IED projection.");
                selectedIedIndex_ = -1;
                selectedValueIndex_ = -1;
                selectedPointIndices_.clear();
                navigationIndex_.clear();
                valueScopeIndex_.clear();
                preparedValueIndexIed_ = -1;
                preparedPointCount_ = 0;
                appendActivity(
                    QStringLiteral("Importer"),
                    fatalError_,
                    QStringLiteral("Error"));
            }

            emit valuesChanged();
            emit modelChanged();
            emit selectionChanged();
            emit configurationChanged();
            emit runtimeChanged();

            if (selectedProjectionReady) {
                qInfo().noquote() << QStringLiteral(
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
                    QStringLiteral("Success"));
            }
        } else {
            fatalError_ = result->error.isEmpty()
                ? QStringLiteral("The engineering model could not be parsed.")
                : result->error;
            appendActivity(
                QStringLiteral("Importer"),
                fatalError_,
                QStringLiteral("Error"));
            emit modelChanged();
        }
    }

    if (pendingAsyncImport_.has_value()) {
        auto next = std::move(*pendingAsyncImport_);
        pendingAsyncImport_.reset();

        // The newest request owns the visible source identity. Keep the old
        // parse result out of the UI and immediately reuse the single worker.
        sourcePath_ = next.path;
        sourceName_ = QFileInfo(next.path).fileName();
        fatalError_.clear();
        launchAsyncImport(std::move(next));
        emit modelChanged();
        return;
    }

    emit modelChanged();
}
