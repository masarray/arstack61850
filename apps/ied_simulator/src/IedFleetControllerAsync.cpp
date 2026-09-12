// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedFleetController.hpp"

#include "ariec61850/scl/parser.hpp"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <exception>
#include <filesystem>
#include <memory>
#include <utility>

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

        QElapsedTimer timer;
        timer.start();
        try {
            result->document.emplace(ar::iec61850::scl::SclParser{}.load(
                std::filesystem::path{request.path.toStdWString()}));
        } catch (const std::exception& error) {
            result->error = QString::fromUtf8(error.what());
        } catch (...) {
            result->error = QStringLiteral("Unexpected failure while parsing the engineering model.");
        }
        result->elapsedMilliseconds = timer.elapsed();

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
            documents_.push_back(LoadedDocument{result->path, std::move(*result->document)});
            fatalError_.clear();
            previousValue_.reset();
            rebuildPresentation();
            appendActivity(
                QStringLiteral("Importer"),
                QStringLiteral("%1 imported in %2 ms without blocking the GUI parser path.")
                    .arg(sourceName_)
                    .arg(result->elapsedMilliseconds),
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
