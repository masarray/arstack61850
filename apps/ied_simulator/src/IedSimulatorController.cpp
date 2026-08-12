// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedSimulatorController.hpp"
#include "SclMmsMaterializer.hpp"

#include "ariec61850/scl/parser.hpp"

#include <QCoreApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QNetworkInterface>
#include <QSaveFile>
#include <QSet>
#include <QTimer>
#include <QXmlStreamReader>

#include <algorithm>
#include <array>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

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
    if (type == QStringLiteral("Timestamp")) return QStringLiteral("1970-01-01 00:00:00.000");
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
    auto dataObject = qstring(entry.do_name);
    dataObject.replace(QLatin1Char('.'), QLatin1Char('$'));
    QStringList parts{
        qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst),
        qstring(entry.functional_constraint),
        dataObject};
    if (!attribute.isEmpty()) parts.push_back(attribute);
    parts.removeAll(QString{});
    return parts.join(QLatin1Char('$'));
}

bool mmsWritableFc(const QString& fc) {
    return fc.compare(QStringLiteral("SP"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("SG"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("SE"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("SV"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("CF"), Qt::CaseInsensitive) == 0 ||
        fc.compare(QStringLiteral("DC"), Qt::CaseInsensitive) == 0;
}

QString runtimeValueKey(const QVariantMap& item) {
    const auto domain = item.value(QStringLiteral("mmsDomain")).toString();
    const auto mmsItem = item.value(QStringLiteral("mmsItem")).toString();
    if (!domain.isEmpty() && !mmsItem.isEmpty()) {
        return domain + QLatin1Char('\n') + mmsItem;
    }
    return item.value(QStringLiteral("reference")).toString();
}

QString deterministicTimestamp(quint64 milliseconds);

QVariantMap materializedValueMap(const arstack::iedsim::MaterializedSclValue& value) {
    QVariantMap item;
    item.insert(
        QStringLiteral("name"),
        value.dataObject + QLatin1Char('.') + value.dataAttribute);
    item.insert(QStringLiteral("reference"), value.reference);
    item.insert(QStringLiteral("logicalDevice"), value.logicalDevice);
    item.insert(QStringLiteral("logicalNode"), value.logicalNode);
    item.insert(QStringLiteral("dataObject"), value.dataObject);
    item.insert(QStringLiteral("dataAttribute"), value.dataAttribute);
    item.insert(QStringLiteral("fc"), value.functionalConstraint);
    item.insert(QStringLiteral("cdc"), value.cdc);
    item.insert(QStringLiteral("type"), value.normalizedType);
    item.insert(QStringLiteral("rawType"), value.rawType);
    item.insert(QStringLiteral("mmsTypeSignature"), value.mmsTypeSignature);
    item.insert(QStringLiteral("iedName"), value.iedName);
    item.insert(QStringLiteral("mmsDomain"), value.mmsDomain);
    item.insert(QStringLiteral("mmsItem"), value.mmsItem);
    item.insert(QStringLiteral("value"), value.initialValue);
    item.insert(QStringLiteral("quality"), QStringLiteral("Good"));
    item.insert(QStringLiteral("origin"), QStringLiteral("scl"));
    const auto controlMetadata =
        value.dataAttribute.compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) == 0 ||
        value.dataAttribute.compare(QStringLiteral("sboTimeout"), Qt::CaseInsensitive) == 0 ||
        value.dataAttribute.compare(QStringLiteral("operTimeout"), Qt::CaseInsensitive) == 0;
    item.insert(
        QStringLiteral("writable"),
        !value.quality && !value.timestamp && !controlMetadata);
    item.insert(QStringLiteral("mmsWritable"), value.mmsWritable);
    item.insert(QStringLiteral("changed"), false);
    item.insert(QStringLiteral("timestamp"), deterministicTimestamp(0U));
    item.insert(QStringLiteral("updated"), deterministicTimestamp(0U));
    item.insert(QStringLiteral("liveRevision"), 0ULL);
    if (value.normalizedType == QStringLiteral("Enumeration")) {
        item.insert(
            QStringLiteral("options"),
            QStringList{
                QStringLiteral("intermediate-state"),
                QStringLiteral("off"),
                QStringLiteral("on"),
                QStringLiteral("bad-state")});
    } else if (value.normalizedType == QStringLiteral("Boolean")) {
        item.insert(
            QStringLiteral("options"),
            QStringList{QStringLiteral("false"), QStringLiteral("true")});
    }
    return item;
}

QByteArray manifestField(QString value) {
    value.replace(QLatin1Char('\t'), QLatin1Char(' '));
    value.replace(QLatin1Char('\r'), QLatin1Char(' '));
    value.replace(QLatin1Char('\n'), QLatin1Char(' '));
    return value.toUtf8();
}

QString liveHex(const QString& value) {
    return QString::fromLatin1(value.toUtf8().toHex());
}

QString liveUnhex(const QString& value) {
    return QString::fromUtf8(QByteArray::fromHex(value.toLatin1()));
}

QString deterministicTimestamp(const quint64 milliseconds) {
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(milliseconds))
        .toUTC()
        .toString(Qt::ISODateWithMs);
}

quint8 reportTriggerMask(const ar::iec61850::scl::SclReportControl& report) {
    quint8 mask{};
    if (report.trigger_options.data_change) mask |= 0x40U;
    if (report.trigger_options.quality_change) mask |= 0x20U;
    if (report.trigger_options.data_update) mask |= 0x10U;
    if (report.trigger_options.integrity) mask |= 0x08U;
    if (report.trigger_options.general_interrogation) mask |= 0x04U;
    return mask;
}

std::array<quint8, 2U> reportOptionalMask(
    const ar::iec61850::scl::SclReportControl& report) {
    std::array<quint8, 2U> mask{};
    if (report.optional_fields.sequence_number) mask[0] |= 0x40U;
    if (report.optional_fields.report_timestamp) mask[0] |= 0x20U;
    if (report.optional_fields.reason_code) mask[0] |= 0x10U;
    if (report.optional_fields.data_set) mask[0] |= 0x08U;
    if (report.optional_fields.data_reference) mask[0] |= 0x04U;
    if (report.buffered && report.optional_fields.buffer_overflow) mask[0] |= 0x02U;
    if (report.buffered && report.optional_fields.entry_id) mask[0] |= 0x01U;
    if (report.optional_fields.configuration_revision) mask[1] |= 0x80U;
    if (report.buffered && report.optional_fields.segmentation) mask[1] |= 0x40U;
    return mask;
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
    return QStringLiteral("Validated · %1 IED%2 · full SCL MMS model ready")
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
        const auto materialized = arstack::iedsim::materializeSclMmsModel(path);
        QVariantList materializedValues;
        materializedValues.reserve(static_cast<qsizetype>(materialized.values.size()));
        for (const auto& value : materialized.values) {
            materializedValues.push_back(materializedValueMap(value));
        }
        if (running_ || starting_) stopSimulation();
        if (!append) {
            documents_.clear();
            runtimeValues_.clear();
        }
        documents_.push_back(LoadedDocument{path, std::move(document), std::move(materializedValues)});
        sourcePath_ = path;
        sourceName_ = QFileInfo(path).fileName();
        fatalError_.clear();
        previousValue_.reset();
        pendingMutationRequest_ = 0U;
        pendingUndo_ = false;
        rebuildPresentation();
        appendActivity(
            QStringLiteral("Importer"),
            QStringLiteral("%1 imported: %2 structural MMS leaves across %3 logical nodes.")
                .arg(sourceName_)
                .arg(materialized.values.size())
                .arg(materialized.logicalNodeCount),
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
    pendingMutationRequest_ = 0U;
    pendingUndo_ = false;
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

bool IedSimulatorController::selectValueByMmsItem(const QString& mmsItem) {
    for (qsizetype index = 0; index < values_.size(); ++index) {
        if (values_.at(index).toMap().value(QStringLiteral("mmsItem")).toString() == mmsItem) {
            selectValue(static_cast<int>(index));
            return true;
        }
    }
    return false;
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
    if (!running_ || pendingMutationRequest_ != 0U ||
        selectedValueIndex_ < 0 || selectedValueIndex_ >= values_.size()) {
        return false;
    }
    const auto item = values_.at(selectedValueIndex_).toMap();
    if (!item.value(QStringLiteral("writable")).toBool()) return false;

    previousValue_ = ValueSnapshot{
        selectedIedIndex_, selectedValueIndex_, item, 0U};
    const auto requestId = nextMutationRequest_++;
    pendingMutationRequest_ = requestId;
    pendingUndo_ = false;
    const auto expectedRevision = std::optional<quint64>{
        item.value(QStringLiteral("liveRevision")).toULongLong()};
    if (!sendLiveMutation(item, value, quality, origin, requestId, expectedRevision)) {
        pendingMutationRequest_ = 0U;
        previousValue_.reset();
        return false;
    }
    return true;
}

bool IedSimulatorController::undoLastChange() {
    if (!running_ || pendingMutationRequest_ != 0U || !previousValue_.has_value() ||
        previousValue_->iedIndex != selectedIedIndex_ ||
        previousValue_->valueIndex < 0 || previousValue_->valueIndex >= values_.size() ||
        previousValue_->appliedRevision == 0U) {
        return false;
    }
    const auto requestId = nextMutationRequest_++;
    pendingMutationRequest_ = requestId;
    pendingUndo_ = true;
    const auto& previous = previousValue_->value;
    if (!sendLiveMutation(
            previous,
            previous.value(QStringLiteral("value")).toString(),
            previous.value(QStringLiteral("quality")).toString(),
            QStringLiteral("gui-undo"),
            requestId,
            previousValue_->appliedRevision)) {
        pendingMutationRequest_ = 0U;
        pendingUndo_ = false;
        return false;
    }
    return true;
}

bool IedSimulatorController::sendLiveMutation(
    const QVariantMap& item,
    const QString& value,
    const QString& quality,
    const QString& origin,
    const quint64 requestId,
    const std::optional<quint64> expectedRevision) {
    if (serverProcess_.state() != QProcess::Running) return false;
    const auto domain = item.value(QStringLiteral("mmsDomain")).toString();
    const auto mmsItem = item.value(QStringLiteral("mmsItem")).toString();
    if (domain.isEmpty() || mmsItem.isEmpty()) return false;

    QByteArray command{"IEDSIM_CMD kind=set request="};
    command += QByteArray::number(requestId);
    command += " domain=";
    command += liveHex(domain).toLatin1();
    command += " item=";
    command += liveHex(mmsItem).toLatin1();
    command += " value=";
    command += liveHex(value).toLatin1();
    command += " quality=";
    command += liveHex(quality).toLatin1();
    command += " origin=";
    command += liveHex(origin).toLatin1();
    if (expectedRevision.has_value()) {
        command += " expected=";
        command += QByteArray::number(*expectedRevision);
    }
    command += '\n';
    return serverProcess_.write(command) == command.size();
}

void IedSimulatorController::applyServerValueState(const QVariantMap& fields) {
    const auto domain = liveUnhex(fields.value(QStringLiteral("domain")).toString());
    const auto itemName = liveUnhex(fields.value(QStringLiteral("item")).toString());
    const auto key = domain + QLatin1Char('\n') + itemName;
    auto found = runtimeValues_.find(key);
    if (found == runtimeValues_.end()) {
        appendActivity(
            QStringLiteral("Live state"),
            QStringLiteral("Server published an unknown state key %1/%2.").arg(domain, itemName),
            QStringLiteral("Warning"));
        return;
    }

    auto state = found.value();
    const auto revision = fields.value(QStringLiteral("revision")).toULongLong();
    const auto timestampMs = fields.value(QStringLiteral("timestamp_ms")).toULongLong();
    const auto timestamp = deterministicTimestamp(timestampMs);
    state.insert(QStringLiteral("value"), liveUnhex(fields.value(QStringLiteral("value")).toString()));
    state.insert(QStringLiteral("quality"), liveUnhex(fields.value(QStringLiteral("quality")).toString()));
    state.insert(QStringLiteral("origin"), liveUnhex(fields.value(QStringLiteral("origin")).toString()));
    state.insert(QStringLiteral("timestamp"), timestamp);
    state.insert(QStringLiteral("updated"), timestamp);
    state.insert(QStringLiteral("liveRevision"), revision);
    state.insert(QStringLiteral("changed"), revision != 0U);
    found.value() = state;

    for (qsizetype index = 0; index < values_.size(); ++index) {
        if (runtimeValueKey(values_.at(index).toMap()) == key) {
            values_[index] = state;
            break;
        }
    }

    const auto requestId = fields.value(QStringLiteral("request")).toULongLong();
    if (requestId != 0U && requestId == pendingMutationRequest_) {
        if (pendingUndo_) {
            previousValue_.reset();
        } else if (previousValue_.has_value()) {
            previousValue_->appliedRevision = revision;
        }
        pendingMutationRequest_ = 0U;
        pendingUndo_ = false;
    }

    emit valuesChanged();
    emit selectionChanged();
    appendActivity(
        QStringLiteral("Live state"),
        QStringLiteral("%1/%2 = %3 · quality %4 · origin %5 · rev %6")
            .arg(
                domain,
                itemName,
                state.value(QStringLiteral("value")).toString(),
                state.value(QStringLiteral("quality")).toString(),
                state.value(QStringLiteral("origin")).toString())
            .arg(revision),
        QStringLiteral("Success"));
}

bool IedSimulatorController::writeLiveStateSnapshot(const QString& path) const {
    if (path.isEmpty()) return false;
    QStringList keys = runtimeValues_.keys();
    std::sort(keys.begin(), keys.end());
    QVariantList snapshot;
    snapshot.reserve(keys.size());
    for (const auto& key : keys) snapshot.push_back(runtimeValues_.value(key));
    QSaveFile output{path};
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    output.write(QJsonDocument::fromVariant(snapshot).toJson(QJsonDocument::Indented));
    return output.commit();
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
        "Counts: IED=%1; LD=%2; materialized DO=%3; materialized DA=%4; "
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
    if (standardError) {
        qWarning().noquote() << "[IEDSIM server]" << line;
    } else {
        qInfo().noquote() << "[IEDSIM server]" << line;
    }
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
    if (kind == QStringLiteral("value_state")) {
        applyServerValueState(fields);
        return;
    }
    if (kind == QStringLiteral("value_rejected")) {
        const auto requestId = fields.value(QStringLiteral("request")).toULongLong();
        const bool wasUndo = pendingUndo_;
        if (requestId != 0U && requestId == pendingMutationRequest_) {
            pendingMutationRequest_ = 0U;
            pendingUndo_ = false;
            if (!wasUndo) previousValue_.reset();
        }
        appendActivity(
            QStringLiteral("Live state"),
            QStringLiteral("Mutation rejected: %1")
                .arg(liveUnhex(fields.value(QStringLiteral("reason")).toString())),
            QStringLiteral("Warning"));
        return;
    }
    if (kind == QStringLiteral("state_ready")) {
        appendActivity(
            QStringLiteral("Live state"),
            QStringLiteral("Server-authoritative live store ready: %1 values, rev %2, clock %3 ms.")
                .arg(fields.value(QStringLiteral("values")).toString())
                .arg(fields.value(QStringLiteral("revision")).toString())
                .arg(fields.value(QStringLiteral("clock_ms")).toString()),
            QStringLiteral("Success"));
        return;
    }
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
                QStringLiteral("%1 MMS leaves exceeded the host interoperability profile and were omitted.")
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

    QSaveFile output{serverModelManifestPath_};
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("Could not create the MMS model manifest: %1")
                .arg(output.errorString()),
            QStringLiteral("Error"));
        return false;
    }

    ++modelRevision_;
    output.write("ARSTACK_IED_MODEL\t4\t");
    output.write(QByteArray::number(modelRevision_));
    output.write("\n");

    QSet<QString> emittedObjects;
    const auto emitObject = [&](const QVariantMap& item) {
        const auto domain = item.value(QStringLiteral("mmsDomain")).toString();
        const auto mmsItem = item.value(QStringLiteral("mmsItem")).toString();
        const auto signature = item.value(QStringLiteral("mmsTypeSignature")).toString();
        if (domain.isEmpty() || mmsItem.isEmpty() || signature.isEmpty()) return;
        const auto key = domain + QLatin1Char('\n') + mmsItem;
        if (emittedObjects.contains(key)) return;
        emittedObjects.insert(key);
        output.write("OBJ\t");
        output.write(manifestField(domain));
        output.write("\t");
        output.write(manifestField(mmsItem));
        output.write("\t");
        output.write(manifestField(item.value(QStringLiteral("rawType")).toString()));
        output.write("\t");
        output.write(manifestField(item.value(QStringLiteral("type")).toString()));
        output.write("\t");
        output.write(manifestField(signature));
        output.write("\t");
        output.write(manifestField(item.value(QStringLiteral("value")).toString()));
        output.write("\n");
        if (item.value(QStringLiteral("mmsWritable")).toBool()) {
            output.write("MUT\t");
            output.write(manifestField(domain));
            output.write("\t");
            output.write(manifestField(mmsItem));
            output.write("\n");
        }
    };

    // Preserve SCL declaration order for the recursive TypeSpecification while
    // reading the current value from the unified Qt runtime state.
    for (const auto& loaded : documents_) {
        for (const auto& variant : loaded.materializedValues) {
            const auto declared = variant.toMap();
            const auto key = runtimeValueKey(declared);
            const auto found = runtimeValues_.constFind(key);
            emitObject(found == runtimeValues_.cend() ? declared : *found);
        }
    }

    // Keep deterministic host-only extensions, but never let enrichment-only
    // entries redefine the structural SCL model without an exact type.
    QStringList remainingKeys;
    remainingKeys.reserve(runtimeValues_.size());
    for (auto it = runtimeValues_.cbegin(); it != runtimeValues_.cend(); ++it) {
        if (!emittedObjects.contains(it.value().value(QStringLiteral("mmsDomain")).toString() +
                                    QLatin1Char('\n') +
                                    it.value().value(QStringLiteral("mmsItem")).toString()) &&
            !it.value().value(QStringLiteral("mmsTypeSignature")).toString().isEmpty()) {
            remainingKeys.push_back(it.key());
        }
    }
    std::sort(remainingKeys.begin(), remainingKeys.end());
    for (const auto& key : remainingKeys) emitObject(runtimeValues_.value(key));

    for (const auto& loaded : documents_) {
        for (const auto& dataSet : loaded.document.data_sets) {
            const auto dataSetDomain = qstring(dataSet.ied_name) + qstring(dataSet.ld_inst);
            const auto dataSetItem = qstring(dataSet.logical_node_path) + QLatin1Char('$') + qstring(dataSet.name);
            for (const auto& member : dataSet.entries) {
                const auto memberDomain = mmsDomainFor(member);
                const auto memberItem = mmsItemFor(member);
                if (dataSetDomain.isEmpty() || dataSetItem.isEmpty() ||
                    memberDomain.isEmpty() || memberItem.isEmpty()) continue;
                output.write("DS\t");
                output.write(manifestField(dataSetDomain));
                output.write("\t");
                output.write(manifestField(dataSetItem));
                output.write("\t");
                output.write(manifestField(memberDomain));
                output.write("\t");
                output.write(manifestField(memberItem));
                output.write("\n");
            }
        }

        for (const auto& report : loaded.document.report_controls) {
            const auto domain = qstring(report.ied_name) + qstring(report.ld_inst);
            const auto ln = qstring(report.logical_node_path);
            const auto name = qstring(report.name);
            if (domain.isEmpty() || ln.isEmpty() || name.isEmpty()) continue;
            const auto item = ln + (report.buffered ? QStringLiteral("$BR$") : QStringLiteral("$RP$")) + name;
            const auto dataSetDomain = domain;
            const auto dataSetItem = ln + QLatin1Char('$') + qstring(report.data_set_name);
            output.write("RCB\t");
            output.write(manifestField(domain));
            output.write("\t");
            output.write(manifestField(item));
            output.write("\t");
            output.write(report.buffered ? "1" : "0");
            output.write("\t");
            output.write(manifestField(qstring(report.report_id)));
            output.write("\t");
            output.write(manifestField(dataSetDomain));
            output.write("\t");
            output.write(manifestField(dataSetItem));
            output.write("\t");
            output.write(QByteArray::number((report.configuration_revision == 0U ? 1U : report.configuration_revision)));
            output.write("\t");
            output.write(QByteArray::number(report.buffer_time_milliseconds));
            output.write("\t");
            output.write(QByteArray::number(report.integrity_period_milliseconds));
            output.write("\t");
            output.write(report.indexed ? "1" : "0");
            const auto triggerMask = reportTriggerMask(report);
            const auto optionalMask = reportOptionalMask(report);
            output.write("\t");
            output.write(QByteArray::number(triggerMask));
            output.write("\t");
            output.write(QByteArray::number(optionalMask[0]));
            output.write("\t");
            output.write(QByteArray::number(optionalMask[1]));
            output.write("\n");
        }
    }

    // P3: materialize command services from SCL control metadata without
    // making CO a generic writable value. ctlModel/sboTimeout/operTimeout stay
    // regular read-only CF leaves; CTRL only binds the service runtime to the
    // authoritative ST/MX status leaf.
    for (const auto& loaded : documents_) {
        for (const auto& variant : loaded.materializedValues) {
            const auto ctl = variant.toMap();
            if (ctl.value(QStringLiteral("dataAttribute")).toString()
                    .compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) != 0) continue;
            bool modelOk = false;
            const auto model = ctl.value(QStringLiteral("value")).toString().toUInt(&modelOk);
            if (!modelOk || model < 1U || model > 4U) continue;
            const auto domain = ctl.value(QStringLiteral("mmsDomain")).toString();
            const auto ln = ctl.value(QStringLiteral("logicalNode")).toString();
            const auto dataObject = ctl.value(QStringLiteral("dataObject")).toString();
            if (domain.isEmpty() || ln.isEmpty() || dataObject.isEmpty()) continue;

            QString statusDomain;
            QString statusItem;
            quint64 sboTimeout = 10'000U;
            quint64 operTimeout = 1'000U;
            for (const auto& siblingVariant : loaded.materializedValues) {
                const auto sibling = siblingVariant.toMap();
                if (sibling.value(QStringLiteral("mmsDomain")).toString() != domain ||
                    sibling.value(QStringLiteral("logicalNode")).toString() != ln ||
                    sibling.value(QStringLiteral("dataObject")).toString() != dataObject) continue;
                const auto da = sibling.value(QStringLiteral("dataAttribute")).toString();
                const auto fc = sibling.value(QStringLiteral("fc")).toString();
                if (da.compare(QStringLiteral("stVal"), Qt::CaseInsensitive) == 0 &&
                    fc.compare(QStringLiteral("ST"), Qt::CaseInsensitive) == 0) {
                    statusDomain = sibling.value(QStringLiteral("mmsDomain")).toString();
                    statusItem = sibling.value(QStringLiteral("mmsItem")).toString();
                } else if (da.compare(QStringLiteral("sboTimeout"), Qt::CaseInsensitive) == 0) {
                    bool ok = false;
                    const auto parsed = sibling.value(QStringLiteral("value")).toString().toULongLong(&ok);
                    if (ok && parsed > 0U) sboTimeout = parsed;
                } else if (da.compare(QStringLiteral("operTimeout"), Qt::CaseInsensitive) == 0) {
                    bool ok = false;
                    const auto parsed = sibling.value(QStringLiteral("value")).toString().toULongLong(&ok);
                    if (ok && parsed > 0U) operTimeout = parsed;
                }
            }
            if (statusDomain.isEmpty() || statusItem.isEmpty()) continue;
            output.write("CTRL\t");
            output.write(manifestField(domain));
            output.write("\t");
            output.write(manifestField(ln));
            output.write("\t");
            output.write(manifestField(dataObject));
            output.write("\t");
            output.write(QByteArray::number(model));
            output.write("\t");
            output.write(manifestField(statusDomain));
            output.write("\t");
            output.write(manifestField(statusItem));
            output.write("\t");
            output.write(QByteArray::number(sboTimeout));
            output.write("\t");
            output.write(QByteArray::number(operTimeout));
            output.write("\n");
        }
    }

    if (emittedObjects.isEmpty()) {
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("The imported SCL produced no exact structural MMS objects."),
            QStringLiteral("Error"));
        output.cancelWriting();
        return false;
    }

    if (!output.commit()) {
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("Could not publish the MMS model manifest: %1")
                .arg(output.errorString()),
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

    seedRuntimeValues();
    std::set<QString> logicalDevices;
    std::set<QString> dataObjects;
    std::set<QString> dataAttributes;
    for (auto it = runtimeValues_.cbegin(); it != runtimeValues_.cend(); ++it) {
        const auto& item = it.value();
        const auto ied = item.value(QStringLiteral("iedName")).toString();
        const auto ld = item.value(QStringLiteral("logicalDevice")).toString();
        const auto ln = item.value(QStringLiteral("logicalNode")).toString();
        const auto dataObject = item.value(QStringLiteral("dataObject")).toString();
        const auto dataAttribute = item.value(QStringLiteral("dataAttribute")).toString();
        if (!ld.isEmpty()) logicalDevices.insert(ied + QLatin1Char('/') + ld);
        if (!dataObject.isEmpty()) {
            dataObjects.insert(ied + QLatin1Char('/') + ld + QLatin1Char('/') + ln +
                               QLatin1Char('/') + dataObject);
        }
        if (!dataAttribute.isEmpty()) {
            dataAttributes.insert(ied + QLatin1Char('/') + ld + QLatin1Char('/') + ln +
                                  QLatin1Char('/') + dataObject + QLatin1Char('/') + dataAttribute);
        }
    }
    logicalDeviceCount_ = static_cast<int>(logicalDevices.size());
    dataObjectCount_ = static_cast<int>(dataObjects.size());
    dataAttributeCount_ = static_cast<int>(dataAttributes.size());

    const auto maximumIedIndex = static_cast<int>(ieds_.size()) - 1;
    selectedIedIndex_ = ieds_.isEmpty()
        ? -1
        : std::clamp(selectedIedIndex_, 0, maximumIedIndex);
    if (selectedIedIndex_ < 0 && !ieds_.isEmpty()) selectedIedIndex_ = 0;
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
    const auto selectedName = ieds_.at(selectedIedIndex_).toMap()
        .value(QStringLiteral("name")).toString();
    std::vector<QVariantMap> selected;
    selected.reserve(static_cast<std::size_t>(runtimeValues_.size()));
    for (auto it = runtimeValues_.cbegin(); it != runtimeValues_.cend(); ++it) {
        const auto& item = it.value();
        const auto iedName = item.value(QStringLiteral("iedName")).toString();
        if (!iedName.isEmpty() && iedName != selectedName) continue;
        selected.push_back(item);
    }
    std::sort(selected.begin(), selected.end(), [](const auto& left, const auto& right) {
        const auto leftKey = left.value(QStringLiteral("mmsDomain")).toString() + QLatin1Char('/') +
            left.value(QStringLiteral("mmsItem")).toString();
        const auto rightKey = right.value(QStringLiteral("mmsDomain")).toString() + QLatin1Char('/') +
            right.value(QStringLiteral("mmsItem")).toString();
        return leftKey < rightKey;
    });
    for (const auto& item : selected) values_.push_back(item);
    selectedValueIndex_ = values_.isEmpty() ? -1 : 0;
    emit valuesChanged();
}

void IedSimulatorController::seedRuntimeValues() {
    const auto seedMap = [this](QVariantMap incoming) {
        const auto key = runtimeValueKey(incoming);
        if (key.isEmpty()) return;
        if (!runtimeValues_.contains(key)) {
            runtimeValues_.insert(key, std::move(incoming));
            return;
        }
        auto current = runtimeValues_.value(key);
        static const QStringList metadataFields{
            QStringLiteral("name"),
            QStringLiteral("reference"),
            QStringLiteral("logicalDevice"),
            QStringLiteral("logicalNode"),
            QStringLiteral("dataObject"),
            QStringLiteral("dataAttribute"),
            QStringLiteral("fc"),
            QStringLiteral("cdc"),
            QStringLiteral("type"),
            QStringLiteral("rawType"),
            QStringLiteral("mmsTypeSignature"),
            QStringLiteral("iedName"),
            QStringLiteral("mmsDomain"),
            QStringLiteral("mmsItem"),
            QStringLiteral("timestamp"),
            QStringLiteral("liveRevision"),
            QStringLiteral("options")};
        for (const auto& field : metadataFields) {
            const auto existing = current.value(field);
            if ((!existing.isValid() || existing.toString().isEmpty()) && incoming.contains(field)) {
                current.insert(field, incoming.value(field));
            }
        }
        current.insert(
            QStringLiteral("mmsWritable"),
            current.value(QStringLiteral("mmsWritable")).toBool() ||
                incoming.value(QStringLiteral("mmsWritable")).toBool());
        runtimeValues_.insert(key, std::move(current));
    };

    for (const auto& loaded : documents_) {
        for (const auto& value : loaded.materializedValues) seedMap(value.toMap());
        const auto seedEntry = [&](const ar::iec61850::scl::SclDataSetEntry& entry) {
            seedMap(valueMap(entry));
        };
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
    const auto fc = qstring(entry.functional_constraint);
    QVariantMap item;
    item.insert(QStringLiteral("name"), displayName(entry));
    item.insert(QStringLiteral("reference"), referenceFor(entry));
    item.insert(QStringLiteral("logicalDevice"), qstring(entry.ld_inst));
    item.insert(
        QStringLiteral("logicalNode"),
        qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst));
    item.insert(QStringLiteral("dataObject"), qstring(entry.do_name));
    item.insert(QStringLiteral("dataAttribute"), qstring(entry.da_name));
    item.insert(QStringLiteral("fc"), fc);
    item.insert(QStringLiteral("cdc"), qstring(entry.cdc));
    item.insert(QStringLiteral("type"), type);
    item.insert(QStringLiteral("rawType"), qstring(entry.basic_type));
    item.insert(QStringLiteral("iedName"), qstring(entry.ied_name));
    item.insert(QStringLiteral("mmsDomain"), mmsDomainFor(entry));
    item.insert(QStringLiteral("mmsItem"), mmsItemFor(entry));
    item.insert(QStringLiteral("value"), initialValue(type, qstring(entry.da_name)));
    item.insert(QStringLiteral("quality"), QStringLiteral("Good"));
    item.insert(QStringLiteral("origin"), QStringLiteral("scl"));
    item.insert(QStringLiteral("writable"), !entry.is_quality && !entry.is_timestamp);
    item.insert(
        QStringLiteral("mmsWritable"),
        mmsWritableFc(fc) && !entry.is_quality && !entry.is_timestamp);
    item.insert(QStringLiteral("changed"), false);
    item.insert(QStringLiteral("timestamp"), deterministicTimestamp(0U));
    item.insert(QStringLiteral("updated"), deterministicTimestamp(0U));
    item.insert(QStringLiteral("liveRevision"), 0ULL);
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