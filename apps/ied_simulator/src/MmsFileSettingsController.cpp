// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsFileSettingsController.hpp"

#include "MmsSettingGroupPolicy.hpp"
#include "ariec61850/mms/control_block_read.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/file_service.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/services.hpp"

#include <QFile>
#include <QMetaObject>
#include <QPointer>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <functional>
#include <map>
#include <span>
#include <stdexcept>
#include <utility>

namespace mms = ar::iec61850::mms;

namespace {
std::span<const std::uint8_t> responsePayload(
    const mms::MmsConfirmedExchangeResult& exchange) {
    return exchange.presentation_payload.empty()
        ? std::span<const std::uint8_t>{exchange.envelope.mms_payload}
        : std::span<const std::uint8_t>{exchange.presentation_payload};
}

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](const char character) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    });
    return value;
}

bool containsFunctionalConstraint(
    const std::string& combined,
    const std::string& candidate) {
    std::size_t begin{};
    while (begin <= combined.size()) {
        const auto end = combined.find('/', begin);
        const auto token = combined.substr(
            begin, end == std::string::npos ? std::string::npos : end - begin);
        if (lowerAscii(token) == lowerAscii(candidate)) return true;
        if (end == std::string::npos) break;
        begin = end + 1U;
    }
    return false;
}

std::vector<mms::MmsControlBlockCandidate> mergedSettingGroups(
    const mms::MmsDiscoverySnapshot& snapshot) {
    const auto inventory = mms::MmsControlBlockInventoryBuilder::build(snapshot);
    std::vector<mms::MmsControlBlockCandidate> result;
    std::map<std::string, std::size_t> indexByKey;

    for (const auto& candidate : inventory) {
        if (candidate.kind != mms::MmsControlBlockKind::setting_group) continue;
        const auto key = lowerAscii(
            candidate.domain + "\x1f" + candidate.logical_node + "\x1f" + candidate.name);
        auto found = indexByKey.find(key);
        if (found == indexByKey.end()) {
            mms::MmsControlBlockCandidate merged;
            merged.kind = mms::MmsControlBlockKind::setting_group;
            merged.domain = candidate.domain;
            merged.logical_node = candidate.logical_node;
            merged.functional_constraint = candidate.functional_constraint;
            merged.name = candidate.name;
            merged.reference = candidate.domain + "/" + candidate.logical_node + "." + candidate.name;
            result.push_back(std::move(merged));
            found = indexByKey.emplace(key, result.size() - 1U).first;
        } else if (!containsFunctionalConstraint(
                       result[found->second].functional_constraint,
                       candidate.functional_constraint)) {
            result[found->second].functional_constraint += "/" + candidate.functional_constraint;
        }

        auto& merged = result[found->second];
        for (const auto& attribute : candidate.attributes) {
            const auto duplicate = std::any_of(
                merged.attributes.begin(), merged.attributes.end(), [&](const auto& existing) {
                    return lowerAscii(existing.variable.reference()) ==
                           lowerAscii(attribute.variable.reference());
                });
            if (!duplicate) merged.attributes.push_back(attribute);
        }
    }

    for (auto& candidate : result) {
        std::sort(candidate.attributes.begin(), candidate.attributes.end(), [](const auto& left, const auto& right) {
            return lowerAscii(left.attribute_path) < lowerAscii(right.attribute_path);
        });
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return lowerAscii(left.reference) < lowerAscii(right.reference);
    });
    return result;
}

QString displayValue(const mms::MmsControlBlockAttributeReadEvidence* attribute) {
    if (attribute == nullptr || !attribute->value) return {};
    return QString::fromStdString(mms::MmsDataCodec::to_display_string(*attribute->value));
}

QVariantMap settingGroupMap(const mms::MmsControlBlockReadResult& result) {
    QVariantMap map;
    map.insert(QStringLiteral("reference"), QString::fromStdString(result.candidate.reference));
    map.insert(QStringLiteral("domain"), QString::fromStdString(result.candidate.domain));
    map.insert(QStringLiteral("logicalNode"), QString::fromStdString(result.candidate.logical_node));
    map.insert(QStringLiteral("name"), QString::fromStdString(result.candidate.name));
    map.insert(QStringLiteral("functionalConstraints"),
               QString::fromStdString(result.candidate.functional_constraint));
    map.insert(QStringLiteral("complete"), result.complete());
    map.insert(QStringLiteral("error"), QString::fromStdString(result.error));

    QVariantList attributes;
    for (const auto& attribute : result.attributes) {
        QVariantMap item;
        item.insert(QStringLiteral("path"), QString::fromStdString(attribute.attribute_path));
        item.insert(QStringLiteral("reference"), QString::fromStdString(attribute.variable.reference()));
        item.insert(QStringLiteral("success"), attribute.success());
        item.insert(QStringLiteral("value"), displayValue(&attribute));
        item.insert(QStringLiteral("failureCode"),
                    attribute.failure_code ? QVariant::fromValue<qulonglong>(*attribute.failure_code)
                                           : QVariant{});
        attributes.push_back(item);
    }
    map.insert(QStringLiteral("attributes"), attributes);
    map.insert(QStringLiteral("attributeCount"), static_cast<int>(attributes.size()));

    const auto state = MmsSettingGroupPolicy::inspect(result);
    map.insert(QStringLiteral("numOfSG"),
               state.numOfSG ? QVariant::fromValue<qulonglong>(*state.numOfSG) : QVariant{});
    map.insert(QStringLiteral("actSG"),
               state.activeSG ? QVariant::fromValue<qulonglong>(*state.activeSG) : QVariant{});
    map.insert(QStringLiteral("editSG"),
               state.editSG ? QVariant::fromValue<qulonglong>(*state.editSG) : QVariant{});
    map.insert(QStringLiteral("cnfEdit"),
               state.cnfEdit ? QVariant{*state.cnfEdit} : QVariant{});
    map.insert(QStringLiteral("lActTm"),
               displayValue(MmsSettingGroupPolicy::findAttribute(result, "LActTm")));

    const auto blocked = MmsSettingGroupPolicy::activationBlockReason(state);
    map.insert(QStringLiteral("canActivate"), blocked.empty());
    map.insert(QStringLiteral("activationBlockedReason"), QString::fromStdString(blocked));
    map.insert(QStringLiteral("fullEditSupported"), false);
    return map;
}

QVariantMap failedSettingGroupMap(
    const mms::MmsControlBlockCandidate& candidate,
    const QString& error) {
    QVariantMap map;
    map.insert(QStringLiteral("reference"), QString::fromStdString(candidate.reference));
    map.insert(QStringLiteral("domain"), QString::fromStdString(candidate.domain));
    map.insert(QStringLiteral("logicalNode"), QString::fromStdString(candidate.logical_node));
    map.insert(QStringLiteral("name"), QString::fromStdString(candidate.name));
    map.insert(QStringLiteral("functionalConstraints"),
               QString::fromStdString(candidate.functional_constraint));
    map.insert(QStringLiteral("complete"), false);
    map.insert(QStringLiteral("error"), error);
    map.insert(QStringLiteral("attributes"), QVariantList{});
    map.insert(QStringLiteral("attributeCount"), 0);
    map.insert(QStringLiteral("canActivate"), false);
    map.insert(QStringLiteral("activationBlockedReason"), error);
    map.insert(QStringLiteral("fullEditSupported"), false);
    return map;
}

QVariantList readSettingGroups(
    mms::MmsAssociationRuntime& association,
    const std::vector<mms::MmsControlBlockCandidate>& candidates,
    const std::stop_token stopToken) {
    QVariantList maps;
    maps.reserve(static_cast<qsizetype>(candidates.size()));
    mms::MmsControlBlockReadClient reader{association};
    for (const auto& candidate : candidates) {
        if (stopToken.stop_requested()) {
            throw std::runtime_error("Setting Group refresh cancelled.");
        }
        try {
            maps.push_back(settingGroupMap(reader.read(candidate, 16U, stopToken)));
        } catch (const std::exception& exception) {
            maps.push_back(failedSettingGroupMap(
                candidate, QString::fromUtf8(exception.what())));
        }
    }
    return maps;
}

QString remoteTimestamp(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) return {};
    const auto printable = std::all_of(bytes.begin(), bytes.end(), [](const std::uint8_t value) {
        return value >= 0x20U && value <= 0x7EU;
    });
    if (printable) {
        return QString::fromLatin1(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<qsizetype>(bytes.size()));
    }
    return QString::fromLatin1(
        QByteArray(reinterpret_cast<const char*>(bytes.data()),
                   static_cast<qsizetype>(bytes.size())).toHex(' '));
}

QVariantList fileEntryMaps(const mms::MmsFileDirectoryResult& result) {
    QVariantList entries;
    entries.reserve(static_cast<qsizetype>(result.entries.size()));
    for (const auto& entry : result.entries) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), QString::fromStdString(entry.name));
        map.insert(QStringLiteral("path"), QString::fromStdString(entry.path));
        map.insert(QStringLiteral("directory"), entry.likely_directory());
        map.insert(QStringLiteral("sizeBytes"),
                   entry.size_bytes ? QVariant::fromValue<qulonglong>(*entry.size_bytes)
                                    : QVariant{});
        map.insert(QStringLiteral("modified"), remoteTimestamp(entry.last_modified));
        entries.push_back(map);
    }
    return entries;
}

QString localPathFromQml(const QString& value) {
    const QUrl url{value};
    if (url.isLocalFile()) return url.toLocalFile();
    return value;
}

class QtFileSink final : public mms::MmsFileSink {
public:
    explicit QtFileSink(QString path) : file_{std::move(path)} {}

    [[nodiscard]] bool open(std::string& error) noexcept {
        if (file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) return true;
        error = file_.errorString().toStdString();
        return false;
    }

    [[nodiscard]] bool write(
        const std::span<const std::uint8_t> data,
        std::string& error) noexcept override {
        const auto written = file_.write(
            reinterpret_cast<const char*>(data.data()),
            static_cast<qint64>(data.size()));
        if (written == static_cast<qint64>(data.size())) return true;
        error = file_.errorString().toStdString();
        return false;
    }

    [[nodiscard]] bool flush(std::string& error) noexcept override {
        if (file_.flush()) return true;
        error = file_.errorString().toStdString();
        return false;
    }

    [[nodiscard]] bool reset(std::string& error) noexcept override {
        if (!file_.resize(0) || !file_.seek(0)) {
            error = file_.errorString().toStdString();
            return false;
        }
        return true;
    }

private:
    QFile file_;
};

class CallbackProgressSink final : public mms::MmsFileProgressSink {
public:
    using Callback = std::function<void(const mms::MmsFileTransferProgress&)>;
    explicit CallbackProgressSink(Callback callback) : callback_{std::move(callback)} {}
    void report(const mms::MmsFileTransferProgress& progress) noexcept override {
        try {
            callback_(progress);
        } catch (...) {
        }
    }
private:
    Callback callback_;
};
} // namespace

struct MmsFileSettingsController::WorkerState final {
    std::unique_ptr<mms::MmsTcpLiveDiscoverySession> session;
    std::vector<mms::MmsControlBlockCandidate> settingGroups;
};

MmsFileSettingsController::MmsFileSettingsController(QObject* parent)
    : QObject(parent),
      workerState_(std::make_shared<WorkerState>()),
      sessionStopSource_(std::make_shared<std::stop_source>()),
      operationStopSource_(std::make_shared<std::stop_source>()) {
    ioPool_.setMaxThreadCount(1);
    ioPool_.setExpiryTimeout(-1);
}

MmsFileSettingsController::~MmsFileSettingsController() {
    if (operationStopSource_) operationStopSource_->request_stop();
    if (sessionStopSource_) sessionStopSource_->request_stop();
    ioPool_.clear();
    ioPool_.waitForDone();
}

void MmsFileSettingsController::setHost(const QString& value) {
    const auto normalized = value.trimmed();
    if (host_ == normalized) return;
    host_ = normalized;
    emit configurationChanged();
}

void MmsFileSettingsController::setPort(const int value) {
    if (value < 1 || value > 65'535 || port_ == value) return;
    port_ = value;
    emit configurationChanged();
}

bool MmsFileSettingsController::connected() const noexcept {
    return state_ == State::connected;
}

bool MmsFileSettingsController::busy() const noexcept {
    return state_ == State::connecting || state_ == State::discovering || operationBusy_;
}

QString MmsFileSettingsController::stateText() const {
    switch (state_) {
    case State::disconnected: return QStringLiteral("Disconnected");
    case State::connecting: return QStringLiteral("Connecting");
    case State::discovering: return QStringLiteral("Discovering SGCB");
    case State::connected: return operationBusy_ ? QStringLiteral("Working") : QStringLiteral("Connected");
    case State::faulted: return QStringLiteral("Faulted");
    }
    return {};
}

QString MmsFileSettingsController::endpoint() const {
    return host_ + QLatin1Char(':') + QString::number(port_);
}

double MmsFileSettingsController::downloadPercent() const noexcept {
    if (!downloadExpectedKnown_ || downloadExpectedBytes_ == 0U) return -1.0;
    return std::min(100.0,
        (static_cast<double>(downloadBytes_) * 100.0) /
        static_cast<double>(downloadExpectedBytes_));
}

void MmsFileSettingsController::appendDiagnostic(const QString& text) {
    if (text.isEmpty()) return;
    constexpr auto maximumDiagnostics = 128;
    while (diagnostics_.size() >= maximumDiagnostics) diagnostics_.removeFirst();
    diagnostics_.push_back(text);
    emit diagnosticsChanged();
}

QString MmsFileSettingsController::diagnosticsText() const {
    return diagnostics_.join(QLatin1Char('\n'));
}

void MmsFileSettingsController::setOperationBusy(const bool value) {
    if (operationBusy_ == value) return;
    operationBusy_ = value;
    emit stateChanged();
}

std::shared_ptr<std::stop_source> MmsFileSettingsController::replaceSessionStopSource() {
    if (sessionStopSource_) sessionStopSource_->request_stop();
    sessionStopSource_ = std::make_shared<std::stop_source>();
    return sessionStopSource_;
}

std::shared_ptr<std::stop_source> MmsFileSettingsController::replaceOperationStopSource() {
    if (operationStopSource_) operationStopSource_->request_stop();
    operationStopSource_ = std::make_shared<std::stop_source>();
    return operationStopSource_;
}

void MmsFileSettingsController::clearRemoteState() {
    associationProfile_.clear();
    currentDirectory_.clear();
    fileEntries_.clear();
    filePageCount_ = 0;
    settingGroups_.clear();
    selectedSettingGroupIndex_ = -1;
    selectedSettingGroup_.clear();
    emit filesChanged();
    emit settingsChanged();
    emit selectionChanged();
}

void MmsFileSettingsController::refreshSelectedSettingGroup() {
    if (selectedSettingGroupIndex_ < 0 ||
        selectedSettingGroupIndex_ >= settingGroups_.size()) {
        selectedSettingGroup_.clear();
    } else {
        selectedSettingGroup_ = settingGroups_.at(selectedSettingGroupIndex_).toMap();
    }
    emit selectionChanged();
}

bool MmsFileSettingsController::connectToIed() {
    if (busy() || connected()) return false;
    if (host_.trimmed().isEmpty() || port_ < 1 || port_ > 65'535) {
        lastError_ = QStringLiteral("A non-empty host and TCP port 1..65535 are required.");
        state_ = State::faulted;
        emit stateChanged();
        return false;
    }

    const auto sessionStop = replaceSessionStopSource();
    replaceOperationStopSource();
    const auto generation = ++generation_;
    const auto requestedHost = host_.trimmed();
    const auto requestedPort = port_;
    state_ = State::connecting;
    operationBusy_ = false;
    lastError_.clear();
    clearRemoteState();
    appendDiagnostic(QStringLiteral("Connect utility session %1:%2 · generation %3")
        .arg(requestedHost).arg(requestedPort).arg(generation));
    emit stateChanged();

    const QPointer<MmsFileSettingsController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, sessionStop, generation, requestedHost, requestedPort] {
        try {
            if (worker->session) worker->session->disconnect();
            worker->session.reset();
            worker->settingGroups.clear();

            mms::MmsAssociationOptions associationOptions;
            associationOptions.connect_timeout = std::chrono::milliseconds{5'000};
            associationOptions.request_timeout = std::chrono::milliseconds{5'000};
            auto session = std::make_unique<mms::MmsTcpLiveDiscoverySession>(
                mms::TcpMmsTransportOptions{}, associationOptions);
            session->connect(
                {requestedHost.toStdString(), static_cast<std::uint16_t>(requestedPort)},
                sessionStop->get_token());

            if (self) {
                QMetaObject::invokeMethod(self, [self, generation] {
                    if (!self || self->generation_ != generation) return;
                    self->state_ = State::discovering;
                    self->appendDiagnostic(QStringLiteral("Association accepted; discovering Setting Groups."));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }

            mms::MmsLiveDiscoveryOptions options;
            options.probe_variable_types = false;
            options.read_data_set_directories = false;
            options.probe_report_controls = false;
            options.maximum_pages_per_query = 256U;
            options.maximum_domains = 4'096U;
            options.maximum_names_per_domain = 65'536U;
            const auto discovery = session->discover(options, sessionStop->get_token());
            auto candidates = mergedSettingGroups(discovery.names);
            if (candidates.size() > 64U) {
                throw std::runtime_error("Setting Group inventory exceeds the desktop bound of 64 SGCBs.");
            }
            const auto settings = readSettingGroups(
                session->association(), candidates, sessionStop->get_token());
            const auto associationProfile =
                QString::fromStdString(session->association().active_association_profile());

            worker->settingGroups = candidates;
            worker->session = std::move(session);

            if (self) {
                QMetaObject::invokeMethod(self,
                    [self, generation, settings, associationProfile] {
                        if (!self || self->generation_ != generation) return;
                        self->associationProfile_ = associationProfile;
                        self->settingGroups_ = settings;
                        self->selectedSettingGroupIndex_ = settings.isEmpty() ? -1 : 0;
                        self->refreshSelectedSettingGroup();
                        self->state_ = State::connected;
                        self->lastError_.clear();
                        self->appendDiagnostic(
                            QStringLiteral("Utility discovery complete · %1 SGCB(s)")
                                .arg(settings.size()));
                        emit self->settingsChanged();
                        emit self->stateChanged();
                    },
                    Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (worker->session) worker->session->disconnect();
            worker->session.reset();
            worker->settingGroups.clear();
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->state_ = State::faulted;
                    self->lastError_ = message;
                    self->appendDiagnostic(QStringLiteral("Utility connection failed · %1").arg(message));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

void MmsFileSettingsController::disconnectFromIed() {
    if (operationStopSource_) operationStopSource_->request_stop();
    if (sessionStopSource_) sessionStopSource_->request_stop();
    sessionStopSource_ = std::make_shared<std::stop_source>();
    operationStopSource_ = std::make_shared<std::stop_source>();
    ++generation_;
    state_ = State::disconnected;
    operationBusy_ = false;
    downloadActive_ = false;
    lastError_.clear();
    clearRemoteState();
    emit transferChanged();
    appendDiagnostic(QStringLiteral("Utility session disconnected · generation invalidated."));
    emit stateChanged();

    const auto worker = workerState_;
    ioPool_.start([worker] {
        if (worker->session) worker->session->disconnect();
        worker->session.reset();
        worker->settingGroups.clear();
    });
}

bool MmsFileSettingsController::reconnect() {
    disconnectFromIed();
    return connectToIed();
}

bool MmsFileSettingsController::browseDirectory(const QString& remoteDirectory) {
    if (!connected() || operationBusy_) return false;
    const auto operationStop = replaceOperationStopSource();
    const auto generation = generation_;
    const auto requested = remoteDirectory.trimmed();
    setOperationBusy(true);
    lastError_.clear();
    appendDiagnostic(QStringLiteral("FileDirectory · %1")
        .arg(requested.isEmpty() ? QStringLiteral("/") : requested));

    const QPointer<MmsFileSettingsController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, operationStop, generation, requested] {
        try {
            if (!worker->session || !worker->session->associated()) {
                throw std::runtime_error("MMS association is not active.");
            }
            mms::MmsAssociationFileServiceChannel channel{worker->session->association()};
            mms::MmsFileTransferRuntime runtime{channel};
            mms::MmsFileDirectoryOptions options;
            options.maximum_pages = 64U;
            options.maximum_entries = 4'096U;
            options.maximum_diagnostics = 64U;
            options.maximum_diagnostic_bytes = 512U;
            const auto result = runtime.list_directory(
                requested.toStdString(), options, operationStop->get_token());
            if (!result.success) {
                throw std::runtime_error(result.message.empty()
                    ? "FileDirectory failed."
                    : result.message);
            }
            const auto entries = fileEntryMaps(result);
            const auto directory = QString::fromStdString(result.directory_name);
            const auto pages = static_cast<int>(result.pages.size());
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, entries, directory, pages] {
                    if (!self || self->generation_ != generation) return;
                    self->fileEntries_ = entries;
                    self->currentDirectory_ = directory;
                    self->filePageCount_ = pages;
                    self->lastError_.clear();
                    self->setOperationBusy(false);
                    self->appendDiagnostic(
                        QStringLiteral("FileDirectory complete · %1 entries · %2 page(s)")
                            .arg(entries.size()).arg(pages));
                    emit self->filesChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->lastError_ = message;
                    self->setOperationBusy(false);
                    self->appendDiagnostic(QStringLiteral("FileDirectory failed · %1").arg(message));
                    emit self->filesChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

bool MmsFileSettingsController::downloadFile(
    const QString& remotePath,
    const QString& localFileUrl) {
    if (!connected() || operationBusy_) return false;
    const auto normalizedRemote = remotePath.trimmed();
    const auto localPath = localPathFromQml(localFileUrl).trimmed();
    if (normalizedRemote.isEmpty() || localPath.isEmpty()) {
        lastError_ = QStringLiteral("Remote path and local Save As path are required.");
        emit stateChanged();
        return false;
    }

    const auto operationStop = replaceOperationStopSource();
    const auto generation = generation_;
    setOperationBusy(true);
    downloadActive_ = true;
    downloadBytes_ = 0U;
    downloadExpectedBytes_ = 0U;
    downloadExpectedKnown_ = false;
    lastDownloadRemotePath_ = normalizedRemote;
    lastDownloadLocalPath_ = localPath;
    lastError_.clear();
    appendDiagnostic(QStringLiteral("Download · %1").arg(normalizedRemote));
    emit transferChanged();

    const QPointer<MmsFileSettingsController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, operationStop, generation, normalizedRemote, localPath] {
        try {
            if (!worker->session || !worker->session->associated()) {
                throw std::runtime_error("MMS association is not active.");
            }
            QtFileSink sink{localPath};
            std::string sinkError;
            if (!sink.open(sinkError)) {
                throw std::runtime_error("Could not open local output file: " + sinkError);
            }

            std::uint64_t lastPublished{};
            CallbackProgressSink progress{[self, generation, &lastPublished](
                const mms::MmsFileTransferProgress& update) {
                const bool publish = update.complete || update.bytes_transferred == 0U ||
                    update.bytes_transferred >= lastPublished + 64U * 1024U;
                if (!publish) return;
                lastPublished = update.bytes_transferred;
                if (!self) return;
                const auto bytes = static_cast<qulonglong>(update.bytes_transferred);
                const auto expectedKnown = update.expected_bytes.has_value();
                const auto expected = expectedKnown
                    ? static_cast<qulonglong>(*update.expected_bytes)
                    : 0U;
                QMetaObject::invokeMethod(self,
                    [self, generation, bytes, expectedKnown, expected] {
                        if (!self || self->generation_ != generation) return;
                        self->downloadBytes_ = bytes;
                        self->downloadExpectedKnown_ = expectedKnown;
                        self->downloadExpectedBytes_ = expected;
                        emit self->transferChanged();
                    },
                    Qt::QueuedConnection);
            }};

            mms::MmsAssociationFileServiceChannel channel{worker->session->association()};
            mms::MmsFileTransferRuntime runtime{channel};
            mms::MmsFileTransferOptions options;
            options.maximum_bytes = 512ULL * 1024ULL * 1024ULL;
            options.maximum_read_operations = 100'000U;
            options.maximum_block_bytes = 1U * 1024U * 1024U;
            options.maximum_diagnostics = 64U;
            options.maximum_diagnostic_bytes = 512U;
            const auto result = runtime.download_adaptive(
                normalizedRemote.toStdString(), sink, options, &progress,
                operationStop->get_token());
            if (!result.success || !result.remote_file_closed) {
                QFile::remove(localPath);
                throw std::runtime_error(result.message.empty()
                    ? "File download failed or remote FileClose was not confirmed."
                    : result.message);
            }

            if (self) {
                const auto bytes = static_cast<qulonglong>(result.bytes_transferred);
                const auto expectedKnown = result.expected_bytes.has_value();
                const auto expected = expectedKnown
                    ? static_cast<qulonglong>(*result.expected_bytes)
                    : 0U;
                QMetaObject::invokeMethod(self,
                    [self, generation, bytes, expectedKnown, expected] {
                        if (!self || self->generation_ != generation) return;
                        self->downloadBytes_ = bytes;
                        self->downloadExpectedKnown_ = expectedKnown;
                        self->downloadExpectedBytes_ = expected;
                        self->downloadActive_ = false;
                        self->lastError_.clear();
                        self->setOperationBusy(false);
                        self->appendDiagnostic(
                            QStringLiteral("Download complete · %1 bytes · FileClose confirmed")
                                .arg(bytes));
                        emit self->transferChanged();
                    }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            QFile::remove(localPath);
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->downloadActive_ = false;
                    self->lastError_ = message;
                    self->setOperationBusy(false);
                    self->appendDiagnostic(QStringLiteral("Download failed · %1").arg(message));
                    emit self->transferChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

bool MmsFileSettingsController::cancelOperation() {
    if (!operationBusy_ || !operationStopSource_) return false;
    operationStopSource_->request_stop();
    appendDiagnostic(QStringLiteral("Cancel requested for active utility operation."));
    return true;
}

bool MmsFileSettingsController::refreshSettingGroups() {
    if (!connected() || operationBusy_) return false;
    const auto operationStop = replaceOperationStopSource();
    const auto generation = generation_;
    setOperationBusy(true);
    lastError_.clear();

    const QPointer<MmsFileSettingsController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, operationStop, generation] {
        try {
            if (!worker->session || !worker->session->associated()) {
                throw std::runtime_error("MMS association is not active.");
            }
            const auto settings = readSettingGroups(
                worker->session->association(), worker->settingGroups,
                operationStop->get_token());
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, settings] {
                    if (!self || self->generation_ != generation) return;
                    self->settingGroups_ = settings;
                    if (self->settingGroups_.isEmpty()) {
                        self->selectedSettingGroupIndex_ = -1;
                    } else if (self->selectedSettingGroupIndex_ < 0 ||
                               self->selectedSettingGroupIndex_ >= self->settingGroups_.size()) {
                        self->selectedSettingGroupIndex_ = 0;
                    }
                    self->refreshSelectedSettingGroup();
                    self->lastError_.clear();
                    self->setOperationBusy(false);
                    self->appendDiagnostic(
                        QStringLiteral("Setting Groups refreshed · %1 SGCB(s)")
                            .arg(settings.size()));
                    emit self->settingsChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->lastError_ = message;
                    self->setOperationBusy(false);
                    self->appendDiagnostic(QStringLiteral("Setting Group refresh failed · %1").arg(message));
                    emit self->settingsChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

bool MmsFileSettingsController::selectSettingGroup(const int row) {
    if (row < 0 || row >= settingGroups_.size()) return false;
    if (selectedSettingGroupIndex_ == row) return true;
    selectedSettingGroupIndex_ = row;
    refreshSelectedSettingGroup();
    return true;
}

bool MmsFileSettingsController::activateSelectedSettingGroup(const int group) {
    if (!connected() || operationBusy_ || selectedSettingGroupIndex_ < 0 || group < 1) {
        return false;
    }
    const auto maximum = selectedSettingGroup_.value(QStringLiteral("numOfSG")).toULongLong();
    if (maximum == 0U || static_cast<qulonglong>(group) > maximum) {
        lastError_ = QStringLiteral("Requested setting group is outside 1..NumOfSG.");
        appendDiagnostic(QStringLiteral("ActSG rejected · %1").arg(lastError_));
        emit stateChanged();
        return false;
    }
    if (!selectedSettingGroup_.value(QStringLiteral("canActivate")).toBool()) {
        lastError_ = selectedSettingGroup_.value(
            QStringLiteral("activationBlockedReason")).toString();
        if (lastError_.isEmpty()) {
            lastError_ = QStringLiteral("Selected SGCB is not safe for guarded activation.");
        }
        appendDiagnostic(QStringLiteral("ActSG rejected · %1").arg(lastError_));
        emit stateChanged();
        return false;
    }

    const auto row = selectedSettingGroupIndex_;
    const auto operationStop = replaceOperationStopSource();
    const auto generation = generation_;
    setOperationBusy(true);
    lastError_.clear();
    appendDiagnostic(QStringLiteral("Guarded ActSG request · group %1 · no retry").arg(group));

    const QPointer<MmsFileSettingsController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, operationStop, generation, row, group] {
        try {
            if (!worker->session || !worker->session->associated()) {
                throw std::runtime_error("MMS association is not active.");
            }
            if (row < 0 || static_cast<std::size_t>(row) >= worker->settingGroups.size()) {
                throw std::runtime_error("Selected SGCB is stale.");
            }

            auto& association = worker->session->association();
            const auto& candidate = worker->settingGroups[static_cast<std::size_t>(row)];
            mms::MmsControlBlockReadClient reader{association};
            const auto before = reader.read(candidate, 16U, operationStop->get_token());
            const auto state = MmsSettingGroupPolicy::inspect(before);
            MmsSettingGroupPolicy::validateActivation(
                state, static_cast<std::uint64_t>(group));
            const auto* activeAttribute =
                MmsSettingGroupPolicy::findAttribute(before, "ActSG");
            if (activeAttribute == nullptr || !activeAttribute->value) {
                throw std::runtime_error("ActSG exact MMS variable is unavailable.");
            }

            mms::MmsWriteRequest request;
            request.invoke_id = association.next_invoke_id();
            request.variables.push_back(activeAttribute->variable);
            request.values.push_back(MmsSettingGroupPolicy::activationValue(
                state, static_cast<std::uint64_t>(group)));
            const auto encoded = mms::MmsServiceCodec::encode_write_request_p_data(
                request, association.negotiated().presentation_context_id);
            const auto exchange = association.exchange_confirmed(
                encoded, request.invoke_id, operationStop->get_token());
            if (exchange.envelope.kind != mms::MmsPduKind::confirmed_response) {
                throw std::runtime_error("ActSG Write did not return MMS Confirmed-Response.");
            }
            const auto response = mms::MmsServiceCodec::decode_write_response(
                responsePayload(exchange), request.invoke_id);
            if (response.results.size() != 1U || !response.all_success()) {
                throw std::runtime_error("ActSG Write was rejected by the IED.");
            }

            const auto after = reader.read(candidate, 16U, operationStop->get_token());
            const auto verified = MmsSettingGroupPolicy::inspect(after);
            if (!verified.activeSG || *verified.activeSG != static_cast<std::uint64_t>(group)) {
                throw std::runtime_error("ActSG verification Read did not observe the requested group.");
            }
            const auto updated = settingGroupMap(after);

            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, row, group, updated] {
                    if (!self || self->generation_ != generation) return;
                    if (row >= 0 && row < self->settingGroups_.size()) {
                        self->settingGroups_[row] = updated;
                    }
                    if (self->selectedSettingGroupIndex_ == row) {
                        self->selectedSettingGroup_ = updated;
                        emit self->selectionChanged();
                    }
                    self->lastError_.clear();
                    self->setOperationBusy(false);
                    self->appendDiagnostic(
                        QStringLiteral("ActSG verified · group %1 · one Write + verification Read")
                            .arg(group));
                    emit self->settingsChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->lastError_ = message;
                    self->setOperationBusy(false);
                    self->appendDiagnostic(QStringLiteral("ActSG failed · no retry · %1").arg(message));
                    emit self->settingsChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}
