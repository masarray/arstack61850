// SPDX-License-Identifier: GPL-3.0-or-later
#include "SclWorkspaceController.hpp"

#include "ariec61850/scl/exporter.hpp"
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

std::optional<scl::SclEdition> requestedEdition(
    const QString& text,
    const scl::SclEdition sourceEdition) {
    const auto key = text.trimmed().toLower();
    if (key.isEmpty() || key == QStringLiteral("preserve")) return sourceEdition;
    if (key == QStringLiteral("ed1")) return scl::SclEdition::edition1;
    if (key == QStringLiteral("ed2")) return scl::SclEdition::edition2;
    if (key == QStringLiteral("ed2.1") || key == QStringLiteral("ed21")) {
        return scl::SclEdition::edition21;
    }
    return std::nullopt;
}

std::optional<scl::SclExportProfile> profileFromPath(const QString& path) {
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QStringLiteral("scd")) return scl::SclExportProfile::scd;
    if (suffix == QStringLiteral("icd")) return scl::SclExportProfile::icd;
    if (suffix == QStringLiteral("cid")) return scl::SclExportProfile::cid;
    return std::nullopt;
}

QString profileLabel(const scl::SclExportProfile profile) {
    switch (profile) {
    case scl::SclExportProfile::scd: return QStringLiteral("SCD");
    case scl::SclExportProfile::icd: return QStringLiteral("ICD");
    case scl::SclExportProfile::cid: return QStringLiteral("CID");
    }
    return QStringLiteral("SCL");
}

bool entriesEquivalent(
    const std::vector<scl::SclDataSetEntry>& left,
    const std::vector<scl::SclDataSetEntry>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.index != rhs.index || lhs.signal_reference != rhs.signal_reference ||
            lhs.ied_name != rhs.ied_name || lhs.ld_inst != rhs.ld_inst ||
            lhs.prefix != rhs.prefix || lhs.ln_class != rhs.ln_class ||
            lhs.ln_inst != rhs.ln_inst || lhs.do_name != rhs.do_name ||
            lhs.da_name != rhs.da_name || lhs.functional_constraint != rhs.functional_constraint ||
            lhs.cdc != rhs.cdc || lhs.basic_type != rhs.basic_type ||
            lhs.enum_type != rhs.enum_type || lhs.is_quality != rhs.is_quality ||
            lhs.is_timestamp != rhs.is_timestamp || lhs.configured_value != rhs.configured_value) {
            return false;
        }
    }
    return true;
}

bool dataSetsEquivalent(
    const std::vector<scl::SclDataSet>& left,
    const std::vector<scl::SclDataSet>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.key != rhs.key || lhs.ied_name != rhs.ied_name || lhs.ld_inst != rhs.ld_inst ||
            lhs.logical_node_path != rhs.logical_node_path || lhs.name != rhs.name ||
            lhs.reference != rhs.reference || !entriesEquivalent(lhs.entries, rhs.entries) ||
            !entriesEquivalent(lhs.expanded_entries, rhs.expanded_entries)) {
            return false;
        }
    }
    return true;
}

bool reportsEquivalent(
    const std::vector<scl::SclReportControl>& left,
    const std::vector<scl::SclReportControl>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.ied_name != rhs.ied_name || lhs.ld_inst != rhs.ld_inst ||
            lhs.logical_node_path != rhs.logical_node_path || lhs.name != rhs.name ||
            lhs.report_id != rhs.report_id || lhs.data_set_name != rhs.data_set_name ||
            lhs.data_set_reference != rhs.data_set_reference ||
            lhs.data_set_binding_status != rhs.data_set_binding_status ||
            lhs.control_block_reference != rhs.control_block_reference || lhs.buffered != rhs.buffered ||
            lhs.indexed != rhs.indexed || lhs.configuration_revision != rhs.configuration_revision ||
            lhs.buffer_time_milliseconds != rhs.buffer_time_milliseconds ||
            lhs.integrity_period_milliseconds != rhs.integrity_period_milliseconds ||
            !entriesEquivalent(lhs.entries, rhs.entries)) {
            return false;
        }
    }
    return true;
}

bool gooseEquivalent(
    const std::vector<scl::SclGooseStream>& left,
    const std::vector<scl::SclGooseStream>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.kind != rhs.kind || lhs.ied_name != rhs.ied_name || lhs.ld_inst != rhs.ld_inst ||
            lhs.control_name != rhs.control_name ||
            lhs.control_block_reference != rhs.control_block_reference ||
            lhs.data_set_name != rhs.data_set_name || lhs.data_set_reference != rhs.data_set_reference ||
            lhs.configuration_revision != rhs.configuration_revision || !(lhs.address == rhs.address) ||
            !entriesEquivalent(lhs.entries, rhs.entries) || lhs.go_id != rhs.go_id ||
            lhs.min_time_milliseconds != rhs.min_time_milliseconds ||
            lhs.max_time_milliseconds != rhs.max_time_milliseconds) {
            return false;
        }
    }
    return true;
}

bool sampledValuesEquivalent(
    const std::vector<scl::SclSampledValuesStream>& left,
    const std::vector<scl::SclSampledValuesStream>& right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto& lhs = left[index];
        const auto& rhs = right[index];
        if (lhs.kind != rhs.kind || lhs.ied_name != rhs.ied_name || lhs.ld_inst != rhs.ld_inst ||
            lhs.control_name != rhs.control_name ||
            lhs.control_block_reference != rhs.control_block_reference ||
            lhs.data_set_name != rhs.data_set_name || lhs.data_set_reference != rhs.data_set_reference ||
            lhs.configuration_revision != rhs.configuration_revision || !(lhs.address == rhs.address) ||
            !entriesEquivalent(lhs.entries, rhs.entries) || lhs.sv_id != rhs.sv_id ||
            lhs.smv_id != rhs.smv_id || lhs.sample_rate != rhs.sample_rate ||
            lhs.sample_mode != rhs.sample_mode || lhs.no_asdu != rhs.no_asdu ||
            !(lhs.smv_options == rhs.smv_options)) {
            return false;
        }
    }
    return true;
}

bool modeledSemanticsEquivalent(const scl::SclDocument& left, const scl::SclDocument& right) {
    // Namespace/edition/source name intentionally change during a normalized export.
    // Type-template identifier strings are also implementation IDs; the resolved
    // reference/FC/CDC/basic-type projection is the semantic contract.
    return left.header_id == right.header_id && left.header_version == right.header_version &&
           left.header_revision == right.header_revision && left.ieds == right.ieds &&
           left.logical_nodes == right.logical_nodes &&
           entriesEquivalent(left.model_entries, right.model_entries) &&
           dataSetsEquivalent(left.data_sets, right.data_sets) &&
           reportsEquivalent(left.report_controls, right.report_controls) &&
           gooseEquivalent(left.goose_streams, right.goose_streams) &&
           sampledValuesEquivalent(left.sampled_values_streams, right.sampled_values_streams);
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
