// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/scl/dataset_reference.hpp"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <vector>

namespace ar::iec61850::scl {
namespace {

std::string trim_copy(std::string_view text) {
    auto first = text.begin();
    while (first != text.end() && std::isspace(static_cast<unsigned char>(*first)) != 0) {
        ++first;
    }
    auto last = text.end();
    while (last != first && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
        --last;
    }
    return {first, last};
}

std::string lower_copy(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

bool same(std::string_view left, std::string_view right) {
    return lower_copy(trim_copy(left)) == lower_copy(trim_copy(right));
}

std::vector<std::string> split_path(const std::string& text, const char delimiter) {
    std::vector<std::string> result;
    std::size_t offset{};
    while (offset <= text.size()) {
        const auto end = text.find(delimiter, offset);
        const auto part = trim_copy(std::string_view{text}.substr(
            offset,
            end == std::string::npos ? std::string::npos : end - offset));
        if (!part.empty()) {
            result.push_back(part);
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1U;
    }
    return result;
}

struct ParsedReference final {
    std::string ld_inst;
    std::string logical_node_path;
    std::string local_name;
};

std::string resolve_ld_inst(
    const std::vector<std::string>& path,
    const std::string& ied_name,
    const std::string& fallback_ld_inst) {
    if (path.size() < 2U) {
        return fallback_ld_inst;
    }

    auto domain = trim_copy(path[path.size() - 2U]);
    if (same(domain, ied_name) && path.size() >= 3U) {
        domain = trim_copy(path[path.size() - 3U]);
    }

    const auto lower_domain = lower_copy(domain);
    const auto lower_ied = lower_copy(ied_name);
    if (!ied_name.empty() && lower_domain.starts_with(lower_ied) && domain.size() > ied_name.size()) {
        domain = domain.substr(ied_name.size());
    }

    return domain.empty() ? fallback_ld_inst : domain;
}

ParsedReference parse_reference(
    const std::string& raw_reference,
    const std::string& ied_name,
    const std::string& fallback_ld_inst,
    const std::string& fallback_logical_node_path) {
    auto normalized = trim_copy(raw_reference);
    std::replace(normalized.begin(), normalized.end(), '\\', '/');

    auto lower = lower_copy(normalized);
    std::size_t position{};
    while ((position = lower.find("$ds$", position)) != std::string::npos) {
        normalized.replace(position, 4U, "$");
        lower.replace(position, 4U, "$");
        ++position;
    }

    const auto path = split_path(normalized, '/');
    auto leaf = path.empty() ? normalized : path.back();
    auto ld_inst = resolve_ld_inst(path, ied_name, fallback_ld_inst);
    auto logical_node_path = fallback_logical_node_path;
    auto local_name = leaf;

    const auto dollar_parts = split_path(leaf, '$');
    if (dollar_parts.size() >= 2U) {
        logical_node_path = dollar_parts.front();
        local_name = dollar_parts.back();
    } else {
        const auto dot = leaf.find_last_of('.');
        if (dot != std::string::npos && dot > 0U && dot + 1U < leaf.size()) {
            logical_node_path = trim_copy(std::string_view{leaf}.substr(0U, dot));
            local_name = trim_copy(std::string_view{leaf}.substr(dot + 1U));
        }
    }

    if (logical_node_path.empty()) {
        logical_node_path = fallback_logical_node_path;
    }
    if (ld_inst.empty()) {
        ld_inst = fallback_ld_inst;
    }

    return {ld_inst, logical_node_path, trim_copy(local_name)};
}

std::string build_canonical_reference(
    const std::string& ied_name,
    const std::string& ld_inst,
    const std::string& logical_node_path,
    const std::string& local_name) {
    if (local_name.empty()) {
        return {};
    }
    return ied_name + ld_inst + "/" + logical_node_path + "$" + local_name;
}

} // namespace

SclDataSetBindingResolution SclDataSetReferenceResolver::resolve(
    const std::span<const SclDataSet> data_sets,
    const std::string& ied_name,
    const std::string& ld_inst,
    const std::string& logical_node_path,
    const std::string& raw_reference) {
    if (trim_copy(raw_reference).empty()) {
        return {};
    }

    const auto parts = parse_reference(
        raw_reference,
        ied_name,
        ld_inst,
        logical_node_path);

    const SclDataSet* match{};
    std::size_t match_count{};
    for (const auto& data_set : data_sets) {
        if (same(data_set.ied_name, ied_name) &&
            same(data_set.ld_inst, parts.ld_inst) &&
            same(data_set.logical_node_path, parts.logical_node_path) &&
            same(data_set.name, parts.local_name)) {
            ++match_count;
            if (match_count == 1U) {
                match = &data_set;
            }
            if (match_count > 1U) {
                break;
            }
        }
    }

    if (match_count == 1U && match != nullptr) {
        return {
            match->entries.empty()
                ? SclDataSetBindingStatus::resolved_empty
                : SclDataSetBindingStatus::resolved,
            match,
            match->reference,
            match->name,
        };
    }

    return {
        SclDataSetBindingStatus::unresolved,
        nullptr,
        build_canonical_reference(
            ied_name,
            parts.ld_inst,
            parts.logical_node_path,
            parts.local_name),
        parts.local_name,
    };
}

} // namespace ar::iec61850::scl
