// SPDX-License-Identifier: GPL-3.0-or-later

#include "IedFleetController.hpp"
#include "IedRuntimeGuardrails.hpp"

#include "ariec61850/scl/parser.hpp"

#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QSaveFile>
#include <QSet>
#include <QTimer>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <set>
#include <string>

namespace {
QString qstring(const std::string& value) {
    return QString::fromStdString(value);
}

std::optional<int> controlModelCode(QString value) {
    value = value.trimmed();
    bool numericOk{};
    const auto numeric = value.toInt(&numericOk);
    if (numericOk && numeric >= 0 && numeric <= 4) return numeric;

    QString token;
    token.reserve(value.size());
    for (const auto character : value.toLower()) {
        if (character.isLetterOrNumber()) token.append(character);
    }
    if (token == QStringLiteral("statusonly")) return 0;
    if (token == QStringLiteral("directwithnormalsecurity") ||
        token == QStringLiteral("directnormal")) return 1;
    if (token == QStringLiteral("sbowithnormalsecurity") ||
        token == QStringLiteral("selectbeforeoperatewithnormalsecurity") ||
        token == QStringLiteral("sbonormal")) return 2;
    if (token == QStringLiteral("directwithenhancedsecurity") ||
        token == QStringLiteral("directenhanced")) return 3;
    if (token == QStringLiteral("sbowithenhancedsecurity") ||
        token == QStringLiteral("selectbeforeoperatewithenhancedsecurity") ||
        token == QStringLiteral("sboenhanced")) return 4;
    return std::nullopt;
}

QString mmsDomainFor(const ar::iec61850::scl::SclDataSetEntry& entry) {
    return qstring(entry.ied_name) + qstring(entry.ld_inst);
}

QString mmsItemFor(const ar::iec61850::scl::SclDataSetEntry& entry) {
    auto dataObject = qstring(entry.do_name);
    dataObject.replace(QLatin1Char('.'), QLatin1Char('$'));
    auto attribute = qstring(entry.da_name);
    attribute.replace(QLatin1Char('.'), QLatin1Char('$'));
    QStringList parts{
        qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst),
        qstring(entry.functional_constraint),
        dataObject};
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

bool isActiveState(const IedFleetController::RuntimeState state) {
    return state == IedFleetController::RuntimeState::starting ||
        state == IedFleetController::RuntimeState::running ||
        state == IedFleetController::RuntimeState::stopping;
}
} // namespace

IedFleetController::IedFleetController(QObject* parent)
    : QObject(parent) {
    connect(
        QCoreApplication::instance(),
        &QCoreApplication::aboutToQuit,
        this,
        [this] { stopAllProcessesBlocking(); });

    appendActivity(
        QStringLiteral("Workspace"),
        QStringLiteral("Ready. Import an IEC 61850 engineering model to begin."));
}

IedFleetController::~IedFleetController() {
    stopAllProcessesBlocking();
    removeModelManifests();
}

bool IedFleetController::imported() const noexcept { return !documents_.empty(); }

bool IedFleetController::running() const noexcept {
    const auto* runtime = runtimeAt(selectedIedIndex_);
    return runtime != nullptr && runtime->state == RuntimeState::running;
}

bool IedFleetController::starting() const noexcept {
    const auto* runtime = runtimeAt(selectedIedIndex_);
    return runtime != nullptr && runtime->state == RuntimeState::starting;
}

bool IedFleetController::anyRunning() const noexcept {
    for (const auto& runtime : runtimes_) {
        if (runtime != nullptr && isActiveState(runtime->state)) return true;
    }
    return false;
}

int IedFleetController::runningCount() const noexcept {
    int count{};
    for (const auto& runtime : runtimes_) {
        if (runtime != nullptr && runtime->state == RuntimeState::running) ++count;
    }
    return count;
}

QString IedFleetController::sourceName() const { return sourceName_; }
QString IedFleetController::sourcePath() const { return sourcePath_; }
QString IedFleetController::fatalError() const { return fatalError_; }

QString IedFleetController::modelStatus() const {
    if (!fatalError_.isEmpty()) return fatalError_;
    if (!imported()) return QStringLiteral("No engineering model loaded");
    return QStringLiteral("%1 IED%2 · %3 running · %4 local IPv4 endpoint%5 available")
        .arg(ieds_.size())
        .arg(ieds_.size() == 1 ? QString{} : QStringLiteral("s"))
        .arg(runningCount())
        .arg(bindableIpv4Addresses().size())
        .arg(bindableIpv4Addresses().size() == 1 ? QString{} : QStringLiteral("s"));
}

QString IedFleetController::listenAddress() const {
    const auto* runtime = runtimeAt(selectedIedIndex_);
    return runtime != nullptr ? runtime->listenAddress : defaultListenAddress_;
}

QStringList IedFleetController::availableAddresses() const {
    QStringList result{QStringLiteral("0.0.0.0")};
    const auto addresses = bindableIpv4Addresses();
    for (const auto& address : addresses) {
        if (!result.contains(address)) result.push_back(address);
    }
    if (!result.contains(QStringLiteral("127.0.0.1"))) {
        result.push_back(QStringLiteral("127.0.0.1"));
    }
    return result;
}

QVariantList IedFleetController::networkAddresses() const {
    QVariantList result;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& networkInterface : interfaces) {
        if (!networkInterface.flags().testFlag(QNetworkInterface::IsUp)) continue;
        for (const auto& entry : networkInterface.addressEntries()) {
            if (entry.ip().protocol() != QAbstractSocket::IPv4Protocol) continue;
            QVariantMap item;
            item.insert(QStringLiteral("address"), entry.ip().toString());
            item.insert(QStringLiteral("prefixLength"), entry.prefixLength());
            item.insert(QStringLiteral("interface"), networkInterface.humanReadableName());
            item.insert(QStringLiteral("interfaceName"), networkInterface.name());
            item.insert(QStringLiteral("loopback"), entry.ip().isLoopback());
            item.insert(
                QStringLiteral("label"),
                QStringLiteral("%1 · %2/%3")
                    .arg(networkInterface.humanReadableName(), entry.ip().toString())
                    .arg(entry.prefixLength()));
            result.push_back(item);
        }
    }
    return result;
}

int IedFleetController::port() const noexcept {
    const auto* runtime = runtimeAt(selectedIedIndex_);
    return runtime != nullptr ? runtime->port : defaultPort_;
}

bool IedFleetController::gooseEnabled() const noexcept { return gooseEnabled_; }
bool IedFleetController::fileServiceEnabled() const noexcept { return fileServiceEnabled_; }
QString IedFleetController::fileFolder() const { return fileFolder_; }
QVariantList IedFleetController::ieds() const { return ieds_; }
int IedFleetController::selectedIedIndex() const noexcept { return selectedIedIndex_; }
QVariantList IedFleetController::values() const { return values_; }
int IedFleetController::selectedValueIndex() const noexcept { return selectedValueIndex_; }
QVariantList IedFleetController::activity() const { return activity_; }
int IedFleetController::logicalDeviceCount() const noexcept { return logicalDeviceCount_; }
int IedFleetController::dataObjectCount() const noexcept { return dataObjectCount_; }
int IedFleetController::dataAttributeCount() const noexcept { return dataAttributeCount_; }
int IedFleetController::dataSetCount() const noexcept { return dataSetCount_; }
int IedFleetController::reportCount() const noexcept { return reportCount_; }
int IedFleetController::gooseCount() const noexcept { return gooseCount_; }

QVariantMap IedFleetController::selectedIed() const {
    if (selectedIedIndex_ < 0 || selectedIedIndex_ >= ieds_.size()) return {};
    return ieds_.at(selectedIedIndex_).toMap();
}

QVariantMap IedFleetController::selectedValue() const {
    if (selectedValueIndex_ < 0 || selectedValueIndex_ >= values_.size()) return {};
    return values_.at(selectedValueIndex_).toMap();
}

QString IedFleetController::endpointConflict() const {
    return endpointConflictFor(selectedIedIndex_, true);
}

void IedFleetController::setListenAddress(const QString& value) {
    const auto normalized = value.trimmed();
    auto* runtime = runtimeAt(selectedIedIndex_);
    if (runtime == nullptr) {
        if (defaultListenAddress_ == normalized) return;
        defaultListenAddress_ = normalized;
        emit configurationChanged();
        return;
    }
    if (isActiveState(runtime->state) || runtime->listenAddress == normalized) return;
    runtime->listenAddress = normalized;
    updateIedRuntimePresentation(selectedIedIndex_);
    emit configurationChanged();
}

void IedFleetController::setPort(const int value) {
    if (value < 1 || value > 65'535) return;
    auto* runtime = runtimeAt(selectedIedIndex_);
    if (runtime == nullptr) {
        if (defaultPort_ == value) return;
        defaultPort_ = value;
        emit configurationChanged();
        return;
    }
    if (isActiveState(runtime->state) || runtime->port == value) return;
    runtime->port = value;
    updateIedRuntimePresentation(selectedIedIndex_);
    emit configurationChanged();
}

void IedFleetController::setGooseEnabled(const bool value) {
    if (gooseEnabled_ == value) return;
    gooseEnabled_ = value;
    emit configurationChanged();
}

void IedFleetController::setFileServiceEnabled(const bool value) {
    if (fileServiceEnabled_ == value) return;
    fileServiceEnabled_ = value;
    emit configurationChanged();
}

void IedFleetController::setFileFolder(const QString& value) {
    if (fileFolder_ == value) return;
    fileFolder_ = value;
    emit configurationChanged();
}

bool IedFleetController::loadFile(const QUrl& fileUrl) {
    return importFile(fileUrl, false);
}

bool IedFleetController::addFile(const QUrl& fileUrl) {
    return importFile(fileUrl, true);
}

bool IedFleetController::importFile(const QUrl& fileUrl, const bool append) {
    const auto path = fileUrl.toLocalFile();
    if (path.isEmpty()) {
        fatalError_ = QStringLiteral("Choose a local SCL, CID, SCD, IID, or ICD file.");
        emit modelChanged();
        return false;
    }

    if (anyRunning()) {
        appendActivity(
            QStringLiteral("Workspace"),
            QStringLiteral("Stopping active IEDs before changing the engineering model."),
            QStringLiteral("Info"));
        stopAllProcessesBlocking();
    }

    try {
        auto document = ar::iec61850::scl::SclParser{}.load(
            std::filesystem::path{path.toStdWString()});
        if (!append) {
            removeModelManifests();
            runtimes_.clear();
            ieds_.clear();
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

void IedFleetController::clear() {
    stopAllProcessesBlocking();
    removeModelManifests();
    runtimes_.clear();
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
    emit configurationChanged();
    emit runtimeChanged();
}

void IedFleetController::selectIed(const int index) {
    const int normalized = index >= 0 && index < ieds_.size() ? index : -1;
    if (selectedIedIndex_ == normalized) return;
    selectedIedIndex_ = normalized;
    previousValue_.reset();
    rebuildValues();
    emit selectionChanged();
    emit configurationChanged();
    emit runtimeChanged();
}

void IedFleetController::selectValue(const int index) {
    const int normalized = index >= 0 && index < values_.size() ? index : -1;
    if (selectedValueIndex_ == normalized) return;
    selectedValueIndex_ = normalized;
    emit selectionChanged();
}

bool IedFleetController::startSimulation() {
    return startIed(selectedIedIndex_);
}

void IedFleetController::stopSimulation() {
    stopIed(selectedIedIndex_);
}

bool IedFleetController::startIed(const int index) {
    if (!imported() || index < 0 || index >= ieds_.size()) return false;
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || !runtime->enabled || isActiveState(runtime->state)) return false;

    if (runtime->listenAddress.trimmed().isEmpty()) {
        appendActivity(
            QStringLiteral("Network"),
            QStringLiteral("Assign a local IPv4 address before starting this IED."),
            QStringLiteral("Error"),
            ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
        return false;
    }
    if (!isLocalAddress(runtime->listenAddress)) {
        appendActivity(
            QStringLiteral("Network"),
            QStringLiteral("%1 is not currently assigned to this computer.")
                .arg(runtime->listenAddress),
            QStringLiteral("Error"),
            ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
        return false;
    }
    const auto conflict = endpointConflictFor(index, false);
    if (!conflict.isEmpty()) {
        appendActivity(
            QStringLiteral("Network"), conflict, QStringLiteral("Error"),
            ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
        return false;
    }

    const auto executable = serverExecutable();
    if (executable.isEmpty()) {
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("ariec61850_ied_simulator_server was not found beside the GUI."),
            QStringLiteral("Error"),
            ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
        return false;
    }
    if (!writeModelManifest(index)) return false;

    if (runtime->process == nullptr) {
        runtime->process = std::make_unique<QProcess>();
        connectRuntimeSignals(index);
    }
    runtime->standardOutputBuffer.clear();
    runtime->standardErrorBuffer.clear();
    runtime->standardOutputDrainScheduled = false;
    runtime->standardErrorDrainScheduled = false;
    runtime->standardOutputOverflowReported = false;
    runtime->standardErrorOverflowReported = false;
    ++runtime->startGeneration;
    const auto generation = runtime->startGeneration;
    setRuntimeState(index, RuntimeState::starting);
    runtime->process->setProgram(executable);
    runtime->process->setArguments({
        QStringLiteral("--host"), runtime->listenAddress,
        QStringLiteral("--port"), QString::number(runtime->port),
        QStringLiteral("--model-manifest"), runtime->modelManifestPath});
    runtime->process->start();

    const auto iedName = ieds_.at(index).toMap().value(QStringLiteral("name")).toString();
    appendActivity(
        QStringLiteral("Server"),
        QStringLiteral("Starting IEC 61850 MMS endpoint on %1:%2.")
            .arg(runtime->listenAddress)
            .arg(runtime->port),
        QStringLiteral("Info"),
        iedName);

    QTimer::singleShot(5'000, this, [this, index, generation] {
        auto* current = runtimeAt(index);
        if (current == nullptr || current->startGeneration != generation ||
            current->state != RuntimeState::starting || current->process == nullptr ||
            current->process->state() == QProcess::NotRunning) {
            return;
        }
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("Listener readiness was not confirmed within 5 seconds."),
            QStringLiteral("Error"),
            ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
        stopIed(index);
    });
    return true;
}

void IedFleetController::stopIed(const int index) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || runtime->process == nullptr ||
        runtime->process->state() == QProcess::NotRunning) {
        if (runtime != nullptr) setRuntimeState(index, RuntimeState::ready);
        return;
    }

    setRuntimeState(index, RuntimeState::stopping);
    appendActivity(
        QStringLiteral("Server"),
        QStringLiteral("Stopping MMS endpoint…"),
        QStringLiteral("Info"),
        ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
    // Invalidate all start/readiness timers from the current generation. The
    // delayed hard-kill is tied to this stop generation so it can never kill a
    // freshly restarted server that reuses the same QProcess object.
    const auto stopGeneration = ++runtime->startGeneration;
    auto* const process = runtime->process.get();
    process->terminate();
    QTimer::singleShot(900, this, [this, index, process, stopGeneration] {
        auto* current = runtimeAt(index);
        if (current == nullptr || current->process.get() != process ||
            current->startGeneration != stopGeneration ||
            current->state != RuntimeState::stopping) {
            return;
        }
        if (process->state() != QProcess::NotRunning) process->kill();
    });
}

int IedFleetController::startAllSimulations() {
    if (!imported()) return 0;
    const auto fleetConflict = fleetEndpointConflict();
    if (!fleetConflict.isEmpty()) {
        appendActivity(QStringLiteral("Network"), fleetConflict, QStringLiteral("Error"));
        return 0;
    }

    int started{};
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
    }

    for (int index = 0; index < ieds_.size(); ++index) {
        const auto* runtime = runtimeAt(index);
        if (runtime == nullptr || !runtime->enabled || isActiveState(runtime->state)) continue;
        if (startIed(index)) ++started;
    }
    if (started > 0) {
        appendActivity(
            QStringLiteral("Fleet"),
            QStringLiteral("Starting %1 IED endpoint%2.")
                .arg(started)
                .arg(started == 1 ? QString{} : QStringLiteral("s")),
            QStringLiteral("Success"));
    }
    return started;
}

void IedFleetController::stopAllSimulations() {
    for (int index = 0; index < static_cast<int>(runtimes_.size()); ++index) {
        if (const auto* runtime = runtimeAt(index); runtime != nullptr && isActiveState(runtime->state)) {
            stopIed(index);
        }
    }
}

bool IedFleetController::configureIedEndpoint(
    const int index,
    const QString& address,
    const int requestedPort) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || isActiveState(runtime->state) || requestedPort < 1 ||
        requestedPort > 65'535) {
        return false;
    }
    runtime->listenAddress = address.trimmed();
    runtime->port = requestedPort;
    updateIedRuntimePresentation(index);
    emit configurationChanged();
    if (index == selectedIedIndex_) emit selectionChanged();
    return true;
}

void IedFleetController::setIedEnabled(const int index, const bool enabled) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || isActiveState(runtime->state) || runtime->enabled == enabled) return;
    runtime->enabled = enabled;
    updateIedRuntimePresentation(index);
    emit configurationChanged();
}

bool IedFleetController::autoAssignIedAddresses() {
    if (anyRunning()) {
        appendActivity(
            QStringLiteral("Network"),
            QStringLiteral("Stop active IEDs before changing fleet IP assignments."),
            QStringLiteral("Warning"));
        return false;
    }

    const auto addresses = bindableIpv4Addresses();
    int enabledCount{};
    for (const auto& runtime : runtimes_) {
        if (runtime != nullptr && runtime->enabled) ++enabledCount;
    }
    if (enabledCount == 0) return true;

    int addressIndex{};
    int assigned{};
    for (int index = 0; index < static_cast<int>(runtimes_.size()); ++index) {
        auto* runtime = runtimeAt(index);
        if (runtime == nullptr || !runtime->enabled) continue;
        if (addressIndex < addresses.size()) {
            runtime->listenAddress = addresses.at(addressIndex++);
            ++assigned;
        } else if (enabledCount == 1) {
            runtime->listenAddress = QStringLiteral("127.0.0.1");
            ++assigned;
        } else {
            runtime->listenAddress.clear();
        }
        updateIedRuntimePresentation(index);
    }
    emit configurationChanged();
    emit selectionChanged();

    if (assigned < enabledCount) {
        appendActivity(
            QStringLiteral("Network"),
            QStringLiteral(
                "Assigned %1 of %2 enabled IEDs. Add more IPv4 addresses to the Ethernet adapter, "
                "then refresh interfaces and run Auto assign again.")
                .arg(assigned)
                .arg(enabledCount),
            QStringLiteral("Warning"));
        return false;
    }

    appendActivity(
        QStringLiteral("Network"),
        QStringLiteral("Assigned a distinct local IPv4 address to each enabled IED."),
        QStringLiteral("Success"));
    return true;
}

void IedFleetController::refreshNetworkInterfaces() {
    emit networkInterfacesChanged();
    emit modelChanged();
    appendActivity(
        QStringLiteral("Network"),
        QStringLiteral("Local network interfaces refreshed."));
}

bool IedFleetController::applySelectedValue(
    const QString& value,
    const QString& quality,
    const QString& origin) {
    if (!running() || selectedValueIndex_ < 0 || selectedValueIndex_ >= values_.size()) {
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
    const auto key = runtimeValueKey(
        item.value(QStringLiteral("iedName")).toString(),
        item.value(QStringLiteral("reference")).toString());
    runtimeValues_.insert(key, item);
    if (!writeModelManifest(selectedIedIndex_)) {
        values_[selectedValueIndex_] = previousValue_->value;
        const auto previousKey = runtimeValueKey(
            previousValue_->value.value(QStringLiteral("iedName")).toString(),
            previousValue_->value.value(QStringLiteral("reference")).toString());
        runtimeValues_.insert(previousKey, previousValue_->value);
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
        QStringLiteral("Success"),
        item.value(QStringLiteral("iedName")).toString());
    return true;
}

bool IedFleetController::undoLastChange() {
    if (!previousValue_.has_value() || previousValue_->iedIndex != selectedIedIndex_ ||
        previousValue_->valueIndex < 0 || previousValue_->valueIndex >= values_.size()) {
        return false;
    }
    const int index = previousValue_->valueIndex;
    const auto current = values_[index].toMap();
    const auto previous = previousValue_->value;
    values_[index] = previous;
    runtimeValues_.insert(
        runtimeValueKey(
            previous.value(QStringLiteral("iedName")).toString(),
            previous.value(QStringLiteral("reference")).toString()),
        previous);
    if (!writeModelManifest(selectedIedIndex_)) {
        values_[index] = current;
        runtimeValues_.insert(
            runtimeValueKey(
                current.value(QStringLiteral("iedName")).toString(),
                current.value(QStringLiteral("reference")).toString()),
            current);
        return false;
    }
    selectedValueIndex_ = index;
    previousValue_.reset();
    emit valuesChanged();
    emit selectionChanged();
    appendActivity(
        QStringLiteral("Value"),
        QStringLiteral("Last change to %1 was reverted.")
            .arg(previous.value(QStringLiteral("reference")).toString()),
        QStringLiteral("Info"),
        previous.value(QStringLiteral("iedName")).toString());
    return true;
}

void IedFleetController::clearActivity() {
    activity_.clear();
    emit activityChanged();
}

QString IedFleetController::diagnosticsText() const {
    QString text;
    text += QStringLiteral("ARStack IED Simulator diagnostics\n");
    text += QStringLiteral("Captured: %1\n")
        .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));
    text += QStringLiteral("Model: %1\nSource: %2\n")
        .arg(sourceName_.isEmpty() ? QStringLiteral("<none>") : sourceName_)
        .arg(sourcePath_.isEmpty() ? QStringLiteral("<none>") : sourcePath_);
    text += QStringLiteral("Fleet: %1 IEDs; %2 running\n")
        .arg(ieds_.size())
        .arg(runningCount());
    for (int index = 0; index < ieds_.size(); ++index) {
        const auto ied = ieds_.at(index).toMap();
        const auto* runtime = runtimeAt(index);
        text += QStringLiteral("  %1 | %2 | %3 | pid %4\n")
            .arg(ied.value(QStringLiteral("name")).toString())
            .arg(ied.value(QStringLiteral("endpoint")).toString())
            .arg(ied.value(QStringLiteral("status")).toString())
            .arg(runtime != nullptr && runtime->process != nullptr ? runtime->process->processId() : 0);
    }
    text += QStringLiteral(
        "MMS association profile: Authentication=None; AP-title=1,1,1,999,1; "
        "AE-qualifier=12; P-selector=00 00 00 01; S-selector=00 01; T-selector=00 01\n");
    text += QStringLiteral(
        "Counts: IED=%1; LD=%2; DO=%3; DA/BDA=%4; DataSet=%5; Report=%6; GOOSE=%7\n")
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
        text += QStringLiteral("%1 | %2 | %3 | %4 | %5\n")
            .arg(
                event.value(QStringLiteral("time")).toString(),
                event.value(QStringLiteral("severity")).toString(),
                event.value(QStringLiteral("ied")).toString(),
                event.value(QStringLiteral("category")).toString(),
                event.value(QStringLiteral("message")).toString());
    }
    return text;
}

void IedFleetController::copyDiagnostics() {
    if (auto* clipboard = QGuiApplication::clipboard(); clipboard != nullptr) {
        clipboard->setText(diagnosticsText());
        appendActivity(
            QStringLiteral("Diagnostics"),
            QStringLiteral("Fleet diagnostics copied to the clipboard."),
            QStringLiteral("Success"));
    }
}

void IedFleetController::rebuildPresentation() {
    QHash<QString, QVariantMap> previousConfigurations;
    for (int index = 0; index < ieds_.size(); ++index) {
        const auto ied = ieds_.at(index).toMap();
        const auto* runtime = runtimeAt(index);
        if (runtime == nullptr) continue;
        QVariantMap configuration;
        configuration.insert(QStringLiteral("address"), runtime->listenAddress);
        configuration.insert(QStringLiteral("port"), runtime->port);
        configuration.insert(QStringLiteral("enabled"), runtime->enabled);
        previousConfigurations.insert(
            ied.value(QStringLiteral("sessionKey")).toString(), configuration);
    }

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
            const auto name = qstring(ied.name);
            QVariantMap item;
            item.insert(QStringLiteral("name"), name);
            item.insert(QStringLiteral("manufacturer"), qstring(ied.manufacturer));
            item.insert(QStringLiteral("type"), qstring(ied.type));
            item.insert(QStringLiteral("configVersion"), qstring(ied.config_version));
            item.insert(QStringLiteral("documentIndex"), static_cast<int>(documentIndex));
            item.insert(QStringLiteral("sourcePath"), loaded.path);
            item.insert(QStringLiteral("sessionKey"), loaded.path + QLatin1Char('\x1f') + name);
            ieds_.push_back(item);
        }

        const auto collectEntry = [&](const ar::iec61850::scl::SclDataSetEntry& entry) {
            const auto ld = qstring(entry.ied_name) + QLatin1Char('/') + qstring(entry.ld_inst);
            logicalDevices.insert(ld);
            const auto logicalNode = qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst);
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
    }

    if (ieds_.isEmpty() && !documents_.empty()) {
        QVariantMap fallback;
        const auto name = QFileInfo(sourceName_).completeBaseName();
        fallback.insert(QStringLiteral("name"), name);
        fallback.insert(QStringLiteral("manufacturer"), QStringLiteral("SCL model"));
        fallback.insert(QStringLiteral("type"), QStringLiteral("IED"));
        fallback.insert(QStringLiteral("configVersion"), QString{});
        fallback.insert(QStringLiteral("documentIndex"), 0);
        fallback.insert(QStringLiteral("sourcePath"), documents_.front().path);
        fallback.insert(
            QStringLiteral("sessionKey"),
            documents_.front().path + QLatin1Char('\x1f') + name);
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

    rebuildRuntimeInstances(previousConfigurations);
    rebuildValues();
    emit modelChanged();
    emit selectionChanged();
    emit configurationChanged();
    emit runtimeChanged();
}

void IedFleetController::rebuildRuntimeInstances(
    const QHash<QString, QVariantMap>& previousConfigurations) {
    removeModelManifests();
    runtimes_.clear();
    runtimes_.reserve(static_cast<std::size_t>(ieds_.size()));

    const auto candidates = bindableIpv4Addresses();
    QSet<QString> reservedAddresses;
    int nextCandidate{};
    const bool multiIed = ieds_.size() > 1;

    for (int index = 0; index < ieds_.size(); ++index) {
        const auto ied = ieds_.at(index).toMap();
        const auto key = ied.value(QStringLiteral("sessionKey")).toString();
        const auto previous = previousConfigurations.value(key);

        auto runtime = std::make_unique<RuntimeInstance>();
        runtime->key = key;
        runtime->port = previous.isEmpty()
            ? defaultPort_
            : previous.value(QStringLiteral("port"), defaultPort_).toInt();
        runtime->enabled = previous.isEmpty()
            ? true
            : previous.value(QStringLiteral("enabled"), true).toBool();
        runtime->listenAddress = previous.isEmpty()
            ? (multiIed ? QString{} : defaultListenAddress_)
            : previous.value(QStringLiteral("address")).toString();

        // A wildcard listener consumes the port on all local interfaces. It is
        // convenient for one IED but fundamentally incompatible with a fleet
        // that wants the same IEC 61850 port on several specific laptop IPs.
        if (multiIed && runtime->listenAddress == QStringLiteral("0.0.0.0")) {
            runtime->listenAddress.clear();
        }

        if (multiIed && runtime->listenAddress.isEmpty()) {
            while (nextCandidate < candidates.size() &&
                   reservedAddresses.contains(candidates.at(nextCandidate))) {
                ++nextCandidate;
            }
            if (nextCandidate < candidates.size()) {
                runtime->listenAddress = candidates.at(nextCandidate++);
            }
        }
        if (!runtime->listenAddress.isEmpty() &&
            runtime->listenAddress != QStringLiteral("0.0.0.0")) {
            reservedAddresses.insert(runtime->listenAddress);
        }

        runtime->modelManifestPath = manifestPathFor(index);
        runtime->process = std::make_unique<QProcess>();
        runtimes_.push_back(std::move(runtime));
        connectRuntimeSignals(index);
        updateIedRuntimePresentation(index);
    }

    if (multiIed) {
        int unassigned{};
        for (const auto& runtime : runtimes_) {
            if (runtime != nullptr && runtime->enabled && runtime->listenAddress.isEmpty()) ++unassigned;
        }
        if (unassigned > 0) {
            appendActivity(
                QStringLiteral("Network"),
                QStringLiteral(
                    "%1 IED%2 still need a unique local IPv4 address. Add secondary IPv4 addresses "
                    "to the Ethernet adapter, refresh interfaces, then use Auto assign.")
                    .arg(unassigned)
                    .arg(unassigned == 1 ? QString{} : QStringLiteral("s")),
                QStringLiteral("Warning"));
        }
    }
}

void IedFleetController::rebuildValues() {
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

    ar::iec61850::simulation::IedSimulatorProfileFromSclOptions options;
    options.ied_name = ied.value(QStringLiteral("name")).toString().toStdString();
    options.runtime_ied_name = options.ied_name;
    const auto built = ar::iec61850::simulation::IedSimulatorProfileBuilder::build(
        documents_[static_cast<std::size_t>(documentIndex)].document,
        options);

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

    const auto iedName = ied.value(QStringLiteral("name")).toString();
    for (const auto* point : points) {
        const auto key = runtimeValueKey(iedName, qstring(point->reference));
        if (!runtimeValues_.contains(key)) runtimeValues_.insert(key, valueMap(*point));
        values_.push_back(runtimeValues_.value(key));
    }

    selectedValueIndex_ = values_.isEmpty() ? -1 : 0;
    emit valuesChanged();
}

void IedFleetController::seedRuntimeValues(const int iedIndex) {
    if (iedIndex < 0 || iedIndex >= ieds_.size()) return;
    const auto ied = ieds_.at(iedIndex).toMap();
    const int documentIndex = ied.value(QStringLiteral("documentIndex")).toInt();
    if (documentIndex < 0 || documentIndex >= static_cast<int>(documents_.size())) return;

    ar::iec61850::simulation::IedSimulatorProfileFromSclOptions options;
    options.ied_name = ied.value(QStringLiteral("name")).toString().toStdString();
    options.runtime_ied_name = options.ied_name;
    const auto built = ar::iec61850::simulation::IedSimulatorProfileBuilder::build(
        documents_[static_cast<std::size_t>(documentIndex)].document,
        options);
    const auto iedName = ied.value(QStringLiteral("name")).toString();
    for (const auto& device : built.profile.logical_devices) {
        for (const auto& node : device.logical_nodes) {
            for (const auto& point : node.points) {
                const auto key = runtimeValueKey(iedName, qstring(point.reference));
                if (!runtimeValues_.contains(key)) runtimeValues_.insert(key, valueMap(point));
            }
        }
    }
}

void IedFleetController::updateIedRuntimePresentation(const int index) {
    if (index < 0 || index >= ieds_.size()) return;
    const auto* runtime = runtimeAt(index);
    if (runtime == nullptr) return;
    auto item = ieds_.at(index).toMap();
    item.insert(QStringLiteral("status"), runtimeStateText(runtime->state));
    item.insert(QStringLiteral("running"), runtime->state == RuntimeState::running);
    item.insert(QStringLiteral("starting"), runtime->state == RuntimeState::starting);
    item.insert(QStringLiteral("enabled"), runtime->enabled);
    item.insert(QStringLiteral("listenAddress"), runtime->listenAddress);
    item.insert(QStringLiteral("port"), runtime->port);
    item.insert(
        QStringLiteral("endpoint"),
        runtime->listenAddress.isEmpty()
            ? QStringLiteral("Assign IP")
            : QStringLiteral("%1:%2").arg(runtime->listenAddress).arg(runtime->port));
    ieds_[index] = item;
}

void IedFleetController::connectRuntimeSignals(const int index) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || runtime->process == nullptr) return;
    auto* const process = runtime->process.get();

    connect(process, &QProcess::started, this, [this, index] {
        if (index < 0 || index >= ieds_.size()) return;
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("Server process launched; waiting for listener confirmation."),
            QStringLiteral("Info"),
            ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
    });

    connect(process, &QProcess::errorOccurred, this, [this, index](const QProcess::ProcessError error) {
        auto* current = runtimeAt(index);
        if (current == nullptr || current->process == nullptr) return;
        const bool expectedStop = current->state == RuntimeState::stopping;
        setRuntimeState(index, expectedStop ? RuntimeState::ready : RuntimeState::failed);
        if (!expectedStop) {
            appendActivity(
                QStringLiteral("Server"),
                QStringLiteral("MMS server process error %1: %2")
                    .arg(static_cast<int>(error))
                    .arg(current->process->errorString()),
                QStringLiteral("Error"),
                ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
        }
    });

    connect(
        process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        [this, index](const int exitCode, const QProcess::ExitStatus) {
            auto* current = runtimeAt(index);
            if (current == nullptr || current->process == nullptr) return;
            consumeServerOutput(
                index,
                current->standardOutputBuffer,
                current->process->readAllStandardOutput(),
                false);
            consumeServerOutput(
                index,
                current->standardErrorBuffer,
                current->process->readAllStandardError(),
                true);
            if (!current->standardOutputBuffer.isEmpty()) {
                bool truncated{};
                const auto tail = ar::iedsim::runtime_guardrails::boundedTail(
                    current->standardOutputBuffer, &truncated);
                processServerLine(index, QString::fromUtf8(tail).trimmed(), false);
                if (truncated) {
                    appendActivity(
                        QStringLiteral("Diagnostics"),
                        QStringLiteral("Final child-process stdout line was truncated at the safety limit."),
                        QStringLiteral("Warning"),
                        ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
                }
                current->standardOutputBuffer.clear();
            }
            if (!current->standardErrorBuffer.isEmpty()) {
                bool truncated{};
                const auto tail = ar::iedsim::runtime_guardrails::boundedTail(
                    current->standardErrorBuffer, &truncated);
                processServerLine(index, QString::fromUtf8(tail).trimmed(), true);
                if (truncated) {
                    appendActivity(
                        QStringLiteral("Diagnostics"),
                        QStringLiteral("Final child-process stderr line was truncated at the safety limit."),
                        QStringLiteral("Warning"),
                        ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
                }
                current->standardErrorBuffer.clear();
            }
            const bool wasActive = isActiveState(current->state);
            setRuntimeState(index, RuntimeState::ready);
            if (wasActive) {
                appendActivity(
                    QStringLiteral("Server"),
                    QStringLiteral("MMS endpoint stopped (exit %1).").arg(exitCode),
                    exitCode == 0 ? QStringLiteral("Info") : QStringLiteral("Warning"),
                    ieds_.at(index).toMap().value(QStringLiteral("name")).toString());
            }
        });

    connect(process, &QProcess::readyReadStandardOutput, this, [this, index] {
        auto* current = runtimeAt(index);
        if (current == nullptr || current->process == nullptr) return;
        consumeServerOutput(
            index,
            current->standardOutputBuffer,
            current->process->readAllStandardOutput(),
            false);
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, index] {
        auto* current = runtimeAt(index);
        if (current == nullptr || current->process == nullptr) return;
        consumeServerOutput(
            index,
            current->standardErrorBuffer,
            current->process->readAllStandardError(),
            true);
    });
}

void IedFleetController::setRuntimeState(const int index, const RuntimeState state) {
    auto* runtime = runtimeAt(index);
    if (runtime == nullptr || runtime->state == state) return;
    runtime->state = state;
    updateIedRuntimePresentation(index);
    emit runtimeChanged();
    emit modelChanged();
    if (index == selectedIedIndex_) emit selectionChanged();
}

void IedFleetController::stopAllProcessesBlocking() {
    for (auto& runtime : runtimes_) {
        if (runtime != nullptr && runtime->process != nullptr &&
            runtime->process->state() != QProcess::NotRunning) {
            runtime->process->terminate();
        }
    }

    QElapsedTimer gracefulDeadline;
    gracefulDeadline.start();
    for (auto& runtime : runtimes_) {
        if (runtime == nullptr || runtime->process == nullptr ||
            runtime->process->state() == QProcess::NotRunning) {
            continue;
        }
        const int remaining = std::max(0, 1'200 - static_cast<int>(gracefulDeadline.elapsed()));
        if (remaining > 0) runtime->process->waitForFinished(remaining);
    }

    for (auto& runtime : runtimes_) {
        if (runtime != nullptr && runtime->process != nullptr &&
            runtime->process->state() != QProcess::NotRunning) {
            runtime->process->kill();
        }
    }

    QElapsedTimer killDeadline;
    killDeadline.start();
    for (auto& runtime : runtimes_) {
        if (runtime == nullptr || runtime->process == nullptr ||
            runtime->process->state() == QProcess::NotRunning) {
            continue;
        }
        const int remaining = std::max(0, 500 - static_cast<int>(killDeadline.elapsed()));
        if (remaining > 0) runtime->process->waitForFinished(remaining);
    }

    for (int index = 0; index < static_cast<int>(runtimes_.size()); ++index) {
        if (auto* runtime = runtimeAt(index); runtime != nullptr) {
            runtime->state = RuntimeState::ready;
            updateIedRuntimePresentation(index);
        }
    }
    emit runtimeChanged();
    emit modelChanged();
}

void IedFleetController::appendActivity(
    const QString& category,
    const QString& message,
    const QString& severity,
    const QString& iedName) {
    QVariantMap event;
    event.insert(
        QStringLiteral("time"),
        QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz")));
    event.insert(QStringLiteral("category"), category);
    event.insert(QStringLiteral("message"), message);
    event.insert(QStringLiteral("severity"), severity);
    event.insert(QStringLiteral("ied"), iedName);
    activity_.push_front(event);
    while (activity_.size() > 300) activity_.removeLast();
    emit activityChanged();
}

void IedFleetController::consumeServerOutput(
    const int index,
    QByteArray& buffer,
    const QByteArray& bytes,
    const bool standardError) {
    const auto drained = ar::iedsim::runtime_guardrails::appendAndDrain(buffer, bytes);
    auto* current = runtimeAt(index);

    if (drained.droppedBytes > 0 && current != nullptr) {
        bool& reported = standardError
            ? current->standardErrorOverflowReported
            : current->standardOutputOverflowReported;
        if (!reported) {
            reported = true;
            appendActivity(
                QStringLiteral("Diagnostics"),
                QStringLiteral(
                    "Child-process %1 exceeded the %2 KiB framing buffer; excess bytes are discarded "
                    "to keep simulator memory bounded.")
                    .arg(standardError ? QStringLiteral("stderr") : QStringLiteral("stdout"))
                    .arg(ar::iedsim::runtime_guardrails::kMaxBufferedProcessBytes / 1024),
                QStringLiteral("Warning"),
                index >= 0 && index < ieds_.size()
                    ? ieds_.at(index).toMap().value(QStringLiteral("name")).toString()
                    : QString{});
        }
    }

    for (const auto& rawLine : drained.lines) {
        const auto line = QString::fromUtf8(rawLine).trimmed();
        if (!line.isEmpty()) processServerLine(index, line, standardError);
    }
    if (drained.truncatedLines > 0 && current != nullptr) {
        appendActivity(
            QStringLiteral("Diagnostics"),
            QStringLiteral("%1 child-process line%2 truncated at %3 KiB.")
                .arg(drained.truncatedLines)
                .arg(drained.truncatedLines == 1 ? QString{} : QStringLiteral("s"))
                .arg(ar::iedsim::runtime_guardrails::kMaxProcessLineBytes / 1024),
            QStringLiteral("Warning"),
            index >= 0 && index < ieds_.size()
                ? ieds_.at(index).toMap().value(QStringLiteral("name")).toString()
                : QString{});
    }

    if (!drained.moreCompleteLines || current == nullptr) return;
    bool& scheduled = standardError
        ? current->standardErrorDrainScheduled
        : current->standardOutputDrainScheduled;
    if (scheduled) return;
    scheduled = true;
    QTimer::singleShot(0, this, [this, index, standardError] {
        auto* runtime = runtimeAt(index);
        if (runtime == nullptr) return;
        bool& pending = standardError
            ? runtime->standardErrorDrainScheduled
            : runtime->standardOutputDrainScheduled;
        pending = false;
        auto& pendingBuffer = standardError
            ? runtime->standardErrorBuffer
            : runtime->standardOutputBuffer;
        consumeServerOutput(index, pendingBuffer, QByteArray{}, standardError);
    });
}

void IedFleetController::processServerLine(
    const int index,
    const QString& line,
    const bool standardError) {
    if (index < 0 || index >= ieds_.size()) return;
    const auto iedName = ieds_.at(index).toMap().value(QStringLiteral("name")).toString();

    if (qEnvironmentVariableIsSet("ARSTACK_IEDSIM_TRACE_SERVER")) {
        if (standardError) {
            qWarning().noquote() << '[' + iedName + ']' << line;
        } else {
            qInfo().noquote() << '[' + iedName + ']' << line;
        }
    }
    if (!line.startsWith(QStringLiteral("IEDSIM_EVENT "))) {
        appendActivity(
            QStringLiteral("MMS"),
            line,
            standardError ? QStringLiteral("Error") : QStringLiteral("Info"),
            iedName);
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
        setRuntimeState(index, RuntimeState::running);
        appendActivity(
            QStringLiteral("Server"),
            QStringLiteral("MMS listener ready on %1:%2 · %3 domains · %4 objects.")
                .arg(fields.value(QStringLiteral("bind")).toString())
                .arg(fields.value(QStringLiteral("port")).toString())
                .arg(fields.value(QStringLiteral("domains")).toString())
                .arg(fields.value(QStringLiteral("objects")).toString()),
            QStringLiteral("Success"),
            iedName);
        const auto truncated = fields.value(QStringLiteral("truncated")).toInt();
        if (truncated > 0) {
            appendActivity(
                QStringLiteral("Model"),
                QStringLiteral("%1 model objects exceeded the bounded host profile and were omitted.")
                    .arg(truncated),
                QStringLiteral("Warning"),
                iedName);
        }
        return;
    }
    if (kind == QStringLiteral("client_connected")) {
        appendActivity(
            QStringLiteral("TCP"),
            QStringLiteral("Client %1 connected · association %2.")
                .arg(fields.value(QStringLiteral("remote")).toString())
                .arg(fields.value(QStringLiteral("association")).toString()),
            QStringLiteral("Success"),
            iedName);
        return;
    }
    if (kind == QStringLiteral("protocol_stage")) {
        appendActivity(
            QStringLiteral("Protocol"),
            QStringLiteral("Association %1 reached %2 stage.")
                .arg(fields.value(QStringLiteral("association")).toString())
                .arg(fields.value(QStringLiteral("stage")).toString().toUpper()),
            QStringLiteral("Info"),
            iedName);
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
            accepted ? QStringLiteral("Info") : QStringLiteral("Warning"),
            iedName);
        return;
    }
    if (kind == QStringLiteral("client_closed")) {
        appendActivity(
            QStringLiteral("TCP"),
            QStringLiteral("Client %1 closed · RX %2 · TX %3 · %4.")
                .arg(fields.value(QStringLiteral("remote")).toString())
                .arg(fields.value(QStringLiteral("rx")).toString())
                .arg(fields.value(QStringLiteral("tx")).toString())
                .arg(fields.value(QStringLiteral("state")).toString()),
            QStringLiteral("Info"),
            iedName);
        return;
    }
    if (kind == QStringLiteral("report_sent")) {
        appendActivity(
            QStringLiteral("Report"),
            QStringLiteral("Report %1 sent · SqNum %2 · reason %3.")
                .arg(fields.value(QStringLiteral("rcb")).toString())
                .arg(fields.value(QStringLiteral("sqnum")).toString())
                .arg(fields.value(QStringLiteral("reason")).toString()),
            QStringLiteral("Success"),
            iedName);
        return;
    }
    if (kind != QStringLiteral("server_stopped")) {
        appendActivity(QStringLiteral("MMS"), line, QStringLiteral("Info"), iedName);
    }
}

bool IedFleetController::writeModelManifest(const int iedIndex) {
    if (iedIndex < 0 || iedIndex >= ieds_.size()) return false;
    auto* runtime = runtimeAt(iedIndex);
    if (runtime == nullptr) return false;
    if (runtime->modelManifestPath.isEmpty()) runtime->modelManifestPath = manifestPathFor(iedIndex);

    const auto activeIed = ieds_.at(iedIndex).toMap();
    const auto activeIedName = activeIed.value(QStringLiteral("name")).toString();
    const auto activeDocumentIndex = activeIed.value(QStringLiteral("documentIndex"), -1).toInt();
    if (activeIedName.isEmpty() || activeDocumentIndex < 0 ||
        activeDocumentIndex >= static_cast<int>(documents_.size())) {
        appendActivity(
            QStringLiteral("Model"),
            QStringLiteral("Select a valid IED before starting the simulator."),
            QStringLiteral("Error"),
            activeIedName);
        return false;
    }

    seedRuntimeValues(iedIndex);
    ++runtime->modelRevision;
    QByteArray manifest = "ARSTACK_IED_MODEL\t2\t" +
        QByteArray::number(runtime->modelRevision) + "\n";
    QSet<QString> uniqueRoots;
    const auto& loaded = documents_[static_cast<std::size_t>(activeDocumentIndex)];
    for (const auto& logicalNode : loaded.document.logical_nodes) {
        if (qstring(logicalNode.ied_name) != activeIedName) continue;
        const auto domain = qstring(logicalNode.mms_domain());
        const auto item = qstring(logicalNode.name);
        if (domain.isEmpty() || item.isEmpty()) continue;
        const auto key = domain + QLatin1Char('\n') + item;
        if (uniqueRoots.contains(key)) continue;
        uniqueRoots.insert(key);
        manifest += "LN\t" + manifestField(domain) + "\t" + manifestField(item) + "\n";
    }

    QSet<QString> emittedObjects;
    for (auto it = runtimeValues_.cbegin(); it != runtimeValues_.cend(); ++it) {
        const auto item = it.value();
        if (item.value(QStringLiteral("iedName")).toString() != activeIedName) continue;
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

    QSet<QString> emittedControls;
    for (const auto& entry : loaded.document.model_entries) {
        if (qstring(entry.ied_name) != activeIedName ||
            qstring(entry.functional_constraint).compare(QStringLiteral("CF"), Qt::CaseInsensitive) != 0 ||
            qstring(entry.da_name).compare(QStringLiteral("ctlModel"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        const auto model = controlModelCode(qstring(entry.configured_value));
        if (!model.has_value()) continue;
        const auto domain = mmsDomainFor(entry);
        const auto logicalNode = qstring(entry.prefix) + qstring(entry.ln_class) + qstring(entry.ln_inst);
        auto dataObject = qstring(entry.do_name);
        dataObject.replace(QLatin1Char('.'), QLatin1Char('$'));
        const auto cdc = qstring(entry.cdc).trimmed().toUpper();
        if (domain.isEmpty() || logicalNode.isEmpty() || dataObject.isEmpty() || cdc.isEmpty()) continue;
        const auto key = domain + QLatin1Char('\n') + logicalNode + QLatin1Char('\n') + dataObject;
        if (emittedControls.contains(key)) continue;
        emittedControls.insert(key);
        manifest += "CTL\t" + manifestField(domain) + "\t" + manifestField(logicalNode) +
            "\t" + manifestField(dataObject) + "\t" + manifestField(cdc) + "\t" +
            QByteArray::number(*model) + "\n";
    }

    QSet<QString> emittedDataSetMembers;
    for (const auto& dataSet : loaded.document.data_sets) {
        if (qstring(dataSet.ied_name) != activeIedName) continue;
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

    QSet<QString> emittedReportControls;
    for (const auto& report : loaded.document.report_controls) {
        if (qstring(report.ied_name) != activeIedName) continue;
        const auto domain = qstring(report.ied_name) + qstring(report.ld_inst);
        auto logicalNode = qstring(report.logical_node_path);
        logicalNode.replace(QLatin1Char('.'), QLatin1Char('$'));
        const auto name = qstring(report.name);
        auto dataSetItem = logicalNode + QLatin1Char('$') + qstring(report.data_set_name);
        dataSetItem.replace(QLatin1Char('.'), QLatin1Char('$'));
        const auto item = logicalNode +
            (report.buffered ? QStringLiteral("$BR$") : QStringLiteral("$RP$")) + name;
        if (domain.isEmpty() || logicalNode.isEmpty() || name.isEmpty() || dataSetItem.isEmpty()) continue;
        const auto key = domain + QLatin1Char('\n') + item;
        if (emittedReportControls.contains(key)) continue;
        emittedReportControls.insert(key);

        auto reportId = qstring(report.report_id);
        if (reportId.isEmpty()) reportId = domain + QLatin1Char('/') + item;
        const auto triggerOptions = report.buffered ? 0x6CU : 0x64U;
        const auto optionalFields0 = report.buffered ? 0x79U : 0x78U;
        constexpr auto optionalFields1 = 0x80U;
        manifest += "RCB\t" + manifestField(domain) + "\t" + manifestField(item) + "\t" +
            QByteArray::number(report.buffered ? 1 : 0) + "\t" +
            manifestField(reportId) + "\t" + manifestField(domain) + "\t" +
            manifestField(dataSetItem) + "\t" +
            QByteArray::number(report.configuration_revision) + "\t" +
            QByteArray::number(report.buffer_time_milliseconds) + "\t" +
            QByteArray::number(report.integrity_period_milliseconds) + "\t" +
            QByteArray::number(triggerOptions) + "\t" +
            QByteArray::number(optionalFields0) + "\t" +
            QByteArray::number(optionalFields1) + "\n";
    }

    if (uniqueRoots.isEmpty()) {
        appendActivity(
            QStringLiteral("Model"),
            QStringLiteral("The selected IED contains no discoverable logical-node roots."),
            QStringLiteral("Error"),
            activeIedName);
        return false;
    }
    if (emittedObjects.isEmpty()) {
        appendActivity(
            QStringLiteral("Model"),
            QStringLiteral("The selected IED contains no typed structural data-attribute leaves."),
            QStringLiteral("Error"),
            activeIedName);
        return false;
    }

    QSaveFile output{runtime->modelManifestPath};
    if (!output.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
        output.write(manifest) != manifest.size()) {
        appendActivity(
            QStringLiteral("Model"),
            QStringLiteral("Could not create the temporary MMS model manifest."),
            QStringLiteral("Error"),
            activeIedName);
        return false;
    }
    if (!output.commit()) {
        appendActivity(
            QStringLiteral("Model"),
            QStringLiteral("Could not publish the MMS model manifest atomically."),
            QStringLiteral("Error"),
            activeIedName);
        return false;
    }
    return true;
}

void IedFleetController::removeModelManifests() {
    for (auto& runtime : runtimes_) {
        if (runtime == nullptr || runtime->modelManifestPath.isEmpty()) continue;
        QFile::remove(runtime->modelManifestPath);
        runtime->modelManifestPath.clear();
    }
}

QString IedFleetController::manifestPathFor(const int iedIndex) const {
    const auto pid = QCoreApplication::applicationPid();
    if (iedIndex == 0) {
        // Preserve the long-standing QA path for the first IED while giving
        // every additional simulated IED its own atomically updated manifest.
        return QDir::temp().filePath(
            QStringLiteral("arstack-ied-simulator-%1.model").arg(pid));
    }
    return QDir::temp().filePath(
        QStringLiteral("arstack-ied-simulator-%1-%2.model").arg(pid).arg(iedIndex));
}

QString IedFleetController::serverExecutable() const {
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

bool IedFleetController::isLocalAddress(const QString& addressText) const {
    const auto text = addressText.trimmed();
    if (text == QStringLiteral("0.0.0.0")) return true;
    QHostAddress requested;
    if (!requested.setAddress(text) || requested.protocol() != QAbstractSocket::IPv4Protocol) return false;
    if (requested.isLoopback()) return true;
    const auto localAddresses = QNetworkInterface::allAddresses();
    return std::any_of(
        localAddresses.cbegin(), localAddresses.cend(),
        [&requested](const QHostAddress& candidate) {
            return candidate.protocol() == QAbstractSocket::IPv4Protocol && candidate == requested;
        });
}

QString IedFleetController::endpointConflictFor(
    const int index,
    const bool includeReadyPeers) const {
    const auto* runtime = runtimeAt(index);
    if (runtime == nullptr || !runtime->enabled) return {};
    if (runtime->listenAddress.isEmpty()) return QStringLiteral("This IED has no local IP assignment.");
    if (!isLocalAddress(runtime->listenAddress)) {
        return QStringLiteral("%1 is not assigned to an active local interface.")
            .arg(runtime->listenAddress);
    }

    for (int peerIndex = 0; peerIndex < static_cast<int>(runtimes_.size()); ++peerIndex) {
        if (peerIndex == index) continue;
        const auto* peer = runtimeAt(peerIndex);
        if (peer == nullptr || !peer->enabled || peer->listenAddress.isEmpty()) continue;
        if (!includeReadyPeers && !isActiveState(peer->state)) continue;
        if (peer->port != runtime->port) continue;
        if (peer->listenAddress == runtime->listenAddress ||
            peer->listenAddress == QStringLiteral("0.0.0.0") ||
            runtime->listenAddress == QStringLiteral("0.0.0.0")) {
            const auto peerName = peerIndex >= 0 && peerIndex < ieds_.size()
                ? ieds_.at(peerIndex).toMap().value(QStringLiteral("name")).toString()
                : QStringLiteral("another IED");
            return QStringLiteral("Endpoint conflicts with %1 on port %2. Use a distinct laptop IP address.")
                .arg(peerName)
                .arg(runtime->port);
        }
    }
    return {};
}

QString IedFleetController::fleetEndpointConflict() const {
    for (int index = 0; index < static_cast<int>(runtimes_.size()); ++index) {
        const auto* runtime = runtimeAt(index);
        if (runtime == nullptr || !runtime->enabled) continue;
        if (runtime->listenAddress.isEmpty()) {
            return QStringLiteral("One or more enabled IEDs do not have a local IP assignment.");
        }
        if (!isLocalAddress(runtime->listenAddress)) {
            return QStringLiteral("%1 is not assigned to an active local interface.")
                .arg(runtime->listenAddress);
        }
        const auto conflict = endpointConflictFor(index, true);
        if (!conflict.isEmpty()) return conflict;
    }
    return {};
}

QStringList IedFleetController::bindableIpv4Addresses() const {
    QStringList result;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const auto& networkInterface : interfaces) {
        if (!networkInterface.flags().testFlag(QNetworkInterface::IsUp) ||
            networkInterface.flags().testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const auto& entry : networkInterface.addressEntries()) {
            const auto address = entry.ip();
            if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()) continue;
            const auto text = address.toString();
            if (!result.contains(text)) result.push_back(text);
        }
    }
    return result;
}

IedFleetController::RuntimeInstance* IedFleetController::runtimeAt(const int index) noexcept {
    if (index < 0 || index >= static_cast<int>(runtimes_.size())) return nullptr;
    return runtimes_[static_cast<std::size_t>(index)].get();
}

const IedFleetController::RuntimeInstance* IedFleetController::runtimeAt(const int index) const noexcept {
    if (index < 0 || index >= static_cast<int>(runtimes_.size())) return nullptr;
    return runtimes_[static_cast<std::size_t>(index)].get();
}

QString IedFleetController::runtimeStateText(const RuntimeState state) const {
    switch (state) {
    case RuntimeState::ready: return QStringLiteral("Ready");
    case RuntimeState::starting: return QStringLiteral("Starting");
    case RuntimeState::running: return QStringLiteral("Running");
    case RuntimeState::stopping: return QStringLiteral("Stopping");
    case RuntimeState::failed: return QStringLiteral("Failed");
    }
    return QStringLiteral("Ready");
}

QString IedFleetController::runtimeValueKey(const QString& iedName, const QString& reference) {
    return iedName + QLatin1Char('\x1f') + reference;
}

QVariantMap IedFleetController::valueMap(
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
