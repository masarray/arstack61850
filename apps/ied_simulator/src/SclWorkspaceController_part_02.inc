    if (path.isEmpty()) {
        setFailure(QStringLiteral("A local Save As path is required."));
        return false;
    }

    const auto requested = requestedEdition(targetEdition, document_->edition);
    if (!requested) {
        setFailure(QStringLiteral("Unsupported target edition selector."));
        return false;
    }
    if (*requested != document_->edition) {
        setFailure(QStringLiteral("Exact Save As cannot change edition. Use Canonical Export for %1 -> %2 conversion.")
            .arg(editionKey(document_->edition), editionKey(*requested)));
        return false;
    }

    const auto targetFormat = QFileInfo(path).suffix().toLower();
    if (targetFormat.isEmpty() || targetFormat != sourceFormat_) {
        setFailure(QStringLiteral("Exact Save As cannot relabel profile %1 -> %2. Use Canonical Export for SCD/ICD/CID conversion.")
            .arg(sourceFormat_.isEmpty() ? QStringLiteral("unknown") : sourceFormat_,
                 targetFormat.isEmpty() ? QStringLiteral("none") : targetFormat));
        return false;
    }

    const auto generation = ++generation_;
    const auto stop = replaceStopSource();
    const auto bytes = sourceBytes_;
    const auto expectedDocument = *document_;
    busy_ = true;
    operationText_ = QStringLiteral("Verifying exact Save As");
    lastError_.clear();
    lastExportVerified_ = false;
    lastExportPath_.clear();
    lastExportMode_.clear();
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
            self->lastExportMode_ = verified ? QStringLiteral("Exact source") : QString{};
            self->lastError_ = error;
            if (verified) {
                self->appendDiagnostic(QStringLiteral("Verified exact source Save As · %1").arg(path));
            } else {
                self->appendDiagnostic(QStringLiteral("Save As rejected · %1").arg(error));
            }
            emit self->exportChanged();
            emit self->stateChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

bool SclWorkspaceController::exportCanonical(const QUrl& fileUrl, const QString& targetEdition) {
    if (busy_ || !document_) return false;
    const auto path = localPath(fileUrl).trimmed();
    if (path.isEmpty()) {
        setFailure(QStringLiteral("A local canonical export path is required."));
        return false;
    }

    const auto edition = requestedEdition(targetEdition, document_->edition);
    if (!edition || *edition == scl::SclEdition::unknown) {
        setFailure(QStringLiteral("Canonical export requires Ed1, Ed2 or Ed2.1."));
        return false;
    }
    const auto profile = profileFromPath(path);
    if (!profile) {
        setFailure(QStringLiteral("Canonical export target must use .scd, .icd or .cid. Other SCL profiles remain fail-closed."));
        return false;
    }

    scl::SclCanonicalExportOptions options;
    options.edition = *edition;
    options.profile = *profile;
    const auto validation = scl::SclExporter::validate(*document_, options);
    if (!validation.success) {
        setFailure(QString::fromStdString(validation.error));
        return false;
    }

    const auto generation = ++generation_;
    const auto stop = replaceStopSource();
    const auto expectedDocument = *document_;
    busy_ = true;
    operationText_ = QStringLiteral("Reconstructing canonical SCL");
    lastError_.clear();
    lastExportVerified_ = false;
    lastExportPath_.clear();
    lastExportMode_.clear();
    emit stateChanged();
    emit exportChanged();

    QPointer<SclWorkspaceController> self{this};
    ioPool_.start([self, path, expectedDocument, options, generation, stop] {
        QString error;
        bool verified = false;
        QString warning;
        if (stop->stop_requested()) {
            error = QStringLiteral("Canonical SCL export cancelled before reconstruction.");
        } else {
            const auto generated = scl::SclExporter::canonical(expectedDocument, options);
            if (!generated.success) {
                error = QString::fromStdString(generated.error);
            } else if (generated.xml.size() > static_cast<std::size_t>(maximumSclBytes)) {
                error = QStringLiteral("Canonical SCL export exceeds the bounded 64 MiB workspace limit.");
            } else if (stop->stop_requested()) {
                error = QStringLiteral("Canonical SCL export cancelled before write.");
            } else {
                const QByteArray bytes{generated.xml.data(), static_cast<qsizetype>(generated.xml.size())};
                QSaveFile output(path);
                if (!output.open(QIODevice::WriteOnly)) {
                    error = QStringLiteral("Unable to open canonical export target: %1").arg(output.errorString());
                } else if (output.write(bytes) != bytes.size()) {
                    error = QStringLiteral("Unable to write complete canonical SCL: %1").arg(output.errorString());
                    output.cancelWriting();
                } else if (stop->stop_requested()) {
                    error = QStringLiteral("Canonical SCL export cancelled before commit.");
                    output.cancelWriting();
                } else if (!output.commit()) {
                    error = QStringLiteral("Unable to commit canonical SCL export: %1").arg(output.errorString());
                } else {
                    auto reread = readAndParse(path, stop->get_token());
                    if (!reread.error.isEmpty() || !reread.document) {
                        error = reread.error.isEmpty()
                            ? QStringLiteral("Canonical export could not be reparsed.")
                            : reread.error;
                    } else if (reread.bytes != bytes) {
                        error = QStringLiteral("Canonical export bytes changed after atomic commit.");
                    } else if (reread.document->edition != options.edition) {
                        error = QStringLiteral("Canonical export reparsed with the wrong IEC 61850 edition.");
                    } else if (!modeledSemanticsEquivalent(expectedDocument, *reread.document)) {
                        error = QStringLiteral("Canonical export failed modeled semantic round-trip validation.");
                    } else {
                        verified = true;
                        if (!generated.warnings.empty()) {
                            warning = QString::fromStdString(generated.warnings.front());
                        }
                    }
                }
            }
        }

        const auto mode = QStringLiteral("Canonical %1 %2")
            .arg(profileLabel(options.profile), editionKey(options.edition));
        QMetaObject::invokeMethod(self, [self, path, generation, error = std::move(error),
                                         warning = std::move(warning), mode, verified] {
            if (!self || generation != self->generation_) return;
            self->busy_ = false;
            self->operationText_.clear();
            self->lastExportVerified_ = verified;
            self->lastExportPath_ = verified ? path : QString{};
            self->lastExportMode_ = verified ? mode : QString{};
            self->lastError_ = error;
            if (verified) {
                self->appendDiagnostic(QStringLiteral("Verified %1 export · %2").arg(mode, path));
                if (!warning.isEmpty()) self->appendDiagnostic(QStringLiteral("NOTE · %1").arg(warning));
            } else {
                self->appendDiagnostic(QStringLiteral("Canonical export rejected · %1").arg(error));
            }
            emit self->exportChanged();
            emit self->stateChanged();
        }, Qt::QueuedConnection);
    });
    return true;
}

bool SclWorkspaceController::exportEngineeringContext(
    const QUrl& fileUrl,
    const QString& targetEdition) {
    if (busy_ || !engineeringContext_ || !engineeringContext_->loaded() ||
        engineeringContext_->selectionRequired()) {
        return false;
    }
    const auto* liveModel = engineeringContext_->modelSnapshot();
    if (liveModel == nullptr) {
        setFailure(QStringLiteral("No canonical IED model is available for Save SCL."));
        return false;
    }

    const auto path = localPath(fileUrl).trimmed();
    if (path.isEmpty()) {
        setFailure(QStringLiteral("A local Save SCL path is required."));
        return false;
    }
    const auto edition = requestedEdition(targetEdition, scl::SclEdition::edition2);
    if (!edition || *edition == scl::SclEdition::unknown) {
        setFailure(QStringLiteral("Save SCL requires Ed1, Ed2 or Ed2.1."));
        return false;
    }
    const auto profile = profileFromPath(path);
    if (!profile) {
        setFailure(QStringLiteral(
            "Save SCL target must use .iid, .icd, .cid or .scd."));
        return false;
    }

    const auto projection = scl::project_live_model_to_scl(*liveModel, *edition);
    if (!projection.success) {
        setFailure(QString::fromStdString(projection.error));
        return false;
    }

    scl::SclCanonicalExportOptions options;
    options.edition = *edition;
    options.profile = *profile;
    const auto validation = scl::SclExporter::validate(projection.document, options);
    if (!validation.success) {
        setFailure(QString::fromStdString(validation.error));
        return false;
    }

    const auto generation = ++generation_;
    const auto stop = replaceStopSource();
    const auto expectedDocument = projection.document;
    const auto projectionWarnings = projection.warnings;
    busy_ = true;
    operationText_ = QStringLiteral("Saving canonical IED model");
    lastError_.clear();
    lastExportVerified_ = false;
    lastExportPath_.clear();
    lastExportMode_.clear();
    emit stateChanged();
    emit exportChanged();

    QPointer<SclWorkspaceController> self{this};
    ioPool_.start([self, path, expectedDocument, projectionWarnings, options, generation, stop] {
        QString error;
        bool verified = false;
        QStringList warnings;
        for (const auto& warning : projectionWarnings) {
            warnings.push_back(QString::fromStdString(warning));
        }

        if (stop->stop_requested()) {
            error = QStringLiteral("Save SCL cancelled before local projection.");
        } else {
            const auto generated = scl::SclExporter::canonical(expectedDocument, options);
            if (!generated.success) {
                error = QString::fromStdString(generated.error);
            } else if (generated.xml.size() > static_cast<std::size_t>(maximumSclBytes)) {
                error = QStringLiteral("Save SCL exceeds the bounded 64 MiB workspace limit.");
            } else if (stop->stop_requested()) {
                error = QStringLiteral("Save SCL cancelled before write.");
            } else {
                const QByteArray bytes{
                    generated.xml.data(),
                    static_cast<qsizetype>(generated.xml.size())};
                QSaveFile output(path);
                if (!output.open(QIODevice::WriteOnly)) {
                    error = QStringLiteral("Unable to open Save SCL target: %1")
                                .arg(output.errorString());
                } else if (output.write(bytes) != bytes.size()) {
                    error = QStringLiteral("Unable to write complete Save SCL output: %1")
                                .arg(output.errorString());
                    output.cancelWriting();
                } else if (stop->stop_requested()) {
                    error = QStringLiteral("Save SCL cancelled before commit.");
                    output.cancelWriting();
                } else if (!output.commit()) {
                    error = QStringLiteral("Unable to commit Save SCL output: %1")
                                .arg(output.errorString());
                } else {
                    auto reread = readAndParse(path, stop->get_token());
                    if (!reread.error.isEmpty() || !reread.document) {
                        error = reread.error.isEmpty()
                            ? QStringLiteral("Saved discovery model could not be reparsed.")
                            : reread.error;
                    } else if (reread.document->edition != options.edition) {
                        error = QStringLiteral(
                            "Saved discovery model reparsed with the wrong IEC 61850 edition.");
                    } else if (!discoveredProjectionEquivalent(
                                   expectedDocument, *reread.document)) {
                        error = QStringLiteral(
                            "Saved discovery model failed semantic round-trip validation.");
                    } else {
                        verified = true;
                        for (const auto& warning : generated.warnings) {
                            warnings.push_back(QString::fromStdString(warning));
                        }
                    }
                }
            }
        }

        const auto mode = QStringLiteral("Discovery %1 %2")
            .arg(profileLabel(options.profile), editionKey(options.edition));
        QMetaObject::invokeMethod(
            self,
            [self, path, generation, error = std::move(error),
             warnings = std::move(warnings), mode, verified] {
                if (!self || generation != self->generation_) return;
                self->busy_ = false;
                self->operationText_.clear();
                self->lastExportVerified_ = verified;
                self->lastExportPath_ = verified ? path : QString{};
                self->lastExportMode_ = verified ? mode : QString{};
                self->lastError_ = error;
                if (verified) {
                    self->appendDiagnostic(
                        QStringLiteral("Verified %1 Save SCL · %2").arg(mode, path));
                    for (const auto& warning : warnings) {
                        self->appendDiagnostic(QStringLiteral("NOTE · %1").arg(warning));
                    }
                } else {
                    self->appendDiagnostic(
                        QStringLiteral("Save SCL rejected · %1").arg(error));
                }
                emit self->exportChanged();
                emit self->stateChanged();
            },
            Qt::QueuedConnection);
    });
    return true;
}

void SclWorkspaceController::cancelOperation() {
    if (stopSource_) stopSource_->request_stop();
    appendDiagnostic(QStringLiteral("Cancellation requested for active SCL operation."));
}
