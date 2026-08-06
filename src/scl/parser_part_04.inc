        da_name,
        functional_constraint);

    if (!type.resolved && !signal_reference.empty()) {
        warnings.push_back("Type unresolved for " + signal_reference + ".");
    }

    return {
        index,
        signal_reference,
        ied_name,
        ld_inst,
        prefix,
        ln_class,
        ln_inst,
        do_name,
        da_name,
        functional_constraint.empty() ? type.functional_constraint : functional_constraint,
        type.cdc,
        type.basic_type,
        type.type_id,
        type.enum_type,
        is_quality_attribute(da_name),
        is_timestamp_attribute(da_name),
    };
}

SclStreamAddress build_address(const XmlNode& owner) {
    std::unordered_map<std::string, std::string> values;
    for (const auto* parameter : descendants(owner, "P")) {
        const auto type = normalized_key(attribute(parameter, "type"));
        if (!type.empty()) {
            values[type] = trim_copy(parameter->text);
        }
    }

    const auto get = [&values](const std::string_view type) {
        const auto match = values.find(normalized_key(type));
        return match == values.end() ? std::string{} : match->second;
    };

    const auto app_id_text = get("APPID");
    const auto mac_text = normalize_mac(get("MAC-Address"));
    return {
        normalize_app_id_text(app_id_text),
        parse_u16(app_id_text, true),
        mac_text,
        parse_mac(mac_text),
        parse_u16(get("VLAN-ID"), false),
        parse_u8(get("VLAN-PRIORITY")),
    };
}

const SclDataSet* resolve_data_set(
    const std::vector<SclDataSet>& data_sets,
    const std::unordered_map<std::string, std::size_t>& index,
    const std::string& ied_name,
    const std::string& ld_inst,
    const std::string& logical_node_path,
    const std::string& data_set_name) {
    if (data_set_name.empty()) {
        return nullptr;
    }
    const auto direct = index.find(normalized_key(
        data_set_key(ied_name, ld_inst, logical_node_path, data_set_name)));
    if (direct != index.end()) {
        return &data_sets[direct->second];
    }
    for (const auto& data_set : data_sets) {
        if (same(data_set.ied_name, ied_name) &&
            same(data_set.ld_inst, ld_inst) &&
            same(data_set.name, data_set_name)) {
            return &data_set;
        }
    }
    return nullptr;
}

SclEdition detect_edition(const XmlNode& root) {
    const auto version = attribute(&root, "version");
    const auto revision = attribute(&root, "revision");
    const auto release = attribute(&root, "release");
    const auto ns = lower_copy(namespace_uri(root));

    if (same(version, "2007") && same(revision, "B") &&
        (same(release, "4") || same(release, "4A"))) {
        return SclEdition::edition21;
    }
    if (same(version, "2007") || same(revision, "B")) {
        return SclEdition::edition2;
    }
    if (same(version, "2003") || same(revision, "A")) {
        return SclEdition::edition1;
    }
    if (version.empty() && revision.empty() && release.empty() &&
        ns.find("/2003/scl") != std::string::npos) {
        return SclEdition::edition1;
    }
    if (ns.find("2007b4") != std::string::npos ||
        ns.find("ed2.1") != std::string::npos ||
        ns.find("edition2.1") != std::string::npos) {
        return SclEdition::edition21;
    }
    if (ns.find("scl") != std::string::npos) {
        return SclEdition::edition2;
    }
    return SclEdition::unknown;
}

std::string read_text_file(const std::filesystem::path& path) {
    if (path.empty()) {
        throw std::invalid_argument("SCL file path is empty.");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("The selected SCL file does not exist or cannot be opened: " + path.string());
    }
    stream.seekg(0, std::ios::end);
    const auto length = stream.tellg();
    if (length < 0) {
        throw std::runtime_error("Unable to determine SCL file size: " + path.string());
    }
    const auto size = static_cast<std::uintmax_t>(length);
    if (size > max_xml_bytes) {
        throw std::runtime_error("SCL file exceeds the 64 MiB parser limit.");
    }
    stream.seekg(0, std::ios::beg);
    std::string text(static_cast<std::size_t>(size), '\0');
    if (!text.empty()) {
        stream.read(text.data(), static_cast<std::streamsize>(text.size()));
        if (!stream) {
            throw std::runtime_error("Unable to read the complete SCL file: " + path.string());
        }
    }
    return text;
}

} // namespace

SclDocument SclParser::load(const std::filesystem::path& file_path) const {
    return parse(read_text_file(file_path), file_path.filename().string());
}

SclDocument SclParser::parse(
    const std::string_view xml,
    std::string source_name) const {
    if (trim_copy(xml).empty()) {
        throw std::invalid_argument("SCL XML is empty.");
    }

    const auto root = SecureXmlParser(xml).parse_document();
    if (!is(root, "SCL")) {
        throw std::runtime_error("The selected file is not an IEC 61850 SCL document.");
    }

    SclDocument document;
    document.source_name = std::move(source_name);
    document.namespace_uri = namespace_uri(root);
    document.edition = detect_edition(root);

    if (const auto* header = first_child(root, "Header"); header != nullptr) {
        document.header_id = attribute(header, "id");
        document.header_version = attribute(header, "version");
        document.header_revision = attribute(header, "revision");
    }

    for (const auto* ied : direct_children(root, "IED")) {
        document.ieds.push_back({
            attribute(ied, "name"),
            attribute(ied, "manufacturer"),
            attribute(ied, "type"),
            attribute(ied, "configVersion"),
        });
    }

    const SclTypeIndex type_index(root);
    for (const auto* ied : direct_children(root, "IED")) {
        const auto ied_name = attribute(ied, "name");
        for (const auto* logical_device : descendants(*ied, "LDevice")) {
            const auto ld_inst = attribute(logical_device, "inst");
            for (const auto& logical_node : logical_device->children) {
                if (!is(logical_node, "LN0") && !is(logical_node, "LN")) {
                    continue;
                }
                const auto logical_node_path = build_logical_node_path(logical_node);
                for (const auto* data_set_node : direct_children(logical_node, "DataSet")) {
                    SclDataSet data_set;
                    data_set.ied_name = ied_name;
                    data_set.ld_inst = ld_inst;
                    data_set.logical_node_path = logical_node_path;
                    data_set.name = attribute(data_set_node, "name");
                    data_set.key = data_set_key(
                        ied_name,
                        ld_inst,
                        logical_node_path,
                        data_set.name);
                    data_set.reference = build_data_set_reference(
                        ied_name,
                        ld_inst,
                        logical_node_path,
                        data_set.name);

                    std::size_t entry_index = 1U;
                    for (const auto* fcda : direct_children(*data_set_node, "FCDA")) {
                        data_set.entries.push_back(build_data_set_entry(
                            ied_name,
                            *fcda,
                            entry_index++,
                            type_index,
                            document.warnings));
                    }
                    document.data_sets.push_back(std::move(data_set));
                }
            }
        }
    }

    std::unordered_map<std::string, std::size_t> data_set_index;
    for (std::size_t index = 0U; index < document.data_sets.size(); ++index) {
        data_set_index.try_emplace(
            normalized_key(document.data_sets[index].key),
            index);
    }

    std::unordered_map<std::string, SclStreamAddress> address_index;
