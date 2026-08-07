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

// Mirrors ARIEC61850 LiveIedModelDiscoveryBuilder.GuessSclBType.  This does
// not pretend to be exact MMS type evidence; it only supplies a conservative
// SCL-oriented hint when GetVariableAccessAttributes was skipped or could not
// resolve the leaf from a returned type tree.
[[nodiscard]] inline std::string guess_scl_basic_type(
    const std::string_view attribute_path,
    const std::string_view functional_constraint) {
    const auto parts = split(attribute_path, '.');
    const auto name = parts.empty() ? std::string{} : lower(parts.back());
    if (name == "q") return "Quality";
    if (name == "t" || name.ends_with("tm")) return "Timestamp";
    if (name == "stval") return "BOOLEAN";
    if (name == "f") return "FLOAT32";
    if (name == "i") return "INT32";
    if (same(functional_constraint, "CO")) return "Struct";
    return "Unknown";
}

[[nodiscard]] inline bool attribute_matches(
    const std::string_view path,
    const std::string_view candidate) {
    const auto value = lower(std::string{path});
    const auto wanted = lower(std::string{candidate});
    return value == wanted ||
        (value.size() > wanted.size() &&
         value.ends_with("." + wanted)) ||
        (value.size() > wanted.size() &&
         value.starts_with(wanted + "."));
}

[[nodiscard]] inline bool has_attribute(
    const std::vector<MmsLiveDataAttribute>& attributes,
    const std::initializer_list<std::string_view> candidates) {
    return std::any_of(attributes.begin(), attributes.end(), [&](const auto& attribute) {
        return std::any_of(candidates.begin(), candidates.end(), [&](const auto candidate) {
            return attribute_matches(attribute.attribute_path, candidate);
        });
    });
}

[[nodiscard]] inline bool has_fc(
    const std::vector<MmsLiveDataAttribute>& attributes,
    const std::string_view functional_constraint) {
    return std::any_of(attributes.begin(), attributes.end(), [&](const auto& attribute) {
        return same(attribute.functional_constraint, functional_constraint);
    });
}

[[nodiscard]] inline std::optional<std::pair<std::string, double>> standard_cdc(
    const std::string_view logical_node_class,
    const std::string_view data_object_name) {
    const auto ln = lower(std::string{logical_node_class});
    const auto object = lower(std::string{data_object_name});
    const auto exact = [&](const std::string_view expected_ln,
                           const std::string_view expected_object,
                           const char* cdc,
                           const double score)
        -> std::optional<std::pair<std::string, double>> {
        if (ln == expected_ln && object == expected_object) {
            return std::pair<std::string, double>{cdc, score};
        }
        return std::nullopt;
    };

    if (const auto value = exact("lln0", "namplt", "LPL", 0.98)) return value;
    if (const auto value = exact("lln0", "mod", "INC", 0.94)) return value;
    if (const auto value = exact("lln0", "beh", "INS", 0.94)) return value;
    if (const auto value = exact("lln0", "health", "INS", 0.94)) return value;
    if (const auto value = exact("lphd", "phynam", "DPL", 0.98)) return value;
    if (const auto value = exact("lphd", "proxy", "SPS", 0.92)) return value;
    if (const auto value = exact("lphd", "phyhealth", "INS", 0.94)) return value;
    if (const auto value = exact("ptoc", "op", "ACT", 0.94)) return value;
    if (const auto value = exact("ptoc", "str", "ACD", 0.94)) return value;
    if (const auto value = exact("ptrc", "op", "ACT", 0.94)) return value;
    if (const auto value = exact("rrec", "op", "ACT", 0.92)) return value;
    if (const auto value = exact("cswi", "pos", "DPC", 0.94)) return value;
    if (const auto value = exact("xcbr", "pos", "DPC", 0.94)) return value;
    if (const auto value = exact("xcbr", "cbopcap", "INS", 0.90)) return value;
    if (const auto value = exact("xcbr", "opcnt", "INS", 0.86)) return value;
    if (const auto value = exact("rdre", "fltnum", "INS", 0.90)) return value;
    if (const auto value = exact("rdre", "grifltnum", "INS", 0.90)) return value;
    if (const auto value = exact("mmxu", "phv", "WYE", 0.94)) return value;
    if (const auto value = exact("mmxu", "a", "WYE", 0.94)) return value;
    if (const auto value = exact("mmxu", "ppv", "DEL", 0.92)) return value;
    if (const auto value = exact("mmxu", "w", "WYE", 0.86)) return value;
    if (const auto value = exact("mmxu", "var", "WYE", 0.86)) return value;
    if (const auto value = exact("mmxu", "va", "WYE", 0.86)) return value;
    if (const auto value = exact("mmxu", "pf", "WYE", 0.84)) return value;
    if (const auto value = exact("msqi", "seqa", "SEQ", 0.92)) return value;
    if (const auto value = exact("msqi", "seqv", "SEQ", 0.92)) return value;

    if (object.starts_with("dpcso")) return std::pair<std::string, double>{"DPC", 0.88};
    if (object.starts_with("spcso")) return std::pair<std::string, double>{"SPC", 0.88};
    if (object.starts_with("iscso")) return std::pair<std::string, double>{"ISC", 0.86};
    if (object.ends_with("cntrs") || object.ends_with("cntrst")) {
        return std::pair<std::string, double>{"INC", 0.82};
    }
    if (object.starts_with("sum") || object.starts_with("sup") ||
        object.starts_with("dmd")) {
        return std::pair<std::string, double>{"BCR", 0.80};
    }
    if (object.starts_with("seq")) return std::pair<std::string, double>{"SEQ", 0.80};
    return std::nullopt;
}

// Port of the conservative ARIEC61850 CdcInferenceEngine decision order.  It
// intentionally returns no CDC rather than inventing a precise type when live
// MMS name/type evidence is insufficient.
[[nodiscard]] inline std::pair<std::string, double> infer_cdc(
    const std::string_view ln_class,
    const std::string_view name,
    const std::vector<MmsLiveDataAttribute>& attributes) {
    if (const auto standard = standard_cdc(ln_class, name)) {
        return *standard;
    }

    const auto ln = lower(std::string{ln_class});
    const auto object = lower(std::string{name});
    if (object == "namplt") return {"LPL", 0.96};
    if (object == "phynam") return {"DPL", 0.96};
    if (object == "sgcb") return {"", 0.0};
    if (object == "mod") return {"INC", 0.90};
    if (object == "beh" || object == "health" || object == "autorecst" ||
        object == "optmh") {
        return {"INS", 0.88};
    }
    if (object == "proxy") return {"SPS", 0.86};

    if (has_attribute(attributes, {"actVal", "pulsQty"}) ||
        object.starts_with("sum") || object.starts_with("sup") ||
        object.starts_with("dmd")) {
        return {"BCR", 0.84};
    }
    if (object.starts_with("seq") || has_attribute(attributes, {"seqT"}) ||
        (has_attribute(attributes, {"c1"}) &&
         has_attribute(attributes, {"c2"}) &&
         has_attribute(attributes, {"c3"}))) {
        return {"SEQ", 0.82};
    }
    if (has_attribute(attributes, {"phsAB", "phsBC", "phsCA"})) {
        return {"DEL", 0.80};
    }

    const bool protection_activation =
        (object == "op" || object == "tr" || object.starts_with("op")) &&
        has_fc(attributes, "ST") &&
        has_attribute(attributes, {"general", "q", "t"});
    if (protection_activation) return {"ACT", 0.86};

    const bool protection_start =
        object == "str" && has_fc(attributes, "ST") &&
        has_attribute(attributes, {"general", "dirGeneral", "q", "t"});
    if (protection_start) return {"ACD", 0.84};

    if (object == "pos" || object.starts_with("dpcso") ||
        has_attribute(attributes, {"stSeld"})) {
        return {"DPC", 0.80};
    }
    if (object.starts_with("spcso") || object == "blkopn" ||
        object == "blkcls" ||
        (has_attribute(attributes, {"ctlVal"}) && object != "pos")) {
        return {"SPC", 0.78};
    }

    const bool status_triplet =
        has_attribute(attributes, {"stVal"}) &&
        has_attribute(attributes, {"q"}) &&
        has_attribute(attributes, {"t"});
    if (status_triplet) {
        if (object == "op" && (!ln.empty() && (ln.front() == 'p' || ln == "rrec"))) {
            return {"ACT", 0.86};
        }
        if (object == "str") return {"ACD", 0.82};
        return {"SPS", 0.78};
    }

    const bool phase_structure = has_attribute(
        attributes, {"phsA", "phsB", "phsC", "neut"});
    if (phase_structure) {
        if (ln == "mmxu") return {"WYE", 0.78};
        return {"ACD", 0.62};
    }

    if (has_attribute(attributes, {"cVal", "instCVal"}) &&
        has_attribute(attributes, {"q"}) &&
        has_attribute(attributes, {"t"}) && !phase_structure) {
        return {"CMV", 0.80};
    }
    if (has_attribute(attributes, {"mag", "mag.f", "mag.i"}) &&
        has_attribute(attributes, {"q"}) &&
        has_attribute(attributes, {"t"})) {
        return {"MV", 0.80};
    }

    if (has_attribute(attributes, {"ctlVal", "Oper", "SBOw", "Cancel"}) ||
        has_fc(attributes, "CO")) {
        return {"SPC", 0.55};
    }
    if (has_attribute(attributes, {"setVal"}) || has_fc(attributes, "SP") ||
        has_fc(attributes, "SG") || has_fc(attributes, "SE")) {
        return {"SPG", 0.45};
    }
    if (has_fc(attributes, "MX")) return {"MV", 0.40};
    if (has_fc(attributes, "ST")) return {"SPS", 0.38};
    return {"", 0.0};
}
