// SPDX-License-Identifier: GPL-3.0-or-later
#include "MmsReportController.hpp"

#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/dynamic_data_set.hpp"
#include "ariec61850/mms/live_discovery.hpp"
#include "ariec61850/mms/rcb_selection.hpp"
#include "ariec61850/mms/report_subscription_runtime.hpp"
#include "ariec61850/mms/static_report_session.hpp"

#include <QByteArray>
#include <QMetaObject>
#include <QPointer>

#include <algorithm>
#include <array>
#include <set>
#include <chrono>
#include <optional>
#include <span>
#include <stdexcept>
#include <utility>

namespace mms = ar::iec61850::mms;

namespace {
constexpr int kMaximumVisibleReports = 128;

QString boolText(const std::optional<bool> value) {
    if (!value.has_value()) return QStringLiteral("—");
    return *value ? QStringLiteral("true") : QStringLiteral("false");
}

QString numberText(const std::optional<std::uint64_t> value) {
    return value.has_value() ? QString::number(*value) : QStringLiteral("—");
}

QString hexBytes(const std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return {};
    const QByteArray raw{
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<int>(bytes.size())};
    return QString::fromLatin1(raw.toHex(' ')).toUpper();
}

QString availabilityText(const mms::MmsRcbAvailability value) {
    switch (value) {
    case mms::MmsRcbAvailability::unknown: return QStringLiteral("unknown");
    case mms::MmsRcbAvailability::available: return QStringLiteral("available");
    case mms::MmsRcbAvailability::in_use: return QStringLiteral("in-use");
    case mms::MmsRcbAvailability::used_by_caller: return QStringLiteral("caller-owned");
    case mms::MmsRcbAvailability::no_data_set: return QStringLiteral("no-dataset");
    case mms::MmsRcbAvailability::data_set_unreadable: return QStringLiteral("dataset-unreadable");
    case mms::MmsRcbAvailability::data_set_empty: return QStringLiteral("dataset-empty");
    }
    return QStringLiteral("unknown");
}

bool sameDataSetReference(const QString& left, const QString& right) {
    if (left == right) return true;
    try {
        return mms::MmsDataSetDirectoryCodec::parse_data_set_reference(left.toStdString()) ==
            mms::MmsDataSetDirectoryCodec::parse_data_set_reference(right.toStdString());
    } catch (...) {
        return false;
    }
}

std::vector<std::uint8_t> triggerPayload(const QStringList& names) {
    static const std::array<std::pair<QStringView, std::uint8_t>, 5> bits{{
        {u"data-change", 0x40U},
        {u"quality-change", 0x20U},
        {u"data-update", 0x10U},
        {u"integrity", 0x08U},
        {u"general-interrogation", 0x04U},
    }};
    std::set<QString> unique;
    std::uint8_t value{};
    for (const auto& raw : names) {
        const auto name = raw.trimmed();
        if (name.isEmpty() || !unique.insert(name).second) continue;
        const auto it = std::find_if(bits.begin(), bits.end(), [&name](const auto& item) {
            return item.first == name;
        });
        if (it == bits.end()) {
            throw std::invalid_argument(
                "Unknown IEC 61850 TrgOps name: " + name.toStdString());
        }
        value = static_cast<std::uint8_t>(value | it->second);
    }
    return {value};
}

std::vector<std::uint8_t> optionalPayload(const QStringList& names) {
    struct Bit final {
        QStringView name;
        int byte;
        std::uint8_t mask;
    };
    static const std::array<Bit, 9> bits{{
        {u"sequence-number", 0, 0x40U},
        {u"report-time-stamp", 0, 0x20U},
        {u"reason-for-inclusion", 0, 0x10U},
        {u"data-set-name", 0, 0x08U},
        {u"data-reference", 0, 0x04U},
        {u"buffer-overflow", 0, 0x02U},
        {u"entry-id", 0, 0x01U},
        {u"configuration-revision", 1, 0x80U},
        {u"segmentation", 1, 0x40U},
    }};
    std::set<QString> unique;
    std::vector<std::uint8_t> value(2U, 0U);
    for (const auto& raw : names) {
        const auto name = raw.trimmed();
        if (name.isEmpty() || !unique.insert(name).second) continue;
        const auto it = std::find_if(bits.begin(), bits.end(), [&name](const auto& item) {
            return item.name == name;
        });
        if (it == bits.end()) {
            throw std::invalid_argument(
                "Unknown IEC 61850 OptFlds name: " + name.toStdString());
        }
        value[static_cast<std::size_t>(it->byte)] =
            static_cast<std::uint8_t>(
                value[static_cast<std::size_t>(it->byte)] | it->mask);
    }
    return value;
}

QString bitNamesText(const QStringList& names) {
    return names.isEmpty() ? QStringLiteral("none") : names.join(QStringLiteral(", "));
}

QVariantMap candidateMap(const mms::MmsRcbCandidateEvaluation& candidate) {
    QVariantMap map;
    map.insert(QStringLiteral("reference"), QString::fromStdString(candidate.reference));
    map.insert(QStringLiteral("mode"), QString::fromStdString(candidate.mode));
    map.insert(QStringLiteral("dataSet"), QString::fromStdString(candidate.data_set_reference));
    map.insert(QStringLiteral("reportId"), QString::fromStdString(candidate.report_id));
    map.insert(QStringLiteral("score"), candidate.score);
    map.insert(QStringLiteral("availability"), QString::fromUtf8(mms::mms_rcb_availability_kind_name(candidate.availability).data()));
    map.insert(QStringLiteral("decision"), QString::fromUtf8(mms::mms_rcb_selection_decision_name(candidate.decision).data()));
    map.insert(QStringLiteral("reason"), QString::fromStdString(candidate.reason));
    map.insert(QStringLiteral("recommendedAction"), QString::fromStdString(candidate.recommended_action));
    map.insert(QStringLiteral("preferred"), candidate.preferred);
    map.insert(QStringLiteral("sameDataSet"), candidate.same_data_set);
    map.insert(QStringLiteral("sameLogicalDevice"), candidate.same_logical_device);
    map.insert(QStringLiteral("selectable"), candidate.selectable());
    return map;
}

struct DiscoveryUi final {
    QVariantList dataSets;
    QVariantList reportControls;
    QVariantList staticCandidates;
    QVariantList dynamicCandidates;
    QString associationProfile;
};

DiscoveryUi buildDiscoveryUi(
    const mms::MmsLiveDiscoveryResult& discovery,
    const QString& associationProfile) {
    DiscoveryUi ui;
    ui.associationProfile = associationProfile;

    for (const auto& candidate : discovery.report_inventory.data_sets) {
        QVariantMap map;
        map.insert(QStringLiteral("reference"), QString::fromStdString(candidate.reference));
        map.insert(QStringLiteral("domain"), QString::fromStdString(candidate.domain));
        map.insert(QStringLiteral("logicalNode"), QString::fromStdString(candidate.logical_node));
        map.insert(QStringLiteral("name"), QString::fromStdString(candidate.name));
        QStringList members;
        bool directoryAvailable = false;
        bool deletable = false;
        for (const auto& evidence : discovery.data_set_directories) {
            if (evidence.candidate.reference != candidate.reference || !evidence.directory) continue;
            directoryAvailable = true;
            deletable = evidence.directory->deletable;
            for (const auto& member : evidence.directory->members) {
                QString text = QString::fromStdString(member.user_reference);
                if (!member.functional_constraint.empty()) {
                    text += QStringLiteral("  [") + QString::fromStdString(member.functional_constraint) + QLatin1Char(']');
                }
                members.push_back(text);
            }
            break;
        }
        map.insert(QStringLiteral("directoryAvailable"), directoryAvailable);
        map.insert(QStringLiteral("deletable"), deletable);
        map.insert(QStringLiteral("memberCount"), members.size());
        map.insert(QStringLiteral("members"), members);
        ui.dataSets.push_back(map);
    }

    for (const auto& candidate : discovery.report_inventory.report_controls) {
        QVariantMap map;
        map.insert(QStringLiteral("reference"), QString::fromStdString(candidate.reference));
        map.insert(QStringLiteral("mode"), QString::fromStdString(candidate.mode()));
        map.insert(QStringLiteral("buffered"), candidate.buffered);
        map.insert(QStringLiteral("domain"), QString::fromStdString(candidate.domain));
        map.insert(QStringLiteral("logicalNode"), QString::fromStdString(candidate.logical_node));
        map.insert(QStringLiteral("name"), QString::fromStdString(candidate.name));
        QStringList attributes;
        for (const auto& item : candidate.attributes) attributes.push_back(QString::fromStdString(item));
        map.insert(QStringLiteral("attributes"), attributes);
        map.insert(QStringLiteral("supportsEntryId"), attributes.contains(QStringLiteral("EntryID")));
        map.insert(QStringLiteral("supportsPurgeBuf"), attributes.contains(QStringLiteral("PurgeBuf")));

        for (const auto& evidence : discovery.report_controls) {
            if (evidence.candidate.reference != candidate.reference) continue;
            map.insert(QStringLiteral("probeOk"), evidence.success());
            map.insert(QStringLiteral("probeError"), QString::fromStdString(evidence.error));
            if (evidence.state) {
                const auto& state = *evidence.state;
                map.insert(QStringLiteral("dataSet"), QString::fromStdString(state.data_set_reference));
                map.insert(QStringLiteral("reportId"), QString::fromStdString(state.report_id));
                map.insert(QStringLiteral("confRev"), numberText(state.configuration_revision));
                map.insert(QStringLiteral("bufTm"), numberText(state.buffer_time_ms));
                map.insert(QStringLiteral("intgPd"), numberText(state.integrity_period_ms));
                map.insert(QStringLiteral("sqNum"), numberText(state.sequence_number));
                map.insert(QStringLiteral("enabled"), boolText(state.report_enabled));
                map.insert(QStringLiteral("reserved"), boolText(state.reserved));
                map.insert(QStringLiteral("resvTms"), numberText(state.reservation_time_seconds));
                map.insert(QStringLiteral("owner"), hexBytes(state.owner));
                map.insert(QStringLiteral("entryId"), hexBytes(state.entry_id));
                map.insert(QStringLiteral("triggerOptions"), QString::fromStdString(
                    [&state] {
                        std::string result;
                        for (const auto& name : state.trigger_options.names) {
                            if (!result.empty()) result += ", ";
                            result += name;
                        }
                        return result;
                    }()));
                map.insert(QStringLiteral("optionalFields"), QString::fromStdString(
                    [&state] {
                        std::string result;
                        for (const auto& name : state.optional_fields.names) {
                            if (!result.empty()) result += ", ";
                            result += name;
                        }
                        return result;
                    }()));
                map.insert(QStringLiteral("availability"), availabilityText(state.availability));
                map.insert(QStringLiteral("availabilityReason"), QString::fromStdString(state.availability_reason));
            }
            break;
        }
        if (!map.contains(QStringLiteral("probeOk"))) map.insert(QStringLiteral("probeOk"), false);
        ui.reportControls.push_back(map);
    }

    try {
        const auto staticSelection = mms::MmsRcbPoolSelector::build_static_selection(discovery);
        for (const auto& candidate : staticSelection.candidates) ui.staticCandidates.push_back(candidateMap(candidate));
    } catch (...) {
    }
    try {
        const auto dynamicSelection = mms::MmsRcbPoolSelector::build_dynamic_selection(discovery);
        for (const auto& candidate : dynamicSelection.candidates) ui.dynamicCandidates.push_back(candidateMap(candidate));
    } catch (...) {
    }
    return ui;
}


mms::MmsObjectName contextObjectName(
    const std::string& reference,
    const std::string& fallbackDomain,
    const std::string& fallbackItem) {
    const auto slash = reference.find('/');
    if (slash != std::string::npos && slash > 0U && slash + 1U < reference.size()) {
        return mms::MmsObjectName::domain_specific(
            reference.substr(0U, slash), reference.substr(slash + 1U));
    }
    return mms::MmsObjectName::domain_specific(fallbackDomain, fallbackItem);
}

std::span<const std::uint8_t> confirmedPayload(
    const mms::MmsConfirmedExchangeResult& exchange) {
    return exchange.presentation_payload.empty()
        ? std::span<const std::uint8_t>{exchange.envelope.mms_payload}
        : std::span<const std::uint8_t>{exchange.presentation_payload};
}

mms::MmsLiveDiscoveryResult buildContextDiscoverySeed(
    const mms::MmsLiveModelDocument& model) {
    mms::MmsLiveDiscoveryResult result;
    result.endpoint = model.endpoint;
    result.association_profile = "CanonicalEngineeringContext";

    for (const auto& dataSet : model.data_sets) {
        mms::MmsDataSetCandidate candidate;
        candidate.domain = dataSet.domain;
        candidate.logical_node = dataSet.logical_node;
        candidate.name = dataSet.name;
        candidate.reference = dataSet.reference;
        candidate.raw_mms_name = dataSet.logical_node +
            (dataSet.name.empty() ? std::string{} : "$" + dataSet.name);
        result.report_inventory.data_sets.push_back(candidate);

        mms::MmsDataSetDirectoryEvidence evidence;
        evidence.candidate = candidate;
        mms::MmsDataSetDirectoryResponse directory;
        directory.deletable = dataSet.deletable.value_or(false);
        directory.members.reserve(dataSet.members.size());
        for (const auto& member : dataSet.members) {
            mms::MmsDataSetDirectoryMember projected;
            projected.object_name = contextObjectName(
                member.mms_reference, dataSet.domain, member.reference);
            projected.mms_reference = member.mms_reference.empty()
                ? projected.object_name.reference()
                : member.mms_reference;
            projected.user_reference = member.reference.empty()
                ? projected.mms_reference
                : member.reference;
            projected.functional_constraint = member.functional_constraint;
            projected.logical_node = dataSet.logical_node;
            projected.confidence = 100U;
            directory.members.push_back(std::move(projected));
        }
        evidence.directory = std::move(directory);
        result.data_set_directories.push_back(std::move(evidence));
    }

    static const std::vector<std::string> reportAttributes{
        "DatSet", "RptID", "ConfRev", "IntgPd", "BufTm", "SqNum",
        "RptEna", "Resv", "ResvTms", "Owner", "EntryID", "TimeOfEntry",
        "TrgOps", "OptFlds"};

    for (const auto& control : model.report_controls) {
        mms::MmsReportControlCandidate candidate;
        candidate.domain = control.domain;
        candidate.logical_node = control.logical_node;
        candidate.functional_constraint = control.buffered ? "BR" : "RP";
        candidate.name = control.name;
        candidate.reference = control.reference;
        candidate.buffered = control.buffered;
        candidate.attributes = reportAttributes;
        result.report_inventory.report_controls.push_back(std::move(candidate));
    }
    return result;
}

void probeContextReportControls(
    mms::MmsAssociationRuntime& association,
    mms::MmsLiveDiscoveryResult& discovery,
    const std::stop_token stopToken) {
    if (discovery.report_inventory.report_controls.size() > 1'024U) {
        throw std::runtime_error(
            "Canonical report inventory exceeds the desktop bound of 1024 RCBs.");
    }
    discovery.report_controls.clear();
    discovery.report_controls.reserve(discovery.report_inventory.report_controls.size());
    for (const auto& candidate : discovery.report_inventory.report_controls) {
        if (stopToken.stop_requested()) {
            throw std::runtime_error("Canonical report service attach cancelled.");
        }
        mms::MmsReportControlEvidence evidence;
        evidence.candidate = candidate;
        evidence.requested_attributes = candidate.attributes;
        try {
            const auto invokeId = association.next_invoke_id();
            const auto request = mms::MmsReportControlStateMapper::build_read_request(
                invokeId, candidate, evidence.requested_attributes);
            const auto encoded = mms::MmsServiceCodec::encode_read_request_p_data(
                request, association.negotiated().presentation_context_id);
            const auto exchange = association.exchange_confirmed(
                encoded, invokeId, stopToken);
            const auto response = mms::MmsServiceCodec::decode_read_response(
                confirmedPayload(exchange), invokeId);
            evidence.state = mms::MmsReportControlStateMapper::map_read_response(
                candidate, evidence.requested_attributes, response);
        } catch (const std::exception& exception) {
            evidence.error = exception.what();
            discovery.diagnostics.push_back(
                candidate.reference + ": targeted RCB attach read failed: " + evidence.error);
        }
        discovery.report_controls.push_back(std::move(evidence));
    }
}

struct ReportUi final {
    QVariantList reports;
    QStringList events;
    qulonglong receivedCount{};
    bool active{};
    bool cleanupRequired{};
};

QString reportValueText(const mms::MmsReportValue& item) {
    QString value = QStringLiteral("<failed>");
    if (item.value) value = QString::fromStdString(mms::MmsDataCodec::to_display_string(*item.value));
    QString reference = QString::fromStdString(item.data_reference);
    if (reference.isEmpty() && item.member) reference = QString::fromStdString(item.member->user_reference);
    if (reference.isEmpty()) reference = QStringLiteral("member[%1]").arg(static_cast<qulonglong>(item.data_set_index));
    QString reasons;
    for (const auto& reason : item.reason_for_inclusion.names) {
        if (!reasons.isEmpty()) reasons += QStringLiteral(", ");
        reasons += QString::fromStdString(reason);
    }
    return QStringLiteral("%1 = %2%3")
        .arg(reference, value, reasons.isEmpty() ? QString{} : QStringLiteral("  · ") + reasons);
}

ReportUi buildSubscriptionUi(
    const mms::MmsReportSubscriptionSnapshot& subscription,
    const bool active) {
    ReportUi ui;
    ui.active = active;
    ui.receivedCount = subscription.received_reports;
    ui.cleanupRequired = subscription.cleanup_required;
    for (const auto& event : subscription.events) {
        ui.events.push_back(QString::fromStdString(event.message));
    }
    while (ui.events.size() > 256) ui.events.removeFirst();

    for (auto streamIt = subscription.streams.rbegin();
         streamIt != subscription.streams.rend(); ++streamIt) {
        for (auto frameIt = streamIt->recent_frames.rbegin();
             frameIt != streamIt->recent_frames.rend(); ++frameIt) {
            if (ui.reports.size() >= kMaximumVisibleReports) break;
            const auto& frame = *frameIt;
            QVariantMap map;
            map.insert(QStringLiteral("reportId"), QString::fromStdString(frame.header.report_id));
            map.insert(QStringLiteral("dataSet"), QString::fromStdString(frame.header.data_set_reference));
            map.insert(QStringLiteral("sequence"), numberText(frame.header.sequence_number));
            map.insert(QStringLiteral("subSequence"), numberText(frame.header.sub_sequence_number));
            map.insert(QStringLiteral("confRev"), numberText(frame.header.configuration_revision));
            map.insert(QStringLiteral("entryId"), hexBytes(frame.header.entry_id));
            map.insert(QStringLiteral("overflow"), frame.header.buffer_overflow.value_or(false));
            map.insert(QStringLiteral("decoderMode"), QString::fromStdString(frame.decoder_mode));
            map.insert(QStringLiteral("valueCount"), static_cast<int>(frame.values.size()));
            QStringList values;
            for (const auto& value : frame.values) values.push_back(reportValueText(value));
            map.insert(QStringLiteral("values"), values);
            map.insert(QStringLiteral("valuesSummary"), values.mid(0, 3).join(QStringLiteral(" | ")));
            map.insert(QStringLiteral("duplicateCount"), static_cast<qulonglong>(streamIt->state.duplicate_count));
            map.insert(QStringLiteral("gapCount"), static_cast<qulonglong>(streamIt->state.gap_count));
            map.insert(QStringLiteral("resetCount"), static_cast<qulonglong>(streamIt->state.reset_count));
            map.insert(QStringLiteral("overflowCount"), static_cast<qulonglong>(streamIt->state.buffer_overflow_count));
            ui.reports.push_back(map);
        }
        if (ui.reports.size() >= kMaximumVisibleReports) break;
    }
    return ui;
}

ReportUi buildReportUi(const mms::MmsStaticReportSessionSnapshot& snapshot) {
    if (!snapshot.subscription) {
        ReportUi ui;
        ui.active = snapshot.active;
        return ui;
    }
    return buildSubscriptionUi(*snapshot.subscription, snapshot.active);
}

ReportUi buildReportUi(const mms::MmsReportSubscriptionSnapshot& snapshot) {
    return buildSubscriptionUi(
        snapshot,
        snapshot.state == mms::MmsReportSubscriptionState::active);
}

} // namespace

struct MmsReportController::WorkerState final {
    std::unique_ptr<mms::MmsTcpLiveDiscoverySession> session;
    std::shared_ptr<mms::MmsLiveDiscoveryResult> discovery;
    std::unique_ptr<mms::MmsStaticReportSessionRuntime> report;
    std::unique_ptr<mms::MmsReportSubscriptionRuntime> authoredReport;
    std::unique_ptr<mms::MmsDynamicDataSetRuntime> dynamicDataSets;
};

MmsReportController::MmsReportController(QObject* parent)
    : QObject(parent), workerState_(std::make_shared<WorkerState>()),
      stopSource_(std::make_shared<std::stop_source>()) {
    ioPool_.setMaxThreadCount(1);
    ioPool_.setExpiryTimeout(-1);
    pollTimer_.setInterval(100);
    pollTimer_.setTimerType(Qt::PreciseTimer);
    connect(&pollTimer_, &QTimer::timeout, this, &MmsReportController::schedulePoll);
}

MmsReportController::~MmsReportController() {
    if (stopSource_) stopSource_->request_stop();
    pollTimer_.stop();
    ioPool_.clear();
    ioPool_.waitForDone();
}

void MmsReportController::setHost(const QString& value) {
    const auto normalized = value.trimmed();
    if (host_ == normalized) return;
    host_ = normalized;
    emit configurationChanged();
}

void MmsReportController::setPort(const int value) {
    if (value < 1 || value > 65'535 || port_ == value) return;
    port_ = value;
    emit configurationChanged();
}

void MmsReportController::setEngineeringContext(IedEngineeringContextController* value) {
    if (engineeringContext_ == value) return;
    if (engineeringContext_) disconnect(engineeringContext_, nullptr, this, nullptr);
    engineeringContext_ = value;
    if (engineeringContext_) {
        connect(
            engineeringContext_,
            &IedEngineeringContextController::contextChanged,
            this,
            [this] {
                if (!connected() && !busy()) adoptEngineeringInventory();
            });
    }
    emit engineeringContextChanged();
    if (!connected() && !busy()) adoptEngineeringInventory();
}

bool MmsReportController::connected() const noexcept {
    return state_ == State::ready || state_ == State::enabling || state_ == State::active ||
        state_ == State::disabling || state_ == State::cleanup_required;
}

bool MmsReportController::busy() const noexcept {
    return operationBusy_ || state_ == State::connecting || state_ == State::discovering ||
        state_ == State::enabling || state_ == State::disabling;
}

QString MmsReportController::stateText() const {
    switch (state_) {
    case State::disconnected: return QStringLiteral("Disconnected");
    case State::connecting: return QStringLiteral("Connecting");
    case State::discovering: return QStringLiteral("Attaching reports");
    case State::ready: return QStringLiteral("Ready");
    case State::authoring: return QStringLiteral("Authoring DataSet");
    case State::enabling: return QStringLiteral("Enabling RCB");
    case State::active: return QStringLiteral("Report subscription active");
    case State::disabling: return QStringLiteral("Releasing RCB");
    case State::cleanup_required: return QStringLiteral("Cleanup required");
    case State::faulted: return QStringLiteral("Faulted");
    }
    return {};
}

void MmsReportController::appendDiagnostic(const QString& text) {
    if (text.isEmpty()) return;
    while (diagnostics_.size() >= 128) diagnostics_.removeFirst();
    diagnostics_.push_back(text);
    emit diagnosticsChanged();
}

QString MmsReportController::diagnosticsText() const {
    return diagnostics_.join(QLatin1Char('\n'));
}

std::shared_ptr<std::stop_source> MmsReportController::replaceStopSource() {
    if (stopSource_) stopSource_->request_stop();
    stopSource_ = std::make_shared<std::stop_source>();
    return stopSource_;
}

void MmsReportController::clearInventory() {
    dataSets_.clear();
    reportControls_.clear();
    staticCandidates_.clear();
    dynamicCandidates_.clear();
    ownedDynamicDataSets_.clear();
    associationProfile_.clear();
    selectedRcbIndex_ = -1;
    selectedDataSetIndex_ = -1;
    selectedRcb_.clear();
    selectedDataSetMembers_.clear();
    receivedReports_.clear();
    events_.clear();
    receivedReportCount_ = 0;
    emit inventoryChanged();
    emit selectionChanged();
    emit reportsChanged();
}

void MmsReportController::adoptEngineeringInventory() {
    if (!engineeringContext_ || !engineeringContext_->loaded() ||
        engineeringContext_->selectionRequired() ||
        engineeringContext_->modelSnapshot() == nullptr) {
        clearInventory();
        return;
    }

    const auto seed = buildContextDiscoverySeed(*engineeringContext_->modelSnapshot());
    const auto ui = buildDiscoveryUi(seed, QString{});
    dataSets_ = ui.dataSets;
    reportControls_ = ui.reportControls;
    staticCandidates_.clear();
    dynamicCandidates_.clear();
    associationProfile_.clear();
    selectedRcbIndex_ = reportControls_.isEmpty() ? -1 : 0;
    selectedDataSetIndex_ = dataSets_.isEmpty() ? -1 : 0;
    receivedReports_.clear();
    events_.clear();
    receivedReportCount_ = 0;
    refreshSelection();
    emit inventoryChanged();
    emit reportsChanged();
}

bool MmsReportController::connectToIed() {
    if (busy() || connected()) return false;
    if (host_.trimmed().isEmpty() || port_ < 1 || port_ > 65'535) {
        lastError_ = QStringLiteral("A non-empty host and TCP port 1..65535 are required.");
        state_ = State::faulted;
        emit stateChanged();
        return false;
    }

    const auto stop = replaceStopSource();
    const auto generation = ++generation_;
    const auto requestedHost = host_.trimmed();
    const auto requestedPort = port_;
    state_ = State::connecting;
    operationBusy_ = true;
    active_ = false;
    cleanupRequired_ = false;
    pollPending_ = false;
    lastError_.clear();
    clearInventory();
    if (engineeringContext_) adoptEngineeringInventory();
    std::shared_ptr<mms::MmsLiveModelDocument> contextModel;
    if (engineeringContext_ && engineeringContext_->loaded() &&
        !engineeringContext_->selectionRequired() &&
        engineeringContext_->modelSnapshot() != nullptr) {
        contextModel = std::make_shared<mms::MmsLiveModelDocument>(
            *engineeringContext_->modelSnapshot());
    }
    appendDiagnostic(QStringLiteral("Reports connect %1:%2 · generation %3")
        .arg(requestedHost).arg(requestedPort).arg(generation));
    emit stateChanged();

    const QPointer<MmsReportController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, stop, generation, requestedHost, requestedPort, contextModel] {
        try {
            if (worker->report) worker->report->stop();
            worker->report.reset();
            if (worker->session) worker->session->disconnect();
            worker->session.reset();
            worker->discovery.reset();

            mms::MmsAssociationOptions associationOptions;
            associationOptions.connect_timeout = std::chrono::milliseconds{5'000};
            associationOptions.request_timeout = std::chrono::milliseconds{5'000};
            auto session = std::make_unique<mms::MmsTcpLiveDiscoverySession>(
                mms::TcpMmsTransportOptions{}, associationOptions);
            session->connect(
                {requestedHost.toStdString(), static_cast<std::uint16_t>(requestedPort)},
                stop->get_token());

            if (self) {
                QMetaObject::invokeMethod(self, [self, generation] {
                    if (!self || self->generation_ != generation) return;
                    self->state_ = State::discovering;
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }

            std::shared_ptr<mms::MmsLiveDiscoveryResult> discovery;
            if (contextModel) {
                discovery = std::make_shared<mms::MmsLiveDiscoveryResult>(
                    buildContextDiscoverySeed(*contextModel));
                probeContextReportControls(
                    session->association(), *discovery, stop->get_token());
            } else {
                // Standalone Reports workspace compatibility path. Browser-owned
                // services always receive engineeringContext and therefore skip
                // this full discovery path.
                mms::MmsLiveDiscoveryOptions options;
                options.maximum_variable_type_probes = 8'192U;
                options.maximum_data_set_directories = 4'096U;
                options.maximum_report_control_probes = 1'024U;
                discovery = std::make_shared<mms::MmsLiveDiscoveryResult>(
                    session->discover(options, stop->get_token()));
            }
            const auto profile = QString::fromStdString(
                session->association().active_association_profile());
            const auto ui = buildDiscoveryUi(*discovery, profile);
            worker->discovery = discovery;
            worker->session = std::move(session);

            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, ui] {
                    if (!self || self->generation_ != generation) return;
                    self->dataSets_ = ui.dataSets;
                    self->reportControls_ = ui.reportControls;
                    self->staticCandidates_ = ui.staticCandidates;
                    self->dynamicCandidates_ = ui.dynamicCandidates;
                    self->associationProfile_ = ui.associationProfile;
                    self->state_ = State::ready;
                    self->operationBusy_ = false;
                    self->lastError_.clear();
                    if (!self->reportControls_.isEmpty()) self->selectedRcbIndex_ = 0;
                    else if (!self->dataSets_.isEmpty()) self->selectedDataSetIndex_ = 0;
                    self->refreshSelection();
                    self->appendDiagnostic(QStringLiteral("Report service attach complete · %1 DataSet · %2 RCB")
                        .arg(self->dataSets_.size()).arg(self->reportControls_.size()));
                    emit self->inventoryChanged();
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (worker->session) worker->session->disconnect();
            worker->report.reset();
            worker->session.reset();
            worker->discovery.reset();
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->state_ = State::faulted;
                    self->operationBusy_ = false;
                    self->lastError_ = message;
                    self->appendDiagnostic(QStringLiteral("Report service attach failed · %1").arg(message));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

void MmsReportController::disconnectFromIed() {
    if (stopSource_) stopSource_->request_stop();
    stopSource_ = std::make_shared<std::stop_source>();
    const auto cleanupStop = stopSource_;
    const auto generation = ++generation_;
    pollTimer_.stop();
    pollPending_ = false;
    operationBusy_ = false;
    active_ = false;
    cleanupRequired_ = false;
    state_ = State::disconnected;
    lastError_.clear();
    clearInventory();
    adoptEngineeringInventory();
    appendDiagnostic(QStringLiteral("Reports disconnected · runtime association released; canonical inventory retained."));
    emit stateChanged();

    const QPointer<MmsReportController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, cleanupStop, generation] {
        bool cleanupRequired = false;
        if (worker->report) {
            worker->report->stop(cleanupStop->get_token());
            const auto snapshot = worker->report->snapshot();
            cleanupRequired = snapshot.subscription && snapshot.subscription->cleanup_required;
        }
        if (worker->session) worker->session->disconnect(cleanupStop->get_token());
        worker->report.reset();
        worker->discovery.reset();
        worker->session.reset();
        if (cleanupRequired && self) {
            QMetaObject::invokeMethod(self, [self, generation] {
                if (!self || self->generation_ != generation) return;
                self->lastError_ = QStringLiteral("RCB cleanup could not be confirmed before disconnect; verify the remote RCB before reuse.");
                self->appendDiagnostic(self->lastError_);
                emit self->stateChanged();
            }, Qt::QueuedConnection);
        }
    });
}

bool MmsReportController::reconnect() {
    disconnectFromIed();
    return connectToIed();
}

void MmsReportController::refreshSelection() {
    selectedRcb_.clear();
    selectedDataSetMembers_.clear();
    if (selectedRcbIndex_ >= 0 && selectedRcbIndex_ < reportControls_.size()) {
        selectedRcb_ = reportControls_.at(selectedRcbIndex_).toMap();
        const auto bound = selectedRcb_.value(QStringLiteral("dataSet")).toString();
        if (!bound.isEmpty()) {
            for (int row = 0; row < dataSets_.size(); ++row) {
                const auto map = dataSets_.at(row).toMap();
                if (!sameDataSetReference(bound, map.value(QStringLiteral("reference")).toString())) continue;
                selectedDataSetIndex_ = row;
                selectedDataSetMembers_ = map.value(QStringLiteral("members")).toStringList();
                break;
            }
        }
    }
    if (selectedDataSetMembers_.isEmpty() && selectedDataSetIndex_ >= 0 && selectedDataSetIndex_ < dataSets_.size()) {
        selectedDataSetMembers_ = dataSets_.at(selectedDataSetIndex_).toMap().value(QStringLiteral("members")).toStringList();
    }
    emit selectionChanged();
}

bool MmsReportController::selectRcb(const int row) {
    if (row < 0 || row >= reportControls_.size()) return false;
    selectedRcbIndex_ = row;
    refreshSelection();
    return true;
}

bool MmsReportController::selectDataSet(const int row) {
    if (row < 0 || row >= dataSets_.size()) return false;
    selectedDataSetIndex_ = row;
    selectedDataSetMembers_ = dataSets_.at(row).toMap().value(QStringLiteral("members")).toStringList();
    emit selectionChanged();
    return true;
}

bool MmsReportController::enableSelected(const bool requestGeneralInterrogation) {
    if (!connected() || busy() || active_ || cleanupRequired_ || selectedRcb_.isEmpty()) return false;
    const auto reference = selectedRcb_.value(QStringLiteral("reference")).toString();
    if (reference.isEmpty()) return false;

    const auto generation = generation_;
    const auto stop = stopSource_;
    operationBusy_ = true;
    state_ = State::enabling;
    lastError_.clear();
    emit stateChanged();

    const QPointer<MmsReportController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, stop, generation, reference, requestGeneralInterrogation] {
        try {
            if (!worker->session || !worker->session->associated() || !worker->discovery) {
                throw std::runtime_error("MMS report association is not active.");
            }
            if (worker->report) {
                const auto previous = worker->report->snapshot();
                if (previous.active || (previous.subscription && previous.subscription->cleanup_required)) {
                    throw std::runtime_error("Previous report subscription still owns state or requires cleanup.");
                }
                worker->report.reset();
            }

            mms::MmsStaticReportSessionOptions options;
            options.selection.preferred_rcb_reference = reference.toStdString();
            options.selection.strict_rcb = true;
            // Strict selection still pins the exact operator-selected RCB. This
            // flag permits an explicitly selected URCB; it does not authorize
            // fallback to another RCB because strict_rcb remains true.
            options.selection.allow_urcb_fallback = true;
            options.selection.allow_polling_fallback = false;
            options.maximum_candidate_attempts = 1U;
            options.subscription.trigger_general_interrogation = requestGeneralInterrogation;
            options.subscription.reserve_unbuffered_rcb = true;
            options.subscription.maximum_events = 256U;
            options.subscription.monitor_options.maximum_streams = 64U;
            options.subscription.monitor_options.maximum_frames_per_stream = 64U;

            auto report = std::make_unique<mms::MmsStaticReportSessionRuntime>(
                worker->session->association(), *worker->discovery, options);
            report->prepare(stop->get_token());
            report->start(stop->get_token());
            const auto ui = buildReportUi(report->snapshot());
            worker->report = std::move(report);

            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, ui, requestGeneralInterrogation] {
                    if (!self || self->generation_ != generation) return;
                    self->operationBusy_ = false;
                    self->active_ = ui.active;
                    self->cleanupRequired_ = ui.cleanupRequired;
                    self->receivedReports_ = ui.reports;
                    self->events_ = ui.events;
                    self->receivedReportCount_ = ui.receivedCount;
                    self->state_ = ui.cleanupRequired ? State::cleanup_required : State::active;
                    self->appendDiagnostic(requestGeneralInterrogation
                        ? QStringLiteral("RCB enabled; GI requested explicitly.")
                        : QStringLiteral("RCB enabled without GI."));
                    emit self->reportsChanged();
                    emit self->stateChanged();
                    if (self->active_) self->pollTimer_.start();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->operationBusy_ = false;
                    self->active_ = false;
                    self->lastError_ = message;
                    self->state_ = State::faulted;
                    self->appendDiagnostic(QStringLiteral("RCB enable failed · %1").arg(message));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

bool MmsReportController::disableSelected() {
    if (!active_ || operationBusy_) return false;
    pollTimer_.stop();
    const auto stop = replaceStopSource();
    const auto generation = generation_;
    operationBusy_ = true;
    state_ = State::disabling;
    emit stateChanged();

    const QPointer<MmsReportController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, stop, generation] {
        try {
            if (!worker->report) throw std::runtime_error("No report subscription is active.");
            worker->report->stop(stop->get_token());
            const auto snapshot = worker->report->snapshot();
            const auto ui = buildReportUi(snapshot);
            if (!ui.cleanupRequired) worker->report.reset();
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, ui] {
                    if (!self || self->generation_ != generation) return;
                    self->pollPending_ = false;
                    self->operationBusy_ = false;
                    self->active_ = false;
                    self->cleanupRequired_ = ui.cleanupRequired;
                    self->receivedReports_ = ui.reports;
                    self->events_ = ui.events;
                    self->receivedReportCount_ = ui.receivedCount;
                    self->state_ = ui.cleanupRequired ? State::cleanup_required : State::ready;
                    self->appendDiagnostic(ui.cleanupRequired
                        ? QStringLiteral("RCB release incomplete; explicit cleanup retry required.")
                        : QStringLiteral("RCB disabled and reservation/ownership cleanup completed."));
                    emit self->reportsChanged();
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->pollPending_ = false;
                    self->operationBusy_ = false;
                    self->active_ = false;
                    self->cleanupRequired_ = true;
                    self->state_ = State::cleanup_required;
                    self->lastError_ = message;
                    self->appendDiagnostic(QStringLiteral("RCB release failed · %1").arg(message));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

bool MmsReportController::retryCleanup() {
    if (!cleanupRequired_ || operationBusy_) return false;
    const auto stop = replaceStopSource();
    const auto generation = generation_;
    operationBusy_ = true;
    state_ = State::disabling;
    emit stateChanged();

    const QPointer<MmsReportController> self{this};
    const auto worker = workerState_;
    ioPool_.start([self, worker, stop, generation] {
        try {
            if (!worker->session || !worker->session->associated() || !worker->report) {
                throw std::runtime_error("Cleanup retry requires the original active MMS association.");
            }
            worker->report->stop(stop->get_token());
            const auto ui = buildReportUi(worker->report->snapshot());
            if (!ui.cleanupRequired) worker->report.reset();
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, ui] {
                    if (!self || self->generation_ != generation) return;
                    self->operationBusy_ = false;
                    self->active_ = false;
                    self->cleanupRequired_ = ui.cleanupRequired;
                    self->receivedReports_ = ui.reports;
                    self->events_ = ui.events;
                    self->receivedReportCount_ = ui.receivedCount;
                    self->state_ = ui.cleanupRequired ? State::cleanup_required : State::ready;
                    self->lastError_.clear();
                    self->appendDiagnostic(ui.cleanupRequired
                        ? QStringLiteral("RCB cleanup retry remains incomplete.")
                        : QStringLiteral("RCB cleanup retry completed."));
                    emit self->reportsChanged();
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message] {
                    if (!self || self->generation_ != generation) return;
                    self->operationBusy_ = false;
                    self->lastError_ = message;
                    self->state_ = State::cleanup_required;
                    self->appendDiagnostic(QStringLiteral("Cleanup retry rejected · %1").arg(message));
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
    return true;
}

void MmsReportController::schedulePoll() {
    if (!active_ || operationBusy_ || pollPending_) return;
    const auto generation = generation_;
    const auto stop = stopSource_;
    const auto worker = workerState_;
    pollPending_ = true;
    const QPointer<MmsReportController> self{this};
    ioPool_.start([self, worker, stop, generation] {
        try {
            if (!worker->session || !worker->session->associated() || !worker->report || !worker->report->active()) {
                throw std::runtime_error("Report poll requires an active MMS association and RCB subscription.");
            }
            mms::MmsPduEnvelope envelope;
            static_cast<void>(worker->session->association().try_poll_once_for(
                std::chrono::milliseconds{40}, envelope, stop->get_token()));
            static_cast<void>(worker->report->drain_queued_reports());
            const auto ui = buildReportUi(worker->report->snapshot());
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation, ui] {
                    if (!self || self->generation_ != generation) return;
                    self->pollPending_ = false;
                    self->active_ = ui.active;
                    self->cleanupRequired_ = ui.cleanupRequired;
                    self->receivedReports_ = ui.reports;
                    self->events_ = ui.events;
                    self->receivedReportCount_ = ui.receivedCount;
                    emit self->reportsChanged();
                }, Qt::QueuedConnection);
            }
        } catch (const mms::MmsTransportCancelledError&) {
            if (self) {
                QMetaObject::invokeMethod(self, [self, generation] {
                    if (!self || self->generation_ != generation) return;
                    self->pollPending_ = false;
                }, Qt::QueuedConnection);
            }
        } catch (const std::exception& exception) {
            ReportUi ui;
            if (worker->report) {
                worker->report->stop();
                ui = buildReportUi(worker->report->snapshot());
            }
            if (self) {
                const auto message = QString::fromUtf8(exception.what());
                QMetaObject::invokeMethod(self, [self, generation, message, ui] {
                    if (!self || self->generation_ != generation) return;
                    self->pollTimer_.stop();
                    self->pollPending_ = false;
                    self->active_ = false;
                    self->cleanupRequired_ = ui.cleanupRequired;
                    self->receivedReports_ = ui.reports;
                    self->events_ = ui.events;
                    self->receivedReportCount_ = ui.receivedCount;
                    self->lastError_ = message;
                    self->state_ = ui.cleanupRequired ? State::cleanup_required : State::faulted;
                    self->appendDiagnostic(QStringLiteral("Report association/poll failed · %1").arg(message));
                    emit self->reportsChanged();
                    emit self->stateChanged();
                }, Qt::QueuedConnection);
            }
        }
    });
}
