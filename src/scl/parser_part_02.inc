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
