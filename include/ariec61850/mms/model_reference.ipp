// SPDX-License-Identifier: GPL-3.0-or-later

namespace ar::iec61850::mms {
namespace model_reference_detail {

[[nodiscard]] inline std::string trim_copy(const std::string_view value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string{value.substr(first, last - first + 1U)};
}

[[nodiscard]] inline std::string upper_copy(const std::string_view value) {
    auto result = trim_copy(value);
    std::transform(result.begin(), result.end(), result.begin(), [](const char character) {
        return static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    });
    return result;
}

[[nodiscard]] inline std::vector<std::string> split_non_empty(
    const std::string_view value,
    const char delimiter) {
    std::vector<std::string> result;
    std::size_t start = 0U;
    while (start <= value.size()) {
        const auto end = value.find(delimiter, start);
        const auto part = trim_copy(value.substr(
            start,
            end == std::string_view::npos ? std::string_view::npos : end - start));
        if (!part.empty()) {
            result.push_back(part);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1U;
    }
    return result;
}

[[nodiscard]] inline std::string join(
    const std::vector<std::string>& values,
    const std::size_t begin,
    const std::size_t end,
    const char delimiter) {
    std::string result;
    for (std::size_t index = begin; index < end; ++index) {
        if (!result.empty()) {
            result.push_back(delimiter);
        }
        result += values[index];
    }
    return result;
}

[[nodiscard]] inline bool is_upper_ascii(const char value) noexcept {
    return value >= 'A' && value <= 'Z';
}

} // namespace model_reference_detail

inline std::string MmsLivePointReference::user_path() const {
    return data_object_path.empty()
        ? logical_node
        : logical_node + "." + data_object_path;
}

inline std::string MmsLivePointReference::user_reference() const {
    return domain.empty() ? user_path() : domain + "/" + user_path();
}

inline std::string MmsLivePointReference::mms_reference() const {
    return domain.empty() ? mms_item_name : domain + "/" + mms_item_name;
}

inline bool MmsLivePointReference::report_attribute() const noexcept {
    return functional_constraint == "RP" || functional_constraint == "BR";
}

inline bool MmsLivePointReference::control_attribute() const noexcept {
    return functional_constraint == "CO";
}

inline bool MmsLiveReferenceParser::known_functional_constraint(
    const std::string_view value) noexcept {
    static constexpr std::array<std::string_view, 20U> constraints{
        "ST", "MX", "CO", "SP", "SG", "SE", "SV", "CF", "DC", "EX",
        "SR", "OR", "BL", "RP", "BR", "LG", "GO", "GS", "MS", "US"};
    const auto normalized = model_reference_detail::upper_copy(value);
    return std::find(constraints.begin(), constraints.end(), normalized) !=
           constraints.end();
}

inline std::string MmsLiveReferenceParser::normalize_functional_constraint(
    const std::string_view value) {
    return model_reference_detail::upper_copy(value);
}

inline std::optional<MmsLivePointReference> MmsLiveReferenceParser::parse_variable(
    const std::string_view domain,
    const std::string_view raw_mms_name,
    std::string source,
    const std::uint32_t confidence) {
    const auto normalized_domain = model_reference_detail::trim_copy(domain);
    const auto normalized_item = model_reference_detail::trim_copy(raw_mms_name);
    if (normalized_domain.empty() || normalized_item.empty()) {
        return std::nullopt;
    }

    const auto parts = model_reference_detail::split_non_empty(normalized_item, '$');
    if (parts.size() < 3U) {
        return std::nullopt;
    }

    std::size_t functional_constraint_index = parts.size();
    for (std::size_t index = 1U; index < parts.size(); ++index) {
        if (known_functional_constraint(parts[index])) {
            functional_constraint_index = index;
            break;
        }
    }
    if (functional_constraint_index < 1U ||
        functional_constraint_index + 1U >= parts.size()) {
        return std::nullopt;
    }

    MmsLivePointReference point;
    point.domain = normalized_domain;
    point.logical_node = model_reference_detail::join(parts, 0U, functional_constraint_index, '$');
    point.functional_constraint = normalize_functional_constraint(
        parts[functional_constraint_index]);
    point.data_object_path = model_reference_detail::join(
        parts,
        functional_constraint_index + 1U,
        parts.size(),
        '.');
    point.mms_item_name = model_reference_detail::join(parts, 0U, parts.size(), '$');
    point.source = std::move(source);
    point.confidence = confidence;
    if (point.logical_node.empty() || point.data_object_path.empty()) {
        return std::nullopt;
    }
    return point;
}

inline MmsLogicalNodeName MmsLiveReferenceParser::parse_logical_node_name(
    const std::string_view logical_node_name) {
    const auto normalized = model_reference_detail::trim_copy(logical_node_name);
    if (normalized.empty()) {
        return {};
    }
    if (model_reference_detail::upper_copy(normalized) == "LLN0") {
        return {normalized, {}, "LLN0", {}};
    }

    for (std::size_t index = 0U; index + 4U <= normalized.size(); ++index) {
        if (!model_reference_detail::is_upper_ascii(normalized[index]) ||
            !model_reference_detail::is_upper_ascii(normalized[index + 1U]) ||
            !model_reference_detail::is_upper_ascii(normalized[index + 2U]) ||
            !model_reference_detail::is_upper_ascii(normalized[index + 3U])) {
            continue;
        }
        return {
            normalized,
            normalized.substr(0U, index),
            normalized.substr(index, 4U),
            normalized.substr(index + 4U)};
    }
    return {normalized, {}, normalized, {}};
}

inline std::string MmsLiveReferenceParser::top_data_object_name(
    const std::string_view data_object_path) {
    const auto normalized = model_reference_detail::trim_copy(data_object_path);
    const auto dot = normalized.find('.');
    return dot == std::string::npos ? normalized : normalized.substr(0U, dot);
}

inline std::string MmsLiveReferenceParser::data_attribute_path(
    const std::string_view data_object_path) {
    const auto normalized = model_reference_detail::trim_copy(data_object_path);
    const auto dot = normalized.find('.');
    if (dot == std::string::npos || dot + 1U >= normalized.size()) {
        return {};
    }
    return normalized.substr(dot + 1U);
}

} // namespace ar::iec61850::mms
