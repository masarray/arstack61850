// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedSimulatorController.hpp"

#include "ariec61850/scl/parser.hpp"

#include <QCoreApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QNetworkInterface>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QXmlStreamReader>

#include <algorithm>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_set>

namespace {
QString qstring(const std::string& value) {
    return QString::fromStdString(value);
}

QString normalizedType(const ar::iec61850::scl::SclDataSetEntry& entry) {
    const auto basic = qstring(entry.basic_type).trimmed();
    const auto cdc = qstring(entry.cdc).trimmed();
    const auto da = qstring(entry.da_name).trimmed();
    if (entry.is_quality || da.compare(QStringLiteral("q"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Quality");
    }
    if (entry.is_timestamp || da.compare(QStringLiteral("t"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Timestamp");
    }
    if (!entry.enum_type.empty() || basic.contains(QStringLiteral("Enum"), Qt::CaseInsensitive) ||
        cdc.compare(QStringLiteral("DPC"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Enumeration");
    }
    if (basic.contains(QStringLiteral("Bool"), Qt::CaseInsensitive)) {
        return QStringLiteral("Boolean");
    }
    if (basic.contains(QStringLiteral("Float"), Qt::CaseInsensitive) ||
        basic.contains(QStringLiteral("INT"), Qt::CaseInsensitive) ||
        basic.contains(QStringLiteral("Integer"), Qt::CaseInsensitive)) {
        return QStringLiteral("Number");
    }
    return basic.isEmpty() ? QStringLiteral("Text") : basic;
}

QString initialValue(const QString& type, const QString& dataAttribute) {
    if (type == QStringLiteral("Boolean")) return QStringLiteral("false");
    if (type == QStringLiteral("Enumeration")) return QStringLiteral("intermediate-state");
    if (type == QStringLiteral("Quality")) return QStringLiteral("Good");
    if (type == QStringLiteral("Timestamp")) {
        return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
    }
    if (type == QStringLiteral("Number")) return QStringLiteral("0");
    if (dataAttribute.compare(QStringLiteral("stVal"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("on");
    }
    return QStringLiteral("—");
}

QString displayName(const ar::iec61850::scl::SclDataSetEntry& entry) {
    const auto dataObject = qstring(entry.do_name);
    const auto attribute = qstring(entry.da_name);
    if (!dataObject.isEmpty() && !attribute.isEmpty()) {
        return dataObject + QLatin1Char('.') + attribute;
    }
    if (!dataObject.isEmpty()) return dataObject;
    return qstring(entry.signal_reference);
}

QString referenceFor(const ar::iec61850::scl::SclDataSetEntry& entry) {
    if (!entry.signal_reference.empty()) return qstring(entry.signal_reference);
    QStringList parts;
    if (!entry.ld_inst.empty()) parts.push_back(qstring(entry.ld_inst));
    const auto logicalNode = qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst);
    if (!logicalNode.isEmpty()) parts.push_back(logicalNode);
    if (!entry.do_name.empty()) parts.push_back(qstring(entry.do_name));
    if (!entry.da_name.empty()) parts.push_back(qstring(entry.da_name));
    return parts.join(QLatin1Char('/'));
}

QString mmsDomainFor(const ar::iec61850::scl::SclDataSetEntry& entry) {
    return qstring(entry.ied_name) + qstring(entry.ld_inst);
}

QString mmsItemFor(const ar::iec61850::scl::SclDataSetEntry& entry) {
    auto attribute = qstring(entry.da_name);
    attribute.replace(QLatin1Char('.'), QLatin1Char('$'));
    QStringList parts{
        qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst),
        qstring(entry.functional_constraint),
        qstring(entry.do_name)};
    if (!attribute.isEmpty()) parts.push_back(attribute);
    parts.removeAll(QString{});
    return parts.join(QLatin1Char('$'));
}

QByteArray manifestField(QString value) {
    value.replace(QLatin1Char('\t'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return value.toUtf8();
}
} // namespace

IedSimulatorController::IedSimulatorController(QObject* parent)
    : QObject(parent) {
    connect(
        QCoreApplication::instance(),
        &QCoreApplication::aboutToQuit,
        this,
        [this] { stopSimulation(); });
    connect(
        &serverProcess_,
        &QProcess::started,
        this,
        [this] {
            const auto generation = serverStartGeneration_;
            appendActivity(
                QStringLiteral("Server"),
                QStringLiteral("Server process launched; waiting for listener confirmation."));
            QTimer::singleShot(5'000, this, [this, generation] {
                if (generation != serverStartGeneration_ || !starting_ ||
                    serverProcess_.state() == QProcess::NotRunning) return;
                appendActivity(
                    QStringLiteral("Server"),
                    QStringLiteral("Listener readiness was not confirmed within 5 seconds."),
                    QStringLiteral("Error"));
                stopSimulation();
            });
        });
    connect(
        &serverProcess_,
        &QProcess::errorOccurred,
        this,
        [this](const QProcess::ProcessError) {
            setRuntimeState(false, false);
            appendActivity(
                QStringLiteral("Server"),
                QStringLiteral("Could not start the MMS server: %1")
                    .arg(serverProcess_.errorString()),
                QStringLiteral("Error"));
        });
    connect(
        &serverProcess_,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this](const int exitCode, const QProcess::ExitStatus) {
            consumeServerOutput(
                standardOutputBuffer_, serverProcess_.readAllStandardOutput(), false);
            consumeServerOutput(
                standardErrorBuffer_, serverProcess_.readAllStandardError(), true);
            if (!standardOutputBuffer_.isEmpty()) {
                processServerLine(QString::fromUtf8(standardOutputBuffer_), false);
                standardOutputBuffer_.clear();
            }
            if (!standardErrorBuffer_.isEmpty()) {
                processServerLine(QString::fromUtf8(standardErrorBuffer_), true);
                standardErrorBuffer_.clear();
            }
            const bool wasActive = running_ || starting_;
            setRuntimeState(false, false);
            if (wasActive) {
                appendActivity(
                    QStringLiteral("Server"),
                    QStringLiteral("MMS endpoint stopped (exit %1).").arg(exitCode),
                    exitCode == 0 ? QStringLiteral("Info") : QStringLiteral("Warning"));
            }
        });
    connect(&serverProcess_, &QProcess::readyReadStandardOutput, this, [this] {
        consumeServerOutput(
            standardOutputBuffer_, serverProcess_.readAllStandardOutput(), false);
    });
    connect(&serverProcess_, &QProcess::readyReadStandardError, this, [this] {
        consumeServerOutput(
            standardErrorBuffer_, serverProcess_.readAllStandardError(), true);
    });
    appendActivity(
        QStringLiteral("Workspace"),
        QStringLiteral("Ready. Import an SCL, CID, SCD, or IID file to begin."));
}

IedSimulatorController::~IedSimulatorController() {
    if (serverProcess_.state() != QProcess::NotRunning) {
        serverProcess_.terminate();
        if (!serverProcess_.waitForFinished(800)) {
            serverProcess_.kill();
            serverProcess_.waitForFinished(800);
        }
    }
    removeModelManifest();
}

bool IedSimulatorController::imported() const noexcept { return !documents_.empty(); }
bool IedSimulatorController::running() const noexcept { return running_; }
bool IedSimulatorController::starting() const noexcept { return starting_; }
QString IedSimulatorController::sourceName() const { return sourceName_; }
QString IedSimulatorController::sourcePath() const { return sourcePath_; }
QString IedSimulatorController::fatalError() const { return fatalError_; }
QString IedSimulatorController::listenAddress() const { return listenAddress_; }
int IedSimulatorController::port() const noexcept { return port_; }
bool IedSimulatorController::gooseEnabled() const noexcept { return gooseEnabled_; }
bool IedSimulatorController::fileServiceEnabled() const noexcept { return fileServiceEnabled_; }
QString IedSimulatorController::fileFolder() const { return fileFolder_; }
QVariantList IedSimulatorController::ieds() const { return ieds_; }
int IedSimulatorController::selectedIedIndex() const noexcept { return selectedIedIndex_; }
QVariantList IedSimulatorController::values() const { return values_; }
int IedSimulatorController::selectedValueIndex() const noexcept { return selectedValueIndex_; }
QVariantList IedSimulatorController::activity() const { return activity_; }
int IedSimulatorController::logicalDeviceCount() const noexcept { return logicalDeviceCount_; }
int IedSimulatorController::dataObjectCount() const noexcept { return dataObjectCount_; }
int IedSimulatorController::dataAttributeCount() const noexcept { return dataAttributeCount_; }
int IedSimulatorController::dataSetCount() const noexcept { return dataSetCount_; }
int IedSimulatorController::reportCount() const noexcept { return reportCount_; }
int IedSimulatorController::gooseCount() const noexcept { return gooseCount_; }

QString IedSimulatorController::modelStatus() const {
    if (!fatalError_.isEmpty()) return fatalError_;
    if (!imported()) return QStringLiteral("No engineering model loaded");
    return QStringLiteral("Validated · %1 IED%2 · IEC 61850 model ready")
        .arg(ieds_.size())
        .arg(ieds_.size() == 1 ? QString{} : QStringLiteral("s"));
}

QStringList IedSimulatorController::availableAddresses() const {
    QStringList result{QStringLiteral("0.0.0.0")};
    const auto addresses = QNetworkInterface::allAddresses();
    for (const auto& address : addresses) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()) continue;
        const auto text = address.toString();
        if (!result.contains(text)) result.push_back(text);
    }
    result.push_back(QStringLiteral("127.0.0.1"));
    return result;
}

QVariantMap IedSimulatorController::selectedIed() const {
    if (selectedIedIndex_ < 0 || selectedIedIndex_ >= ieds_.size()) return {};
    return ieds_.at(selectedIedIndex_).toMap();
}

QVariantMap IedSimulatorController::selectedValue() const {
    if (selectedValueIndex_ < 0 || selectedValueIndex_ >= values_.size()) return {};
    return values_.at(selectedValueIndex_).toMap();
}

void IedSimulatorController::setListenAddress(const QString& value) {
    if (running_ || starting_ || listenAddress_ == value) return;
    listenAddress_ = value;
    emit configurationChanged();
}

void IedSimulatorController::setPort(const int value) {
    if (running_ || starting_ || value < 1 || value > 65'535 || port_ == value) return;
    port_ = value;
    emit configurationChanged();
}

void IedSimulatorController::setGooseEnabled(const bool value) {
    if (gooseEnabled_ == value) return;
    gooseEnabled_ = value;
    emit configurationChanged();
}

void IedSimulatorController::setFileServiceEnabled(const bool value) {
    if (fileServiceEnabled_ == value) return;
    fileServiceEnabled_ = value;
    emit configurationChanged();
}

void IedSimulatorController::setFileFolder(const QString& value) {
    if (fileFolder_ == value) return;
    fileFolder_ = value;
    emit configurationChanged();
}

bool IedSimulatorController::loadFile(const QUrl& fileUrl) {
    return importFile(fileUrl, false);
}

bool IedSimulatorController::addFile(const QUrl& fileUrl) {
    return importFile(fileUrl, true);
}

bool IedSimulatorController::importFile(const QUrl& fileUrl, const bool append) {
    const auto path = fileUrl.toLocalFile();
    if (path.isEmpty()) {
        fatalError_ = QStringLiteral("Choose a local SCL, CID, SCD, or IID file.");
        emit modelChanged();
        return false;
    }
    try {
        auto document = ar::iec61850::scl::SclParser{}.load(
            std::filesystem::path{path.toStdWString()});
        if (!append) {
            if (running_ || starting_) stopSimulation();
            documents_.clear();
            runtimeValues_.clear();
        }
        documents_.push_back(LoadedDocument{path, std::move(document)});
        sourcePath_ = path;
        sourceName_ = QFileInfo(path).fileName();
        fatalError_.clear();
        previousValue_.reset();
        rebuildPresentation();
        appendActivity(
            QStringLiteral("Importer"),
            QStringLiteral("%1 imported and validated successfully.").arg(sourceName_),
            QStringLiteral("Success"));
        return true;
    } catch (const std::exception& error) {
        fatalError_ = QString::fromUtf8(error.what());
        appendActivity(QStringLiteral("Importer"), fatalError_, QStringLiteral("Error"));
        emit modelChanged();
        return false;
    }
}

void IedSimulatorController::clear() {
    stopSimulation();
    documents_.clear();
    ieds_.clear();
    values_.clear();
    runtimeValues_.clear();
    previousValue_.reset();
    sourceName_.clear();
    sourcePath_.clear();
    fatalError_.clear();
    selectedIedIndex_ = -1;
    selectedValueIndex_ = -1;
    logicalDeviceCount_ = 0;
    dataObjectCount_ = 0;
    dataAttributeCount_ = 0;
    dataSetCount_ = 0;
    reportCount_ = 0;
    gooseCount_ = 0;
    emit modelChanged();
    emit selectionChanged();
    emit valuesChanged();
}

void IedSimulatorController::selectIed(const int index) {
    const int normalized = index >= 0 && index < ieds_.size() ? index : -1;
    if (selectedIedIndex_ == normalized) return;
    selectedIedIndex_ = normalized;
    rebuildValues();
    emit selectionChanged();
}

void IedSimulatorController::selectValue(const int index) {
    const int normalized = index >= 0 && index < values_.size() ? index : -1;
    if (selectedValueIndex_ == normalized) return;
    selectedValueIndex_ = normalized;
    emit selectionChanged();
}

bool IedSimulatorController::startSimulation() {
    if (!imported() || running_ || starting_) return false;
    const auto executable = serverExecutable();
    if (executable.isEmpty()) {
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("ariec61850_ied_simulator_server was not found beside the GUI."),
            QStringLiteral("Error"));
        return false;
    }
    if (!writeModelManifest()) return false;
    standardOutputBuffer_.clear();
    standardErrorBuffer_.clear();
    ++serverStartGeneration_;
    setRuntimeState(false, true);
    serverProcess_.setProgram(executable);
    serverProcess_.setArguments({
        QStringLiteral("--host"), listenAddress_,
        QStringLiteral("--port"), QString::number(port_),
        QStringLiteral("--model-manifest"), serverModelManifestPath_});
    serverProcess_.start();
    appendActivity(
        QStringLiteral("Server"),
        QStringLiteral("Starting IEDScout-compatible MMS endpoint on %1:%2.")
            .arg(listenAddress_)
            .arg(port_));
    return true;
}

void IedSimulatorController::stopSimulation() {
    if (serverProcess_.state() == QProcess::NotRunning) {
        setRuntimeState(false, false);
        return;
    }
    appendActivity(QStringLiteral("Server"), QStringLiteral("Stopping MMS endpoint…"));
    serverProcess_.terminate();
    if (!serverProcess_.waitForFinished(900)) {
        serverProcess_.kill();
        serverProcess_.waitForFinished(900);
    }
}

bool IedSimulatorController::applySelectedValue(
    const QString& value,
    const QString& quality,
    const QString& origin) {
    if (!running_ || selectedValueIndex_ < 0 || selectedValueIndex_ >= values_.size()) {
        return false;
    }
    auto item = values_.at(selectedValueIndex_).toMap();
    previousValue_ = ValueSnapshot{selectedIedIndex_, selectedValueIndex_, item};
    const auto before = item.value(QStringLiteral("value")).toString();
    item.insert(QStringLiteral("value"), value);
    item.insert(QStringLiteral("quality"), quality);
    item.insert(QStringLiteral("origin"), origin);
    item.insert(QStringLiteral("changed"), true);
    item.insert(
        QStringLiteral("updated"),
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    values_[selectedValueIndex_] = item;
    runtimeValues_.insert(item.value(QStringLiteral("reference")).toString(), item);
    if (!writeModelManifest()) {
        values_[selectedValueIndex_] = previousValue_->value;
        runtimeValues_.insert(
            previousValue_->value.value(QStringLiteral("reference")).toString(),
            previousValue_->value);
        previousValue_.reset();
        emit valuesChanged();
        emit selectionChanged();
        return false;
    }
    emit valuesChanged();
    emit selectionChanged();
    appendActivity(
        QStringLiteral("Value"),
        QStringLiteral("%1 changed from %2 to %3 · quality %4 · origin %5")
            .arg(item.value(QStringLiteral("reference")).toString(), before, value, quality, origin),
        QStringLiteral("Success"));
    return true;
}

bool IedSimulatorController::undoLastChange() {
    if (!previousValue_.has_value() || previousValue_->iedIndex != selectedIedIndex_ ||
        previousValue_->valueIndex < 0 || previousValue_->valueIndex >= values_.size()) {
        return false;
    }
    const int index = previousValue_->valueIndex;
    const auto reference = previousValue_->value.value(QStringLiteral("reference")).toString();
    const auto current = values_[index].toMap();
    values_[index] = previousValue_->value;
    runtimeValues_.insert(reference, previousValue_->value);
    if (!writeModelManifest()) {
        values_[index] = current;
        runtimeValues_.insert(reference, current);
        return false;
    }
    selectedValueIndex_ = index;
    previousValue_.reset();
    emit valuesChanged();
    emit selectionChanged();
    appendActivity(
        QStringLiteral("Value"),
        QStringLiteral("Last change to %1 was reverted.").arg(reference));
    return true;
}

void IedSimulatorController::clearActivity() {
    activity_.clear();
    emit activityChanged();
}

QString IedSimulatorController::diagnosticsText() const {
    QString text;
    text += QStringLiteral("ARStack IED Simulator diagnostics\n");
    text += QStringLiteral("Captured: %1\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    text += QStringLiteral("Model: %1\nSource: %2\n")
        .arg(sourceName_.isEmpty() ? QStringLiteral("<none>") : sourceName_)
        .arg(sourcePath_.isEmpty() ? QStringLiteral("<none>") : sourcePath_);
    text += QStringLiteral("Endpoint: %1:%2\nState: %3\nProcess: %4 (pid %5)\n")
        .arg(listenAddress_)
        .arg(port_)
        .arg(running_ ? QStringLiteral("ready")
                      : (starting_ ? QStringLiteral("starting") : QStringLiteral("stopped")))
        .arg(serverProcess_.state() == QProcess::NotRunning
                 ? QStringLiteral("not running")
                 : QStringLiteral("running"))
        .arg(serverProcess_.processId());
    text += QStringLiteral(
        "IEDScout profile: Authentication=None; AP-title=1,1,1,999,1; "
        "AE-qualifier=12; P-selector=00 00 00 01; S-selector=00 01; T-selector=00 01\n");
    text += QStringLiteral(
        "Counts: IED=%1; LD=%2; referenced DO=%3; referenced DA=%4; "
        "DataSet=%5; Report=%6; GOOSE=%7\n")
        .arg(ieds_.size())
        .arg(logicalDeviceCount_)
        .arg(dataObjectCount_)
        .arg(dataAttributeCount_)
        .arg(dataSetCount_)
        .arg(reportCount_)
        .arg(gooseCount_);
    text += QStringLiteral("\nRecent activity (newest first):\n");
    for (const auto& item : activity_) {
        const auto event = item.toMap();
        text += QStringLiteral("%1 | %2 | %3 | %4\n")
            .arg(
                event.value(QStringLiteral("time")).toString(),
                event.value(QStringLiteral("severity")).toString(),
                event.value(QStringLiteral("category")).toString(),
                event.value(QStringLiteral("message")).toString());
    }
    return text;
}

void IedSimulatorController::copyDiagnostics() {
    if (auto* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(diagnosticsText());
        appendActivity(
            QStringLiteral("Diagnostics"),
            QStringLiteral("Connection diagnostics copied to the clipboard."),
            QStringLiteral("Success"));
    }
}

void IedSimulatorController::consumeServerOutput(
    QByteArray& buffer,
    const QByteArray& bytes,
    const bool standardError) {
    buffer += bytes;
    while (true) {
        const auto newline = buffer.indexOf('\n');
        if (newline < 0) break;
        const auto line = QString::fromUtf8(buffer.left(newline)).trimmed();
        buffer.remove(0, newline + 1);
        if (!line.isEmpty()) processServerLine(line, standardError);
    }
}

void IedSimulatorController::processServerLine(
    const QString& line,
    const bool standardError) {
    if (!line.startsWith(QStringLiteral("IEDSIM_EVENT "))) {
        appendActivity(
            QStringLiteral("MMS"),
            line,
            standardError ? QStringLiteral("Error") : QStringLiteral("Info"));
        return;
    }

    QVariantMap fields;
    const auto tokens = line.mid(13).split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (const auto& token : tokens) {
        const auto separator = token.indexOf(QLatin1Char('='));
        if (separator > 0) fields.insert(token.left(separator), token.mid(separator + 1));
    }
    const auto kind = fields.value(QStringLiteral("kind")).toString();
    if (kind == QStringLiteral("server_ready")) {
        setRuntimeState(true, false);
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral(
                "MMS listener ready on %1:%2; %3 domains, %4 MMS objects; profile %5.")
                .arg(fields.value(QStringLiteral("bind")).toString())
                .arg(fields.value(QStringLiteral("port")).toString())
                .arg(fields.value(QStringLiteral("domains")).toString())
                .arg(fields.value(QStringLiteral("objects")).toString())
                .arg(fields.value(QStringLiteral("profile")).toString()),
            QStringLiteral("Success"));
        const auto truncated = fields.value(QStringLiteral("truncated")).toInt();
        if (truncated > 0) {
            appendActivity(
                QStringLiteral("Model"),
                QStringLiteral("%1 logical-node roots exceeded the bounded host profile and were omitted.")
                    .arg(truncated),
                QStringLiteral("Warning"));
        }
        return;
    }
    if (kind == QStringLiteral("client_connected")) {
        appendActivity(
            QStringLiteral("TCP"),
            QStringLiteral("Client %1 connected (association %2).")
                .arg(fields.value(QStringLiteral("remote")).toString())
                .arg(fields.value(QStringLiteral("association")).toString()),
            QStringLiteral("Success"));
        return;
    }
    if (kind == QStringLiteral("protocol_stage")) {
        appendActivity(
            QStringLiteral("Protocol"),
            QStringLiteral("Association %1 reached %2 stage.")
                .arg(fields.value(QStringLiteral("association")).toString())
                .arg(fields.value(QStringLiteral("stage")).toString().toUpper()),
            QStringLiteral("Success"));
        return;
    }
    if (kind == QStringLiteral("mms_service")) {
        const bool accepted = fields.value(QStringLiteral("accepted")).toString() !=
            QStringLiteral("false");
        appendActivity(
            QStringLiteral("MMS"),
            QStringLiteral("%1 invoke %2 %3.")
                .arg(fields.value(QStringLiteral("service")).toString())
                .arg(fields.value(QStringLiteral("invoke")).toString())
                .arg(accepted ? QStringLiteral("answered") : QStringLiteral("rejected")),
            accepted ? QStringLiteral("Success") : QStringLiteral("Warning"));
        return;
    }
    if (kind == QStringLiteral("client_closed")) {
        appendActivity(
            QStringLiteral("TCP"),
            QStringLiteral("Client %1 closed; RX %2 bytes, TX %3 bytes, final state %4.")
                .arg(fields.value(QStringLiteral("remote")).toString())
                .arg(fields.value(QStringLiteral("rx")).toString())
                .arg(fields.value(QStringLiteral("tx")).toString())
                .arg(fields.value(QStringLiteral("state")).toString()));
        return;
    }
    if (kind != QStringLiteral("server_stopped")) {
        appendActivity(QStringLiteral("MMS"), line);
    }
}

bool IedSimulatorController::writeModelManifest() {
    if (serverModelManifestPath_.isEmpty()) {
        serverModelManifestPath_ = QDir::temp().filePath(
            QStringLiteral("arstack-ied-simulator-%1.model")
                .arg(QCoreApplication::applicationPid()));
    }
    seedRuntimeValues();
    ++modelRevision_;
    QByteArray manifest = "ARSTACK_IED_MODEL\t2\t" +
        QByteArray::number(modelRevision_) + "\n";
    QSet<QString> uniqueRoots;
    for (const auto& loaded : documents_) {
        QFile input{loaded.path};
        if (!input.open(QIODevice::ReadOnly)) {
            appendActivity(
                QStringLiteral("Model"),
                QStringLiteral("Could not reopen %1 for runtime model projection.").arg(loaded.path),
                QStringLiteral("Error"));
            return false;
        }
        QXmlStreamReader xml{&input};
        QString currentIed;
        QString currentDomain;
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const auto element = xml.name();
                const auto attributes = xml.attributes();
                if (element == QStringLiteral("IED")) {
                    currentIed = attributes.value(QStringLiteral("name")).toString();
                } else if (element == QStringLiteral("LDevice")) {
                    currentDomain = currentIed + attributes.value(QStringLiteral("inst")).toString();
                } else if ((element == QStringLiteral("LN") ||
                            element == QStringLiteral("LN0")) &&
                           !currentDomain.isEmpty()) {
                    const auto logicalNode =
                        attributes.value(QStringLiteral("prefix")).toString() +
                        attributes.value(QStringLiteral("lnClass")).toString() +
                        attributes.value(QStringLiteral("inst")).toString();
                    if (!logicalNode.isEmpty()) {
                        const auto key = currentDomain + QLatin1Char('\n') + logicalNode;
                        if (!uniqueRoots.contains(key)) {
                            uniqueRoots.insert(key);
                            manifest += "LN\t" + currentDomain.toUtf8() + "\t" +
                                logicalNode.toUtf8() + "\n";
                        }
                    }
                }
            } else if (xml.isEndElement()) {
                if (xml.name() == QStringLiteral("LDevice")) currentDomain.clear();
                if (xml.name() == QStringLiteral("IED")) currentIed.clear();
            }
        }
        if (xml.hasError()) {
            appendActivity(
                QStringLiteral("Model"),
                QStringLiteral("Runtime model projection failed: %1").arg(xml.errorString()),
                QStringLiteral("Error"));
            return false;
        }
    }

    QSet<QString> emittedObjects;
    for (auto it = runtimeValues_.cbegin(); it != runtimeValues_.cend(); ++it) {
        const auto item = it.value();
        const auto domain = item.value(QStringLiteral("mmsDomain")).toString();
        const auto mmsItem = item.value(QStringLiteral("mmsItem")).toString();
        if (domain.isEmpty() || mmsItem.isEmpty()) continue;
        const auto key = domain + QLatin1Char('\n') + mmsItem;
        if (emittedObjects.contains(key)) continue;
        emittedObjects.insert(key);
        manifest += "OBJ\t" + manifestField(domain) + "\t" +
            manifestField(mmsItem) + "\t" +
            manifestField(item.value(QStringLiteral("rawType")).toString()) + "\t" +
            manifestField(item.value(QStringLiteral("type")).toString()) + "\t" +
            manifestField(item.value(QStringLiteral("value")).toString()) + "\n";
    }

    QSet<QString> emittedDataSetMembers;
    for (const auto& loaded : documents_) {
        for (const auto& dataSet : loaded.document.data_sets) {
            const auto dataSetDomain = qstring(dataSet.ied_name) + qstring(dataSet.ld_inst);
            auto dataSetItem = qstring(dataSet.logical_node_path) + QLatin1Char('$') +
                qstring(dataSet.name);
            dataSetItem.replace(QLatin1Char('.'), QLatin1Char('$'));
            for (const auto& entry : dataSet.entries) {
                const auto memberDomain = mmsDomainFor(entry);
                const auto memberItem = mmsItemFor(entry);
                const auto memberKey = dataSetDomain + QLatin1Char('\n') + dataSetItem +
                    QLatin1Char('\n') + memberDomain + QLatin1Char('\n') + memberItem;
                if (dataSetDomain.isEmpty() || dataSetItem.isEmpty() ||
                    memberDomain.isEmpty() || memberItem.isEmpty() ||
                    emittedDataSetMembers.contains(memberKey)) continue;
                emittedDataSetMembers.insert(memberKey);
                manifest += "DS\t" + manifestField(dataSetDomain) + "\t" +
                    manifestField(dataSetItem) + "\t" + manifestField(memberDomain) +
                    "\t" + manifestField(memberItem) + "\n";
            }
        }
    }
    if (uniqueRoots.isEmpty()) {
        appendActivity(
            QStringLiteral("Model"),
            QStringLiteral("The imported SCL contains no discoverable logical-node roots."),
            QStringLiteral("Error"));
        return false;
    }
    QSaveFile output{serverModelManifestPath_};
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        output.write(manifest) != manifest.size()) {
        appendActivity(
            QStringLiteral("Model"),
            QStringLiteral("Could not create the temporary MMS model manifest."),
            QStringLiteral("Error"));
        return false;
    }
    if (!output.commit()) {
        appendActivity(
            QStringLiteral("Model"),
            QStringLiteral("Could not publish the MMS model manifest atomically."),
            QStringLiteral("Error"));
        return false;
    }
    return true;
}

void IedSimulatorController::removeModelManifest() {
    if (serverModelManifestPath_.isEmpty()) return;
    QFile::remove(serverModelManifestPath_);
    serverModelManifestPath_.clear();
}

void IedSimulatorController::rebuildPresentation() {
    ieds_.clear();
    logicalDeviceCount_ = 0;
    dataObjectCount_ = 0;
    dataAttributeCount_ = 0;
    dataSetCount_ = 0;
    reportCount_ = 0;
    gooseCount_ = 0;

    std::set<QString> logicalDevices;
    std::set<QString> dataObjects;
    std::set<QString> dataAttributes;
    for (std::size_t documentIndex = 0; documentIndex < documents_.size(); ++documentIndex) {
        const auto& loaded = documents_[documentIndex];
        const auto& document = loaded.document;
        dataSetCount_ += static_cast<int>(document.data_sets.size());
        reportCount_ += static_cast<int>(document.report_controls.size());
        gooseCount_ += static_cast<int>(document.goose_streams.size());

        for (const auto& ied : document.ieds) {
            QVariantMap item;
            item.insert(QStringLiteral("name"), qstring(ied.name));
            item.insert(QStringLiteral("manufacturer"), qstring(ied.manufacturer));
            item.insert(QStringLiteral("type"), qstring(ied.type));
            item.insert(QStringLiteral("configVersion"), qstring(ied.config_version));
            item.insert(QStringLiteral("documentIndex"), static_cast<int>(documentIndex));
            item.insert(QStringLiteral("status"), running_ ? QStringLiteral("Running") : QStringLiteral("Ready"));
            item.insert(QStringLiteral("endpoint"), QStringLiteral("%1:%2").arg(listenAddress_).arg(port_));
            ieds_.push_back(item);
        }

        const auto collectEntry = [&](const ar::iec61850::scl::SclDataSetEntry& entry) {
            const auto ld = qstring(entry.ied_name) + QLatin1Char('/') + qstring(entry.ld_inst);
            logicalDevices.insert(ld);
            const auto object = ld + QLatin1Char('/') + qstring(entry.ln_class) + qstring(entry.ln_inst) +
                QLatin1Char('/') + qstring(entry.do_name);
            dataObjects.insert(object);
            dataAttributes.insert(object + QLatin1Char('/') + qstring(entry.da_name));
        };
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
    if (ieds_.isEmpty()) {
        QVariantMap fallback;
        fallback.insert(QStringLiteral("name"), QFileInfo(sourceName_).completeBaseName());
        fallback.insert(QStringLiteral("manufacturer"), QStringLiteral("SCL model"));
        fallback.insert(QStringLiteral("type"), QStringLiteral("IED"));
        fallback.insert(QStringLiteral("configVersion"), QString{});
        fallback.insert(QStringLiteral("documentIndex"), 0);
        fallback.insert(QStringLiteral("status"), running_ ? QStringLiteral("Running") : QStringLiteral("Ready"));
        fallback.insert(QStringLiteral("endpoint"), QStringLiteral("%1:%2").arg(listenAddress_).arg(port_));
        ieds_.push_back(fallback);
    }
    logicalDeviceCount_ = static_cast<int>(logicalDevices.size());
    dataObjectCount_ = static_cast<int>(dataObjects.size());
    dataAttributeCount_ = static_cast<int>(dataAttributes.size());
    const auto maximumIedIndex = static_cast<int>(ieds_.size()) - 1;
    selectedIedIndex_ = ieds_.isEmpty()
        ? -1
        : std::clamp(selectedIedIndex_, 0, maximumIedIndex);
    if (selectedIedIndex_ < 0 && !ieds_.isEmpty()) selectedIedIndex_ = 0;
    seedRuntimeValues();
    rebuildValues();
    emit modelChanged();
    emit selectionChanged();
}

void IedSimulatorController::rebuildValues() {
    values_.clear();
    selectedValueIndex_ = -1;
    if (selectedIedIndex_ < 0 || selectedIedIndex_ >= ieds_.size()) {
        emit valuesChanged();
        return;
    }
    const auto ied = ieds_.at(selectedIedIndex_).toMap();
    const int documentIndex = ied.value(QStringLiteral("documentIndex")).toInt();
    if (documentIndex < 0 || documentIndex >= static_cast<int>(documents_.size())) {
        emit valuesChanged();
        return;
    }
    const auto selectedName = ied.value(QStringLiteral("name")).toString();
    const auto& document = documents_[static_cast<std::size_t>(documentIndex)].document;
    std::unordered_set<std::string> seen;
    const auto appendEntry = [&](const ar::iec61850::scl::SclDataSetEntry& entry) {
        if (!entry.ied_name.empty() && qstring(entry.ied_name) != selectedName) return;
        const auto reference = referenceFor(entry).toStdString();
        if (!seen.insert(reference).second) return;
        const auto key = referenceFor(entry);
        if (!runtimeValues_.contains(key)) runtimeValues_.insert(key, valueMap(entry));
        values_.push_back(runtimeValues_.value(key));
    };
    for (const auto& dataSet : document.data_sets) {
        for (const auto& entry : dataSet.entries) appendEntry(entry);
    }
    for (const auto& stream : document.goose_streams) {
        for (const auto& entry : stream.entries) appendEntry(entry);
    }
    for (const auto& report : document.report_controls) {
        for (const auto& entry : report.entries) appendEntry(entry);
    }
    selectedValueIndex_ = values_.isEmpty() ? -1 : 0;
    emit valuesChanged();
}

void IedSimulatorController::seedRuntimeValues() {
    const auto seedEntry = [this](const ar::iec61850::scl::SclDataSetEntry& entry) {
        const auto key = referenceFor(entry);
        if (!key.isEmpty() && !runtimeValues_.contains(key)) {
            runtimeValues_.insert(key, valueMap(entry));
        }
    };
    for (const auto& loaded : documents_) {
        for (const auto& dataSet : loaded.document.data_sets) {
            for (const auto& entry : dataSet.entries) seedEntry(entry);
        }
        for (const auto& stream : loaded.document.goose_streams) {
            for (const auto& entry : stream.entries) seedEntry(entry);
        }
        for (const auto& report : loaded.document.report_controls) {
            for (const auto& entry : report.entries) seedEntry(entry);
        }
    }
}

QVariantMap IedSimulatorController::valueMap(
    const ar::iec61850::scl::SclDataSetEntry& entry) {
    const auto type = normalizedType(entry);
    QVariantMap item;
    item.insert(QStringLiteral("name"), displayName(entry));
    item.insert(QStringLiteral("reference"), referenceFor(entry));
    item.insert(QStringLiteral("logicalDevice"), qstring(entry.ld_inst));
    item.insert(
        QStringLiteral("logicalNode"),
        qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst));
    item.insert(QStringLiteral("dataObject"), qstring(entry.do_name));
    item.insert(QStringLiteral("dataAttribute"), qstring(entry.da_name));
    item.insert(QStringLiteral("fc"), qstring(entry.functional_constraint));
    item.insert(QStringLiteral("cdc"), qstring(entry.cdc));
    item.insert(QStringLiteral("type"), type);
    item.insert(QStringLiteral("rawType"), qstring(entry.basic_type));
    item.insert(QStringLiteral("iedName"), qstring(entry.ied_name));
    item.insert(QStringLiteral("mmsDomain"), mmsDomainFor(entry));
    item.insert(QStringLiteral("mmsItem"), mmsItemFor(entry));
    item.insert(QStringLiteral("value"), initialValue(type, qstring(entry.da_name)));
    item.insert(QStringLiteral("quality"), QStringLiteral("Good"));
    item.insert(QStringLiteral("origin"), QStringLiteral("Simulator"));
    item.insert(QStringLiteral("writable"), !entry.is_quality && !entry.is_timestamp);
    item.insert(QStringLiteral("changed"), false);
    item.insert(QStringLiteral("updated"), QStringLiteral("—"));
    if (type == QStringLiteral("Enumeration")) {
        item.insert(
            QStringLiteral("options"),
            QStringList{
                QStringLiteral("intermediate-state"),
                QStringLiteral("off"),
                QStringLiteral("on"),
                QStringLiteral("bad-state")});
    } else if (type == QStringLiteral("Boolean")) {
        item.insert(
            QStringLiteral("options"),
            QStringList{QStringLiteral("false"), QStringLiteral("true")});
    }
    return item;
}

void IedSimulatorController::appendActivity(
    const QString& category,
    const QString& message,
    const QString& severity) {
    QVariantMap event;
    event.insert(
        QStringLiteral("time"),
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    event.insert(QStringLiteral("category"), category);
    event.insert(QStringLiteral("message"), message);
    event.insert(QStringLiteral("severity"), severity);
    activity_.push_front(event);
    while (activity_.size() > 200) activity_.removeLast();
    emit activityChanged();
}

void IedSimulatorController::setRuntimeState(const bool running, const bool starting) {
    if (running_ == running && starting_ == starting) return;
    running_ = running;
    starting_ = starting;
    for (auto& item : ieds_) {
        auto map = item.toMap();
        map.insert(QStringLiteral("status"), running_ ? QStringLiteral("Running") : QStringLiteral("Ready"));
        item = map;
    }
    emit runtimeChanged();
    emit modelChanged();
}

QString IedSimulatorController::serverExecutable() const {
#if defined(Q_OS_WIN)
    constexpr auto executableName = "ariec61850_ied_simulator_server.exe";
#else
    constexpr auto executableName = "ariec61850_ied_simulator_server";
#endif
    const auto besideGui = QCoreApplication::applicationDirPath() + QLatin1Char('/') +
        QString::fromLatin1(executableName);
    if (QFileInfo::exists(besideGui)) return besideGui;
    const auto currentDirectory = QCoreApplication::applicationDirPath() +
        QStringLiteral("/ariec61850-core/") + QString::fromLatin1(executableName);
    if (QFileInfo::exists(currentDirectory)) return currentDirectory;
    return {};
}
