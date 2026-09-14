// SPDX-License-Identifier: GPL-3.0-or-later
#include "SclWorkspaceController.hpp"

#include "ariec61850/scl/parser.hpp"

#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <utility>

namespace scl = ar::iec61850::scl;

namespace {
constexpr qint64 maximumSclBytes = 64LL * 1024LL * 1024LL;

QString localPath(const QUrl& value) {
    if (value.isLocalFile()) return value.toLocalFile();
    return value.toString();
}

QString editionKey(const scl::SclEdition edition) {
    switch (edition) {
    case scl::SclEdition::edition1: return QStringLiteral("ed1");
    case scl::SclEdition::edition2: return QStringLiteral("ed2");
    case scl::SclEdition::edition21: return QStringLiteral("ed2.1");
    case scl::SclEdition::unknown: return QStringLiteral("unknown");
    }
    return QStringLiteral("unknown");
}

QString editionLabel(const scl::SclEdition edition) {
    switch (edition) {
    case scl::SclEdition::edition1: return QStringLiteral("Edition 1");
    case scl::SclEdition::edition2: return QStringLiteral("Edition 2");
    case scl::SclEdition::edition21: return QStringLiteral("Edition 2.1");
    case scl::SclEdition::unknown: return QStringLiteral("Unknown edition");
    }
    return QStringLiteral("Unknown edition");
}

bool modeledSemanticsEquivalent(scl::SclDocument left, scl::SclDocument right) {
    // Source name is intentionally path-derived and changes on Save As. Every
    // modeled IEC 61850 semantic retained by SclDocument must remain identical.
    left.source_name.clear();
    right.source_name.clear();
    return left == right;
}

struct OpenResult final {
    QByteArray bytes;
    std::optional<scl::SclDocument> document;
    QString error;
};

OpenResult readAndParse(const QString& path, const std::stop_token stopToken) {
    OpenResult result;
    if (stopToken.stop_requested()) {
        result.error = QStringLiteral("SCL operation cancelled.");
        return result;
    }

    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Unable to open SCL file: %1").arg(file.errorString());
        return result;
    }
    if (file.size() < 0 || file.size() > maximumSclBytes) {
        result.error = QStringLiteral("SCL file exceeds the bounded 64 MiB workspace limit.");
        return result;
    }
    result.bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        result.error = QStringLiteral("Unable to read complete SCL file: %1").arg(file.errorString());
        result.bytes.clear();
        return result;
    }
    if (stopToken.stop_requested()) {
        result.error = QStringLiteral("SCL operation cancelled.");
        result.bytes.clear();
        return result;
    }

    try {
        scl::SclParser parser;
        const std::string_view xml{result.bytes.constData(), static_cast<std::size_t>(result.bytes.size())};
        result.document = parser.parse(xml, QFileInfo(path).fileName().toStdString());
    } catch (const std::exception& exception) {
        result.error = QString::fromUtf8(exception.what());
        result.bytes.clear();
    }
    return result;
}
} // namespace

SclWorkspaceController::SclWorkspaceController(QObject* parent)
    : QObject(parent), stopSource_(std::make_shared<std::stop_source>()) {
    ioPool_.setMaxThreadCount(1);
    ioPool_.setExpiryTimeout(-1);
}

SclWorkspaceController::~SclWorkspaceController() {
    if (stopSource_) stopSource_->request_stop();
    ioPool_.clear();
    ioPool_.waitForDone();
}

QString SclWorkspaceController::stateText() const {
    if (busy_) return operationText_.isEmpty() ? QStringLiteral("Working") : operationText_;
    if (!lastError_.isEmpty() && !loaded()) return QStringLiteral("SCL faulted");
    return loaded() ? QStringLiteral("SCL ready") : QStringLiteral("No SCL loaded");
}

QString SclWorkspaceController::editionText() const {
    return document_ ? editionLabel(document_->edition) : QStringLiteral("—");
}

QString SclWorkspaceController::namespaceUri() const {
    return document_ ? QString::fromStdString(document_->namespace_uri) : QString{};
}

QString SclWorkspaceController::headerId() const {
    return document_ ? QString::fromStdString(document_->header_id) : QString{};
}

QString SclWorkspaceController::headerVersion() const {
    return document_ ? QString::fromStdString(document_->header_version) : QString{};
}

QString SclWorkspaceController::headerRevision() const {
    return document_ ? QString::fromStdString(document_->header_revision) : QString{};
}

int SclWorkspaceController::iedCount() const noexcept {
    return document_ ? static_cast<int>(document_->ieds.size()) : 0;
}

int SclWorkspaceController::logicalNodeCount() const noexcept {
    return document_ ? static_cast<int>(document_->logical_nodes.size()) : 0;
}

int SclWorkspaceController::modelLeafCount() const noexcept {
    return document_ ? static_cast<int>(document_->model_entries.size()) : 0;
}

int SclWorkspaceController::dataSetCount() const noexcept {
    return document_ ? static_cast<int>(document_->data_sets.size()) : 0;
}

int SclWorkspaceController::reportCount() const noexcept {
    return document_ ? static_cast<int>(document_->report_controls.size()) : 0;
}

int SclWorkspaceController::gooseCount() const noexcept {
    return document_ ? static_cast<int>(document_->goose_streams.size()) : 0;
}

int SclWorkspaceController::smvCount() const noexcept {
    return document_ ? static_cast<int>(document_->sampled_values_streams.size()) : 0;
}

QStringList SclWorkspaceController::preservationReport() const {
    QStringList report;
    if (!document_) {
        report << QStringLiteral("Open an SCL source to inspect export safety.");
        return report;
    }
    report << QStringLiteral("Exact source bytes can be saved with the same SCL profile/extension and verified by reparsing.");
    report << QStringLiteral("Modeled LD/LN/leaf, DataSet, Report, GOOSE and SMV semantics are round-trip checked after Save As.");
    report << QStringLiteral("Canonical reconstruction is intentionally disabled: raw Services, Communication topology, vendor extension XML and full DataTypeTemplates hierarchy are not all retained by SclDocument.");
    report << QStringLiteral("Ed1/Ed2/Ed2.1 conversion is fail-closed until deterministic reconstruction and conversion rules are proven.");
    for (const auto& warning : document_->warnings) {
        report << QStringLiteral("Parser: %1").arg(QString::fromStdString(warning));
    }
    for (const auto& conflict : document_->conflicts) {
        report << QStringLiteral("Conflict %1 [%2]: %3")
                      .arg(QString::fromStdString(conflict.kind),
                           QString::fromStdString(conflict.key),
                           QString::fromStdString(conflict.description));
    }
    return report;
}

std::shared_ptr<std::stop_source> SclWorkspaceController::replaceStopSource() {
    if (stopSource_) stopSource_->request_stop();
    stopSource_ = std::make_shared<std::stop_source>();
    return stopSource_;
}

void SclWorkspaceController::appendDiagnostic(const QString& text) {
    if (text.isEmpty()) return;
    constexpr qsizetype maximumDiagnostics = 96;
    while (diagnostics_.size() >= maximumDiagnostics) diagnostics_.removeFirst();
    diagnostics_.push_back(text);
    emit diagnosticsChanged();
}

void SclWorkspaceController::setFailure(const QString& text) {
    lastError_ = text;
    appendDiagnostic(QStringLiteral("ERROR · %1").arg(text));
    emit stateChanged();
}

QString SclWorkspaceController::diagnosticsText() const {
    return diagnostics_.join(QLatin1Char('\n'));
}

bool SclWorkspaceController::openFile(const QUrl& fileUrl) {
    if (busy_) return false;
    const auto path = localPath(fileUrl).trimmed();
    if (path.isEmpty()) {
        setFailure(QStringLiteral("A local SCL file path is required."));
        return false;
    }

    const auto generation = ++generation_;
    const auto stop = replaceStopSource();
    busy_ = true;
    operationText_ = QStringLiteral("Opening SCL");
    lastError_.clear();
    lastExportVerified_ = false;
    lastExportPath_.clear();
    emit stateChanged();
    emit exportChanged();

    QPointer<SclWorkspaceController> self{this};
    ioPool_.start([self, path, generation, stop] {
        auto result = readAndParse(path, stop->get_token());
        QMetaObject::invokeMethod(self, [self, path, generation, result = std::move(result)]() mutable {
            if (!self || generation != self->generation_) return;
            self->busy_ = false;
            self->operationText_.clear();
            if (!result.error.isEmpty() || !result.document) {
                self->lastError_ = result.error.isEmpty()
                    ? QStringLiteral("SCL parse did not produce a document.")
                    : result.error;
                self->appendDiagnostic(QStringLiteral("Open rejected · %1").arg(self->lastError_));
                emit self->stateChanged();
                return;
            }

            self->document_ = std::move(result.document);
            self->sourceBytes_ = std::move(result.bytes);
            self->sourcePath_ = path;
            const QFileInfo info(path);
            self->sourceName_ = info.fileName();
            self->sourceFormat_ = info.suffix().toLower();
            self->lastError_.clear();
            self->appendDiagnostic(QStringLiteral("Opened %1 · %2 · %3 bytes")
                .arg(self->sourceName_, self->editionText())
                .arg(self->sourceBytes_.size()));
            emit self->workspaceChanged();
            emit self->stateChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

bool SclWorkspaceController::saveAs(const QUrl& fileUrl, const QString& targetEdition) {
    if (busy_ || !document_) return false;
    const auto path = localPath(fileUrl).trimmed();
    if (path.isEmpty()) {
        setFailure(QStringLiteral("A local Save As path is required."));
        return false;
    }

    const auto requested = targetEdition.trimmed().toLower();
    const auto currentEdition = editionKey(document_->edition);
    if (!requested.isEmpty() && requested != QStringLiteral("preserve") && requested != currentEdition) {
        setFailure(QStringLiteral("Cross-edition conversion %1 -> %2 is not yet proven and is rejected fail-closed.")
            .arg(currentEdition, requested));
        return false;
    }

    const auto targetFormat = QFileInfo(path).suffix().toLower();
    if (targetFormat.isEmpty() || targetFormat != sourceFormat_) {
        setFailure(QStringLiteral("Profile relabel %1 -> %2 is not a verified reconstruction. Keep the source extension for source-preserving Save As.")
            .arg(sourceFormat_.isEmpty() ? QStringLiteral("unknown") : sourceFormat_,
                 targetFormat.isEmpty() ? QStringLiteral("none") : targetFormat));
        return false;
    }

    const auto generation = ++generation_;
    const auto stop = replaceStopSource();
    const auto bytes = sourceBytes_;
    const auto expectedDocument = *document_;
    busy_ = true;
    operationText_ = QStringLiteral("Verifying Save As");
    lastError_.clear();
    lastExportVerified_ = false;
    lastExportPath_.clear();
    emit stateChanged();
    emit exportChanged();

    QPointer<SclWorkspaceController> self{this};
    ioPool_.start([self, path, bytes, expectedDocument, generation, stop] {
        QString error;
        bool verified = false;
        if (stop->stop_requested()) {
            error = QStringLiteral("SCL Save As cancelled before write.");
        } else {
            QSaveFile output(path);
            if (!output.open(QIODevice::WriteOnly)) {
                error = QStringLiteral("Unable to open Save As target: %1").arg(output.errorString());
            } else if (output.write(bytes) != bytes.size()) {
                error = QStringLiteral("Unable to write complete SCL source: %1").arg(output.errorString());
                output.cancelWriting();
            } else if (stop->stop_requested()) {
                error = QStringLiteral("SCL Save As cancelled before commit.");
                output.cancelWriting();
            } else if (!output.commit()) {
                error = QStringLiteral("Unable to commit SCL Save As: %1").arg(output.errorString());
            } else {
                auto reread = readAndParse(path, stop->get_token());
                if (!reread.error.isEmpty() || !reread.document) {
                    error = reread.error.isEmpty()
                        ? QStringLiteral("Saved SCL could not be reparsed.")
                        : reread.error;
                } else if (reread.bytes != bytes) {
                    error = QStringLiteral("Saved SCL bytes differ from the source-preserving payload.");
                } else if (!modeledSemanticsEquivalent(expectedDocument, *reread.document)) {
                    error = QStringLiteral("Saved SCL failed modeled semantic round-trip validation.");
                } else {
                    verified = true;
                }
            }
        }

        QMetaObject::invokeMethod(self, [self, path, generation, error = std::move(error), verified] {
            if (!self || generation != self->generation_) return;
            self->busy_ = false;
            self->operationText_.clear();
            self->lastExportVerified_ = verified;
            self->lastExportPath_ = verified ? path : QString{};
            self->lastError_ = error;
            if (verified) {
                self->appendDiagnostic(QStringLiteral("Verified source-preserving Save As · %1").arg(path));
            } else {
                self->appendDiagnostic(QStringLiteral("Save As rejected · %1").arg(error));
            }
            emit self->exportChanged();
            emit self->stateChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

void SclWorkspaceController::cancelOperation() {
    if (stopSource_) stopSource_->request_stop();
    appendDiagnostic(QStringLiteral("Cancellation requested for active SCL operation."));
}
