// SPDX-License-Identifier: GPL-3.0-or-later

namespace ar::iec61850::mms {
namespace live_model_detail {

[[nodiscard]] inline std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return value;
}
[[nodiscard]] inline bool same(std::string_view a, std::string_view b) {
    return lower(std::string{a}) == lower(std::string{b});
}
[[nodiscard]] inline std::vector<std::string> split(std::string_view value, char separator) {
    std::vector<std::string> out;
    std::size_t start{};
    while (start <= value.size()) {
        const auto end = value.find(separator, start);
        out.emplace_back(value.substr(start, end == std::string_view::npos
            ? std::string_view::npos : end - start));
        if (end == std::string_view::npos) break;
        start = end + 1U;
    }
    return out;
}
[[nodiscard]] inline std::string confidence(MmsLiveModelConfidence value) {
    switch (value) {
    case MmsLiveModelConfidence::exact: return "Exact";
    case MmsLiveModelConfidence::high: return "High";
    case MmsLiveModelConfidence::medium: return "Medium";
    case MmsLiveModelConfidence::low: return "Low";
    case MmsLiveModelConfidence::unknown: return "Unknown";
    }
    return "Unknown";
}
[[nodiscard]] inline std::string json(std::string_view value) {
    std::ostringstream out;
    for (char c : value) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << (static_cast<unsigned char>(c) < 0x20U ? '?' : c); break;
        }
    }
    return out.str();
}
[[nodiscard]] inline std::string key(std::string value) {
    std::replace(value.begin(), value.end(), '$', '.');
    return lower(std::move(value));
}
[[nodiscard]] inline std::string bit_names(const MmsReportBitField& field) {
    std::ostringstream out;
    for (std::size_t i = 0; i < field.names.size(); ++i) {
        if (i) out << ',';
        out << field.names[i];
    }
    return out.str();
}
[[nodiscard]] inline std::string optional_bool(std::optional<bool> value) {
    return value ? (*value ? "true" : "false") : std::string{};
}
[[nodiscard]] inline std::string optional_u64(std::optional<std::uint64_t> value) {
    return value ? std::to_string(*value) : std::string{};
}
[[nodiscard]] inline const MmsTypeSpecification* resolve_type(
    const MmsLivePointReference& point,
    const std::vector<MmsVariableTypeEvidence>& evidence,
    std::string& source) {
    const auto target = split(point.mms_item_name, '$');
    const MmsTypeSpecification* best{};
    std::size_t best_depth{};
    for (const auto& item : evidence) {
        if (!item.success() || !same(item.variable.domain, point.domain)) continue;
        const auto root = split(item.variable.item, '$');
        if (root.empty() || root.size() > target.size()) continue;
        bool prefix = true;
        for (std::size_t i = 0; i < root.size(); ++i) prefix &= same(root[i], target[i]);
        if (!prefix) continue;
        const MmsTypeSpecification* current = &item.attributes->type;
        for (std::size_t i = root.size(); current && i < target.size(); ++i) {
            const auto found = std::find_if(current->children.begin(), current->children.end(),
                [&](const auto& child) { return same(child.name, target[i]); });
            current = found == current->children.end() ? nullptr : &*found;
        }
        if (current && root.size() >= best_depth) {
            best = current;
            best_depth = root.size();
            source = root.size() == target.size()
                ? "GetVariableAccessAttributes"
                : "GetVariableAccessAttributesLogicalNodeTree";
        }
    }
    return best;
}
[[nodiscard]] inline std::pair<std::string, double> infer_cdc(
    std::string_view ln_class,
    std::string_view name,
    const std::vector<MmsLiveDataAttribute>& attributes) {
    const auto ln = lower(std::string{ln_class});
    const auto object = lower(std::string{name});
    if (ln == "mmxu" && (object == "a" || object == "phv")) return {"WYE", 0.95};
    if (ln == "mmxu" && object == "ppv") return {"DEL", 0.95};
    if (object == "pos") return {"DPC", 0.92};
    if (object == "mod") return {"INC", 0.90};
    if (object == "namplt") return {"LPL", 0.92};
    bool st_val{}, magnitude{}, quality{}, timestamp{};
    for (const auto& attribute : attributes) {
        const auto leaf = lower(attribute.attribute_path);
        st_val |= leaf == "stval" || leaf.ends_with(".stval");
        magnitude |= leaf == "mag.f" || leaf.ends_with(".mag.f") || leaf == "instmag.f";
        quality |= leaf == "q" || leaf.ends_with(".q");
        timestamp |= leaf == "t" || leaf.ends_with(".t");
    }
