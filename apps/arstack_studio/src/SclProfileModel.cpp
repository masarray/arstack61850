// SPDX-License-Identifier: GPL-3.0-or-later

#include "SclProfileModel.hpp"

#include "ariec61850/sampled_values/esp32p4_profile_support.hpp"
#include "ariec61850/scl/parser.hpp"

#include <QStringList>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>

namespace {
using ar::iec61850::sampled_values::Esp32P4SvProfileSupport;
using ar::iec61850::sampled_values::SvPublisherProfile;
using ar::iec61850::sampled_values::SvPublisherProfileCompileContext;
using ar::iec61850::sampled_values::SvPublisherProfileCompiler;
using ar::iec61850::sampled_values::SvSampleCounterPolicy;
using ar::iec61850::sampled_values::SvSampleMode;
using ar::iec61850::sampled_values::classify_esp32p4_sv_profile;
using ar::iec61850::sampled_values::esp32p4_sv_profile_support_name;

QString qstring(const std::string& value) {
    return QString::fromStdString(value);
}

QStringList qstrings(const std::vector<std::string>& values) {
    QStringList out;
    out.reserve(static_cast<qsizetype>(values.size()));
    for (const auto& value : values) out.push_back(qstring(value));
    return out;
}

QString macText(const std::array<std::uint8_t, 6>& mac) {
    QStringList bytes;
    bytes.reserve(6);
    for (const auto byte : mac) {
        bytes.push_back(QStringLiteral("%1")
            .arg(static_cast<unsigned>(byte), 2, 16, QLatin1Char('0'))
            .toUpper());
    }
    return bytes.join(QLatin1Char(':'));
}

QString sampleModeText(const SvSampleMode mode) {
    switch (mode) {
    case SvSampleMode::samples_per_second: return QStringLiteral("SmpPerSec");
    case SvSampleMode::samples_per_period: return QStringLiteral("SmpPerPeriod");
    default: return QStringLiteral("Unknown");
    }
}

QString counterPolicyText(const SvSampleCounterPolicy policy) {
    switch (policy) {
    case SvSampleCounterPolicy::explicit_modulus: return QStringLiteral("explicit");
    case SvSampleCounterPolicy::candidate_sample_rate_modulus: return QStringLiteral("candidate-rate");
    default: return QStringLiteral("unresolved");
    }
}

QString supportText(const Esp32P4SvProfileSupport support) {
    return QString::fromLatin1(
        esp32p4_sv_profile_support_name(support).data(),
        static_cast<qsizetype>(esp32p4_sv_profile_support_name(support).size()));
}
} // namespace

SclProfileModel::SclProfileModel(QObject* parent) : QAbstractListModel(parent) {}

int SclProfileModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(rows_.size());
}

QVariant SclProfileModel::data(const QModelIndex& index, const int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& row = rows_[static_cast<std::size_t>(index.row())];

    switch (role) {
    case IedRole: return row.ied;
    case ControlRole: return row.control;
    case ControlBlockReferenceRole: return row.controlBlockReference;
    case CompatibilityClassRole: return row.compatibilityClass;
    case DeviceSupportRole: return row.deviceSupport;
    case WarningsRole: return row.warnings;
    case ErrorsRole: return row.errors;
    default: break;
    }

    if (!row.profile.has_value()) return {};
    const auto& p = *row.profile;
    switch (role) {
    case SvIdRole: return qstring(p.sv_id);
    case DestinationMacRole: return macText(p.destination_mac);
    case AppIdRole: return static_cast<int>(p.app_id);
    case PublisherRateRole:
        return p.publisher_rate_hz.has_value()
            ? QVariant::fromValue(static_cast<qulonglong>(*p.publisher_rate_hz))
            : QVariant{};
    case PayloadBytesRole: return static_cast<qulonglong>(p.payload_size_bytes);
    case CounterPolicyRole: return counterPolicyText(p.sample_counter_policy);
    case CounterModulusRole:
        return p.sample_counter_modulus.has_value()
            ? QVariant::fromValue(static_cast<int>(*p.sample_counter_modulus))
            : QVariant{};
    default: return {};
    }
}

QHash<int, QByteArray> SclProfileModel::roleNames() const {
    return {
        {IedRole, "ied"},
        {ControlRole, "control"},
        {ControlBlockReferenceRole, "controlBlockReference"},
        {SvIdRole, "svId"},
        {CompatibilityClassRole, "compatibilityClass"},
        {DeviceSupportRole, "deviceSupport"},
        {DestinationMacRole, "destinationMac"},
        {AppIdRole, "appId"},
        {PublisherRateRole, "publisherRate"},
        {PayloadBytesRole, "payloadBytes"},
        {CounterPolicyRole, "counterPolicy"},
        {CounterModulusRole, "counterModulus"},
        {WarningsRole, "warnings"},
        {ErrorsRole, "errors"},
    };
}

QString SclProfileModel::sourceName() const {
    return document_.has_value() ? qstring(document_->source_name) : QString{};
}

QString SclProfileModel::documentStatus() const {
    if (!document_.has_value()) return QStringLiteral("No engineering file loaded");
    return QStringLiteral("%1 resolved SV stream%2")
        .arg(static_cast<qulonglong>(rows_.size()))
        .arg(rows_.size() == 1U ? QString{} : QStringLiteral("s"));
}

QStringList SclProfileModel::documentWarnings() const {
    if (!document_.has_value()) return {};
    QStringList out = qstrings(document_->warnings);
    for (const auto& conflict : document_->conflicts) {
        out.push_back(QStringLiteral("Conflict: %1").arg(qstring(conflict.description)));
    }
    return out;
}

QString SclProfileModel::fatalError() const { return fatalError_; }
int SclProfileModel::selectedIndex() const noexcept { return selectedIndex_; }
bool SclProfileModel::hasProfiles() const noexcept { return !rows_.empty(); }

QVariantMap SclProfileModel::selectedProfile() const {
    if (selectedIndex_ < 0 || selectedIndex_ >= rowCount()) return {};
    const auto& row = rows_[static_cast<std::size_t>(selectedIndex_)];
    QVariantMap out;
    out.insert(QStringLiteral("compatibilityClass"), row.compatibilityClass);
    out.insert(QStringLiteral("deviceSupport"), row.deviceSupport);
    out.insert(QStringLiteral("warnings"), row.warnings);
    out.insert(QStringLiteral("errors"), row.errors);
    if (row.profile.has_value()) {
        const auto profileMap = profileToVariantMap(*row.profile);
        for (auto it = profileMap.cbegin(); it != profileMap.cend(); ++it) {
            out.insert(it.key(), it.value());
        }
    }
    return out;
}

bool SclProfileModel::loadFile(const QUrl& fileUrl) {
    const auto localPath = fileUrl.toLocalFile();
    if (localPath.isEmpty()) {
        fatalError_ = QStringLiteral("Choose a local SCL/CID/SCD/IID file.");
        emit fatalErrorChanged();
        return false;
    }

    try {
        auto loaded = ar::iec61850::scl::SclParser{}.load(
            std::filesystem::path{localPath.toStdWString()});
        beginResetModel();
        document_ = std::move(loaded);
        confirmedCounterModulus_.reset();
        rows_.clear();
        selectedIndex_ = -1;
        fatalError_.clear();
        endResetModel();
        rebuildRows();
        emit fatalErrorChanged();
        emit sourceChanged();
        return true;
    } catch (const std::exception& error) {
        clear();
        fatalError_ = QString::fromUtf8(error.what());
        emit fatalErrorChanged();
        return false;
    }
}

void SclProfileModel::clear() {
    beginResetModel();
    document_.reset();
    confirmedCounterModulus_.reset();
    rows_.clear();
    selectedIndex_ = -1;
    fatalError_.clear();
    endResetModel();
    emit selectedIndexChanged();
    emit selectedProfileChanged();
    emit fatalErrorChanged();
    emit sourceChanged();
}

void SclProfileModel::selectStream(const int row) {
    const int normalized = row >= 0 && row < rowCount() ? row : -1;
    if (selectedIndex_ == normalized) return;
    selectedIndex_ = normalized;
    emit selectedIndexChanged();
    emit selectedProfileChanged();
}

bool SclProfileModel::confirmCounterModulus(const int modulus) {
    if (!document_.has_value() || modulus <= 0 || modulus > 65535) return false;
    confirmedCounterModulus_ = static_cast<std::uint16_t>(modulus);
    rebuildRows();
    return true;
}

void SclProfileModel::clearCounterConfirmation() {
    if (!confirmedCounterModulus_.has_value()) return;
    confirmedCounterModulus_.reset();
    rebuildRows();
}

void SclProfileModel::rebuildRows() {
    if (!document_.has_value()) return;

    const int previousSelection = selectedIndex_;
    beginResetModel();
    rows_.clear();
    rows_.reserve(document_->sampled_values_streams.size());

    for (const auto& stream : document_->sampled_values_streams) {
        SvPublisherProfileCompileContext context;
        context.sample_counter_modulus = confirmedCounterModulus_;
        auto compiled = SvPublisherProfileCompiler::compile(stream, context);

        Row row;
        row.ied = qstring(stream.ied_name);
        row.control = qstring(stream.control_name);
        row.controlBlockReference = qstring(stream.control_block_reference);
        row.errors = qstrings(compiled.errors);
        row.warnings = qstrings(compiled.warnings);

        if (!compiled.ok()) {
            row.compatibilityClass = QStringLiteral("C");
            row.deviceSupport = QStringLiteral("blocked");
        } else {
            row.profile = std::move(compiled.profile);
            const auto& profile = *row.profile;
            row.compatibilityClass =
                profile.sample_counter_policy == SvSampleCounterPolicy::explicit_modulus
                    ? QStringLiteral("A")
                    : QStringLiteral("B");
            row.deviceSupport = supportText(classify_esp32p4_sv_profile(profile));
        }
        rows_.push_back(std::move(row));
    }

    selectedIndex_ = rows_.empty()
        ? -1
        : std::clamp(previousSelection, 0, static_cast<int>(rows_.size()) - 1);
    if (selectedIndex_ < 0 && !rows_.empty()) selectedIndex_ = 0;
    endResetModel();

    emit selectedIndexChanged();
    emit selectedProfileChanged();
    emit sourceChanged();
}

QVariantMap SclProfileModel::profileToVariantMap(const SvPublisherProfile& p) const {
    QVariantMap map;
    map.insert(QStringLiteral("svId"), qstring(p.sv_id));
    map.insert(QStringLiteral("controlBlockReference"), qstring(p.control_block_reference));
    map.insert(QStringLiteral("dataSetReference"), qstring(p.data_set_reference));
    map.insert(QStringLiteral("destinationMac"), macText(p.destination_mac));
    map.insert(QStringLiteral("appId"), static_cast<int>(p.app_id));
    map.insert(QStringLiteral("appIdHex"),
        QStringLiteral("0x%1").arg(p.app_id, 4, 16, QLatin1Char('0')).toUpper());
    map.insert(QStringLiteral("vlanPresent"), p.vlan_present);
    map.insert(QStringLiteral("vlanId"), static_cast<int>(p.vlan_id));
    map.insert(QStringLiteral("vlanPriority"), static_cast<int>(p.vlan_priority));
    map.insert(QStringLiteral("confRev"), static_cast<qulonglong>(p.configuration_revision));
    map.insert(QStringLiteral("sampleRate"), static_cast<qulonglong>(p.sample_rate_value));
    map.insert(QStringLiteral("sampleMode"), sampleModeText(p.sample_mode));
    map.insert(QStringLiteral("publisherRate"),
        p.publisher_rate_hz.has_value()
            ? QVariant::fromValue(static_cast<qulonglong>(*p.publisher_rate_hz))
            : QVariant{});
    map.insert(QStringLiteral("counterPolicy"), counterPolicyText(p.sample_counter_policy));
    map.insert(QStringLiteral("counterModulus"),
        p.sample_counter_modulus.has_value()
            ? QVariant::fromValue(static_cast<int>(*p.sample_counter_modulus))
            : QVariant{});
    map.insert(QStringLiteral("payloadBytes"), static_cast<qulonglong>(p.payload_size_bytes));
    map.insert(QStringLiteral("nofASDU"), static_cast<int>(p.no_asdu));
    map.insert(QStringLiteral("channelLeafCount"), static_cast<qulonglong>(p.channels.size()));
    map.insert(QStringLiteral("includeDataSet"), p.asdu_options.data_set);
    map.insert(QStringLiteral("includeSampleRate"), p.asdu_options.sample_rate);
    map.insert(QStringLiteral("sampleSynchronizedField"), p.asdu_options.sample_synchronized);
    return map;
}
