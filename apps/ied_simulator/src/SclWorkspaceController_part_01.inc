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

bool SclWorkspaceController::reconstructionSupported() const noexcept {
    if (!document_ || document_->edition == scl::SclEdition::unknown) return false;
    scl::SclCanonicalExportOptions options;
    options.edition = document_->edition;
    options.profile = scl::SclExportProfile::scd;
    return scl::SclExporter::validate(*document_, options).success;
}

bool SclWorkspaceController::editionConversionSupported() const noexcept {
    return reconstructionSupported();
}

bool SclWorkspaceController::profileConversionSupported() const noexcept {
    if (!document_ || document_->ieds.size() != 1U || !reconstructionSupported()) return false;
    scl::SclCanonicalExportOptions options;
    options.edition = document_->edition;
    options.profile = scl::SclExportProfile::cid;
    return scl::SclExporter::validate(*document_, options).success;
}

QStringList SclWorkspaceController::preservationReport() const {
    QStringList report;
    if (!document_) {
        report << QStringLiteral("Open an SCL source to inspect export safety.");
        return report;
    }
    report << QStringLiteral("Exact Save As preserves the original bytes and is allowed only with the same source extension/profile and edition.");
    if (reconstructionSupported()) {
        report << QStringLiteral("Canonical reconstruction is enabled for the resolved ARStack model: LD/LN/DO/DA(BDA), DataTypeTemplates, configured DAI values, DataSets, Reports, GOOSE/SMV controls and modeled Communication addresses.");
        report << QStringLiteral("Canonical output is reparsed and semantic-round-trip verified before success is reported; repeated exports are deterministic for the same model/options.");
        report << QStringLiteral("Ed1, Ed2 and Ed2.1 conversion is available only through canonical reconstruction, never by namespace/version string relabel.");
        report << QStringLiteral("SCD export supports the modeled IED set; ICD/CID conversion is guarded to exactly one IED.");
    } else {
        scl::SclCanonicalExportOptions options;
        options.edition = document_->edition;
        options.profile = scl::SclExportProfile::scd;
        const auto validation = scl::SclExporter::validate(*document_, options);
        report << QStringLiteral("Canonical reconstruction is fail-closed for this source: %1")
                      .arg(QString::fromStdString(validation.error));
    }
    report << QStringLiteral("Canonical export is normalized, not vendor-lossless: unmodeled Services/Substation content and unknown vendor extension XML remain available only through exact source preservation.");
    report << QStringLiteral("Ambiguous nested SDO parent typing and non-ctlModel EnumType ordinal domains fail closed rather than invent engineering values.");
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
    lastExportMode_.clear();
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
