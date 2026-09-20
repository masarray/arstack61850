// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/scl/model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ar::iec61850::scl {

enum class SclExportProfile {
    scd,
    iid,
    icd,
    cid,
};

struct SclCanonicalExportOptions final {
    SclEdition edition{SclEdition::unknown};
    SclExportProfile profile{SclExportProfile::scd};
};

struct SclCanonicalExportResult final {
    bool success{};
    std::string xml;
    std::string error;
    std::vector<std::string> warnings;
};

namespace exporter_detail {

inline std::string xml_escape(const std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '&': escaped += "&amp;"; break;
        case '<': escaped += "&lt;"; break;
        case '>': escaped += "&gt;"; break;
        case '\"': escaped += "&quot;"; break;
        case '\'': escaped += "&apos;"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

inline std::string safe_id(const std::string_view value) {
    std::string result;
    result.reserve(value.size() + 4U);
    for (const char ch : value) {
        const auto byte = static_cast<unsigned char>(ch);
        if (std::isalnum(byte) != 0 || ch == '_') {
            result.push_back(ch);
        } else {
            result.push_back('_');
        }
    }
    if (result.empty()) result = "X";
    const auto first = static_cast<unsigned char>(result.front());
    if (std::isdigit(first) != 0) result.insert(result.begin(), 'X');
    return result;
}

inline std::vector<std::string> split_path(const std::string_view value) {
    std::vector<std::string> result;
    std::size_t offset{};
    while (offset <= value.size()) {
        const auto end = value.find('.', offset);
        const auto count = end == std::string_view::npos ? value.size() - offset : end - offset;
        const auto token = value.substr(offset, count);
        if (!token.empty()) result.emplace_back(token);
        if (end == std::string_view::npos) break;
        offset = end + 1U;
    }
    return result;
}

inline bool same_text(const std::string_view left, const std::string_view right) {
    if (left.size() != right.size()) return false;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        if (std::tolower(lhs) != std::tolower(rhs)) return false;
    }
    return true;
}

inline std::string bool_text(const bool value) {
    return value ? "true" : "false";
}

inline std::string edition_attributes(const SclEdition edition) {
    switch (edition) {
    case SclEdition::edition1:
        return " xmlns=\"http://www.iec.ch/61850/2003/SCL\" version=\"2003\" revision=\"A\"";
    case SclEdition::edition2:
        return " xmlns=\"http://www.iec.ch/61850/2003/SCL\" version=\"2007\" revision=\"B\"";
    case SclEdition::edition21:
        return " xmlns=\"http://www.iec.ch/61850/2003/SCL\" version=\"2007\" revision=\"B\" release=\"4\"";
    case SclEdition::unknown:
        break;
    }
    return {};
}

inline std::string profile_name(const SclExportProfile profile) {
    switch (profile) {
    case SclExportProfile::scd: return "SCD";
    case SclExportProfile::iid: return "IID";
    case SclExportProfile::icd: return "ICD";
    case SclExportProfile::cid: return "CID";
    }
    return "SCL";
}

inline std::string ln_key(const SclLogicalNode& ln) {
    return ln.ied_name + "|" + ln.ld_inst + "|" + ln.prefix + "|" + ln.ln_class + "|" + ln.ln_inst;
}

inline std::string ln_type_id(const SclLogicalNode& ln) {
    return "AR_LNT_" + safe_id(ln.ied_name + "_" + ln.ld_inst + "_" + ln.name);
}

inline bool entry_matches_ln(const SclDataSetEntry& entry, const SclLogicalNode& ln) {
    return entry.ied_name == ln.ied_name && entry.ld_inst == ln.ld_inst &&
           entry.prefix == ln.prefix && entry.ln_class == ln.ln_class &&
           entry.ln_inst == ln.ln_inst;
}

struct DaNode final {
    std::string name;
    std::string functional_constraint;
    std::string basic_type;
    std::string type_id;
    std::string enum_type;
    bool leaf{};
    std::vector<DaNode> children;
};

struct DoNode final {
    std::string name;
    std::string cdc;
    std::vector<DoNode> children;
    std::vector<DaNode> attributes;
};

struct LnTypeModel final {
    const SclLogicalNode* logical_node{};
    std::vector<DoNode> data_objects;
};

template <typename Node>
inline Node& find_or_add(std::vector<Node>& nodes, const std::string& name) {
    const auto found = std::find_if(nodes.begin(), nodes.end(), [&name](const Node& node) {
        return node.name == name;
    });
    if (found != nodes.end()) return *found;
    nodes.push_back(Node{});
    nodes.back().name = name;
    return nodes.back();
}

inline std::string do_type_id(
    const SclLogicalNode& ln,
    const std::string_view path) {
    return "AR_DOT_" + safe_id(ln.ied_name + "_" + ln.ld_inst + "_" + ln.name + "_" + std::string{path});
}

inline std::string da_type_id(
    const SclLogicalNode& ln,
    const std::string_view do_path,
    const std::string_view da_path) {
    return "AR_DAT_" + safe_id(
        ln.ied_name + "_" + ln.ld_inst + "_" + ln.name + "_" + std::string{do_path} + "_" + std::string{da_path});
}

inline void add_model_entry(
    LnTypeModel& model,
    const SclDataSetEntry& entry) {
    const auto do_segments = split_path(entry.do_name);
    const auto da_segments = split_path(entry.da_name);
    if (do_segments.empty() || da_segments.empty()) return;

    DoNode* current_do{};
    for (const auto& segment : do_segments) {
        if (current_do == nullptr) {
            auto& node = find_or_add(model.data_objects, segment);
            if (node.cdc.empty()) node.cdc = entry.cdc;
            current_do = &node;
        } else {
            auto& node = find_or_add(current_do->children, segment);
            if (node.cdc.empty()) node.cdc = entry.cdc;
            current_do = &node;
        }
    }
    if (current_do == nullptr) return;

    DaNode* current_da{};
    for (std::size_t index = 0U; index < da_segments.size(); ++index) {
        const auto& segment = da_segments[index];
        if (current_da == nullptr) {
            auto& node = find_or_add(current_do->attributes, segment);
            if (node.functional_constraint.empty()) node.functional_constraint = entry.functional_constraint;
            current_da = &node;
        } else {
            auto& node = find_or_add(current_da->children, segment);
            current_da = &node;
        }
        if (index + 1U == da_segments.size()) {
            current_da->leaf = true;
            current_da->basic_type = entry.basic_type;
            current_da->type_id = entry.type_id;
            current_da->enum_type = entry.enum_type;
        }
    }
}

inline std::vector<LnTypeModel> build_ln_models(const SclDocument& document) {
    std::vector<LnTypeModel> models;
    models.reserve(document.logical_nodes.size());
    for (const auto& logical_node : document.logical_nodes) {
        LnTypeModel model;
        model.logical_node = &logical_node;
