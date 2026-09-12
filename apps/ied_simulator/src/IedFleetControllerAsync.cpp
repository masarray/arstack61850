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

QVariantMap preparedValueMap(
    const ar::iec61850::simulation::IedSimulatorPoint& point) {
    QVariantMap item;
    const auto dataObject = qstring(point.data_object);
    const auto dataAttribute = qstring(point.data_attribute);
    item.insert(
        QStringLiteral("name"),
        dataAttribute.isEmpty() ? dataObject : dataObject + QLatin1Char('.') + dataAttribute);
    item.insert(QStringLiteral("reference"), qstring(point.reference));
    item.insert(QStringLiteral("logicalDevice"), qstring(point.logical_device));
    item.insert(QStringLiteral("logicalNode"), qstring(point.logical_node));
    item.insert(QStringLiteral("dataObject"), dataObject);
    item.insert(QStringLiteral("dataAttribute"), dataAttribute);
    item.insert(QStringLiteral("fc"), qstring(point.functional_constraint));
    item.insert(QStringLiteral("cdc"), qstring(point.cdc));
    item.insert(QStringLiteral("type"), qstring(point.display_type));
    item.insert(QStringLiteral("rawType"), qstring(point.basic_type));
    item.insert(QStringLiteral("iedName"), qstring(point.ied_name));
    item.insert(QStringLiteral("mmsDomain"), qstring(point.mms_domain));
    item.insert(QStringLiteral("mmsItem"), qstring(point.mms_item));
    item.insert(QStringLiteral("value"), qstring(point.initial_value));
    item.insert(QStringLiteral("quality"), QStringLiteral("Good"));
    item.insert(QStringLiteral("origin"), QStringLiteral("Simulator"));
    item.insert(QStringLiteral("writable"), true);
    item.insert(QStringLiteral("changed"), false);
    item.insert(QStringLiteral("updated"), QStringLiteral("—"));
    if (point.display_type == "Enumeration" && point.cdc == "DPC") {
        item.insert(
            QStringLiteral("options"),
            QStringList{
                QStringLiteral("intermediate-state"),
                QStringLiteral("off"),
                QStringLiteral("on"),
                QStringLiteral("bad-state")});
    } else if (point.display_type == "Boolean") {
        item.insert(
            QStringLiteral("options"),
            QStringList{QStringLiteral("false"), QStringLiteral("true")});
    }
    return item;
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

            // The high-cardinality simulator profile, ordered point projection,
            // value maps and structural counts are all prepared on this one
            // bounded worker. GUI-thread completion only adopts implicitly
            // shared containers and creates the small per-IED runtime objects.
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

            if (!result->ieds.isEmpty()) {
                const auto selectedIed = result->ieds.constFirst().toMap();
                const auto selectedName = selectedIed.value(QStringLiteral("name")).toString();

                ar::iec61850::simulation::IedSimulatorProfileFromSclOptions options;
                options.ied_name = selectedName.toStdString();
                options.runtime_ied_name = options.ied_name;
                const auto built = ar::iec61850::simulation::IedSimulatorProfileBuilder::build(
                    document, options);

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

                result->selectedValues.reserve(static_cast<qsizetype>(points.size()));
                result->runtimeValues.reserve(static_cast<qsizetype>(points.size()));
                for (const auto* point : points) {
                    auto item = preparedValueMap(*point);
                    result->selectedValues.push_back(item);
                    const auto key = qstring(point->ied_name) + QLatin1Char('\x1f') +
                        qstring(point->reference);
                    result->runtimeValues.insert(key, std::move(item));
                }
                result->preparedPointCount = static_cast<int>(points.size());
            }
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

            // Adopt the worker-prepared projection. This deliberately bypasses
            // rebuildPresentation()/rebuildValues() for the interactive Open
            // path so IedSimulatorProfileBuilder and the O(N) point-index build
            // never execute on the GUI thread during a large import.
            ieds_ = std::move(result->ieds);
            values_ = std::move(result->selectedValues);
            runtimeValues_ = std::move(result->runtimeValues);
            logicalDeviceCount_ = result->logicalDeviceCount;
            dataObjectCount_ = result->dataObjectCount;
            dataAttributeCount_ = result->dataAttributeCount;
            dataSetCount_ = result->dataSetCount;
            reportCount_ = result->reportCount;
            gooseCount_ = result->gooseCount;
            selectedIedIndex_ = ieds_.isEmpty() ? -1 : 0;
            selectedValueIndex_ = values_.isEmpty() ? -1 : 0;
            rebuildRuntimeInstances({});

            lastImportWorkerMilliseconds_ = result->elapsedMilliseconds;
            preparedPointCount_ = result->preparedPointCount;
            lastGuiApplyMilliseconds_ = applyTimer.elapsed();

            emit valuesChanged();
            emit modelChanged();
            emit selectionChanged();
            emit configurationChanged();
            emit runtimeChanged();

            qInfo().noquote() << QStringLiteral(
                "IEDSIM_IMPORT_PATH worker_ms=%1 parser_ms=%2 prepare_ms=%3 gui_apply_ms=%4 points=%5")
                .arg(lastImportWorkerMilliseconds_)
                .arg(result->parserMilliseconds)
                .arg(result->preparationMilliseconds)
                .arg(lastGuiApplyMilliseconds_)
                .arg(preparedPointCount_);
            appendActivity(
                QStringLiteral("Importer"),
                QStringLiteral(
                    "%1 parsed and indexed on the bounded worker in %2 ms; GUI adoption took %3 ms.")
                    .arg(sourceName_)
                    .arg(lastImportWorkerMilliseconds_)
                    .arg(lastGuiApplyMilliseconds_),
                QStringLiteral("Success"));
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
