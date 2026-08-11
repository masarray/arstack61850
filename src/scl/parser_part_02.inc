std::string namespace_uri(const XmlNode& root) {
    const auto colon = root.name.find(':');
    if (colon == std::string::npos) {
        return attribute(&root, "xmlns");
    }
    return attribute(&root, "xmlns:" + root.name.substr(0U, colon));
}

std::optional<std::uint16_t> parse_u16(
    const std::string_view text,
    const bool prefer_hex_without_prefix) {
    auto trimmed = trim_copy(text);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    int base = prefer_hex_without_prefix ? 16 : 10;
    if (trimmed.size() > 2U && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
        trimmed.erase(0U, 2U);
        base = 16;
    }
    if (trimmed.empty()) {
        return std::nullopt;
    }

    std::uint32_t value{};
    const auto result = std::from_chars(
        trimmed.data(),
        trimmed.data() + trimmed.size(),
        value,
        base);
    if (result.ec != std::errc{} || result.ptr != trimmed.data() + trimmed.size() ||
        value > std::numeric_limits<std::uint16_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint16_t>(value);
}

std::optional<std::uint8_t> parse_u8(const std::string_view text) {
    const auto value = parse_u16(text, false);
    if (!value || *value > std::numeric_limits<std::uint8_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint8_t>(*value);
}

std::uint32_t uint_attribute(const XmlNode& node, const std::string_view name) {
    const auto text = attribute(&node, name);
    std::uint32_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 10);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() ? value : 0U;
}

std::uint16_t ushort_attribute(const XmlNode& node, const std::string_view name) {
    const auto value = uint_attribute(node, name);
    return value <= std::numeric_limits<std::uint16_t>::max()
        ? static_cast<std::uint16_t>(value)
        : 0U;
}

bool bool_attribute(const XmlNode& node, const std::string_view name) {
    return same(attribute(&node, name), "true");
}

std::string normalize_mac(const std::string_view text) {
    auto normalized = upper_copy(trim_copy(text));
    std::replace(normalized.begin(), normalized.end(), '-', ':');
    return normalized;
}

std::optional<std::array<std::uint8_t, 6>> parse_mac(const std::string_view text) {
    const auto normalized = normalize_mac(text);
    if (normalized.size() != 17U) {
        return std::nullopt;
    }

    std::array<std::uint8_t, 6> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        const auto offset = index * 3U;
        if (index + 1U < bytes.size() && normalized[offset + 2U] != ':') {
            return std::nullopt;
        }
        unsigned value{};
        const auto result = std::from_chars(
            normalized.data() + static_cast<std::ptrdiff_t>(offset),
            normalized.data() + static_cast<std::ptrdiff_t>(offset + 2U),
            value,
            16);
        if (result.ec != std::errc{} ||
            result.ptr != normalized.data() + static_cast<std::ptrdiff_t>(offset + 2U) ||
            value > 0xFFU) {
            return std::nullopt;
        }
        bytes[index] = static_cast<std::uint8_t>(value);
    }
    return bytes;
}

std::string normalize_app_id_text(const std::string_view text) {
    auto trimmed = trim_copy(text);
    if (trimmed.empty()) {
        return {};
    }
    if (trimmed.size() > 2U && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X')) {
        trimmed.erase(0U, 2U);
    }
    return "0x" + upper_copy(trimmed);
}

std::string data_set_key(
    const std::string& ied_name,
    const std::string& ld_inst,
    const std::string& logical_node_path,
    const std::string& data_set_name) {
    return ied_name + "|" + ld_inst + "|" + logical_node_path + "|" + data_set_name;
}

std::string normalized_key(const std::string_view text) {
    return lower_copy(trim_copy(text));
}

std::string stream_address_key(
    const std::string& kind,
    const std::string& ied_name,
    const std::string& ld_inst,
    const std::string& control_name) {
    return normalized_key(kind + "|" + ied_name + "|" + ld_inst + "|" + control_name);
}

std::string build_data_set_reference(
    const std::string& ied_name,
    const std::string& ld_inst,
    const std::string& logical_node_path,
    const std::string& data_set_name) {
    return data_set_name.empty()
        ? std::string{}
        : ied_name + ld_inst + "/" + logical_node_path + "$" + data_set_name;
}

std::string build_logical_node_path(const XmlNode& logical_node) {
    if (is(logical_node, "LN0")) {
        return "LLN0";
    }
    return attribute(&logical_node, "prefix") +
           attribute(&logical_node, "lnClass") +
           attribute(&logical_node, "inst");
}

std::string build_signal_reference(
    const std::string& ied_name,
    const std::string& ld_inst,
    const std::string& prefix,
    const std::string& ln_class,
    const std::string& ln_inst,
    const std::string& do_name,
    const std::string& da_name,
    const std::string& functional_constraint) {
    const auto logical_node = prefix + ln_class + ln_inst;
    const auto data = da_name.empty() ? do_name : do_name + "." + da_name;
    const auto fc_text = functional_constraint.empty()
        ? std::string{}
        : " [" + functional_constraint + "]";
    return ied_name + "/" + ld_inst + "/" + logical_node + "." + data + fc_text;
}

bool is_quality_attribute(const std::string& da_name) {
    const auto lower = lower_copy(da_name);
    return lower == "q" || lower.ends_with(".q");
}

bool is_timestamp_attribute(const std::string& da_name) {
    const auto lower = lower_copy(da_name);
    return lower == "t" || lower.ends_with(".t");
}

std::vector<std::string> split_name(const std::string& name) {
    std::vector<std::string> result;
    std::size_t offset{};
    while (offset <= name.size()) {
        const auto end = name.find('.', offset);
        const auto item = trim_copy(std::string_view{name}.substr(
            offset,
            end == std::string::npos ? std::string::npos : end - offset));
        if (!item.empty()) {
            result.push_back(item);
        }
        if (end == std::string::npos) {
            break;
        }
        offset = end + 1U;
    }
    return result;
}

class SclTypeIndex final {
public:
    struct Resolution final {
        bool resolved{};
        std::string functional_constraint;
        std::string cdc;
        std::string basic_type;
        std::string type_id;
        std::string enum_type;
    };

    struct ExpandedAttribute final {
        std::string da_name;
        Resolution resolution;
    };

    explicit SclTypeIndex(const XmlNode& root) {
        for (const auto* ied : direct_children(root, "IED")) {
            add_first(ieds_, attribute(ied, "name"), ied);
        }
        for (const auto* node : descendants(root, "LNodeType")) {
            add_first(logical_node_types_, attribute(node, "id"), node);
        }
        for (const auto* node : descendants(root, "DOType")) {
            add_first(data_object_types_, attribute(node, "id"), node);
        }
        for (const auto* node : descendants(root, "DAType")) {
            add_first(data_attribute_types_, attribute(node, "id"), node);
        }
        for (const auto* node : descendants(root, "EnumType")) {
            const auto id = attribute(node, "id");
            if (!id.empty()) {
                enum_type_ids_.insert(normalized_key(id));
            }
        }
    }

    [[nodiscard]] std::vector<ExpandedAttribute> expand_data_object(
        const std::string& ied_name,
        const std::string& ld_inst,
        const std::string& prefix,
        const std::string& ln_class,
        const std::string& ln_inst,
        const std::string& do_name,
        const std::string& functional_constraint) const {
        std::vector<ExpandedAttribute> expanded;
        const auto* logical_node = find_logical_node(
            ied_name,
            ld_inst,
            prefix,
            ln_class,
            ln_inst);
        if (logical_node == nullptr) {
            return expanded;
        }

        const auto* logical_node_type = find(
            logical_node_types_, attribute(logical_node, "lnType"));
        const auto do_segments = split_name(do_name);
        if (logical_node_type == nullptr || do_segments.empty()) {
            return expanded;
        }

        const XmlNode* data_object{};
        for (const auto* candidate : direct_children(*logical_node_type, "DO")) {
            if (same(attribute(candidate, "name"), do_segments.front())) {
                data_object = candidate;
                break;
            }
        }
        if (data_object == nullptr) {
            return expanded;
        }

        auto current_type_id = attribute(data_object, "type");
        const XmlNode* data_object_type = find(data_object_types_, current_type_id);
        if (data_object_type == nullptr) {
            return expanded;
        }

        for (std::size_t index = 1U; index < do_segments.size(); ++index) {
            const XmlNode* structured_data_object{};
            for (const auto* candidate : direct_children(*data_object_type, "SDO")) {
                if (same(attribute(candidate, "name"), do_segments[index])) {
                    structured_data_object = candidate;
                    break;
                }
            }
            if (structured_data_object == nullptr) {
                return {};
            }
            current_type_id = attribute(structured_data_object, "type");
            data_object_type = find(data_object_types_, current_type_id);
            if (data_object_type == nullptr) {
                return {};
            }
        }

        const auto cdc = attribute(data_object_type, "cdc");
        const auto append_leaf = [&expanded, &cdc](
                                     const std::string& name,
                                     const std::string& fc,
                                     const XmlNode& node,
                                     const bool resolved) {
            const auto basic_type = attribute(&node, "bType");
            const auto type_id = attribute(&node, "type");
            std::string enum_type;
            if (same(basic_type, "Enum")) {
                enum_type = type_id;
            }
            expanded.push_back({
                name,
                {resolved, fc, cdc, basic_type, type_id, enum_type},
            });
        };

        const auto append_bdas = [this, &expanded, &cdc](
                                     auto&& self,
                                     const XmlNode& container,
                                     const std::string& path,
                                     const std::string& fc) -> void {
            for (const auto* bda : direct_children(container, "BDA")) {
                const auto name = attribute(bda, "name");
                if (name.empty()) {
                    continue;
                }
                const auto child_path = path.empty() ? name : path + "." + name;
                const auto basic_type = attribute(bda, "bType");
                const auto type_id = attribute(bda, "type");
                if (same(basic_type, "Struct")) {
                    const auto* nested = find(data_attribute_types_, type_id);
                    if (nested == nullptr) {
                        expanded.push_back({
                            child_path,
                            {false, fc, cdc, basic_type, type_id, {}},
                        });
                    } else {
                        self(self, *nested, child_path, fc);
                    }
                    continue;
                }
                std::string enum_type;
                if (same(basic_type, "Enum") &&
                    enum_type_ids_.contains(normalized_key(type_id))) {
                    enum_type = type_id;
                }
                expanded.push_back({
                    child_path,
                    {true, fc, cdc, basic_type, type_id, enum_type},
                });
            }
        };

        for (const auto* data_attribute : direct_children(*data_object_type, "DA")) {
            const auto attribute_fc = attribute(data_attribute, "fc");
            if (!functional_constraint.empty() &&
                !same(attribute_fc, functional_constraint)) {
                continue;
            }
            const auto resolved_fc = functional_constraint.empty()
                ? attribute_fc
                : functional_constraint;
            const auto name = attribute(data_attribute, "name");
            if (name.empty()) {
                continue;
            }
            const auto basic_type = attribute(data_attribute, "bType");
            if (same(basic_type, "Struct")) {
                const auto type_id = attribute(data_attribute, "type");
                const auto* attribute_type = find(data_attribute_types_, type_id);
                if (attribute_type == nullptr) {
                    append_leaf(name, resolved_fc, *data_attribute, false);
                } else {
                    append_bdas(append_bdas, *attribute_type, name, resolved_fc);
                }
                continue;
            }
            append_leaf(name, resolved_fc, *data_attribute, true);
        }

        return expanded;
    }