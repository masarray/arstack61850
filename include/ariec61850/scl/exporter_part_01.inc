        for (const auto& entry : document.model_entries) {
            if (entry_matches_ln(entry, logical_node)) add_model_entry(model, entry);
        }
        models.push_back(std::move(model));
    }
    return models;
}

inline bool validate_model(
    const SclDocument& document,
    const SclCanonicalExportOptions& options,
    std::string& error) {
    if (options.edition == SclEdition::unknown) {
        error = "Canonical SCL export requires an explicit supported edition.";
        return false;
    }
    if (document.ieds.empty()) {
        error = "Canonical SCL export requires at least one IED.";
        return false;
    }
    if (document.logical_nodes.empty()) {
        error = "Canonical SCL export requires at least one logical node.";
        return false;
    }
    if (document.model_entries.empty()) {
        error = "Canonical SCL export requires a resolved DataTypeTemplates leaf model.";
        return false;
    }
    if ((options.profile == SclExportProfile::icd || options.profile == SclExportProfile::cid) &&
        document.ieds.size() != 1U) {
        error = profile_name(options.profile) + " export requires exactly one modeled IED.";
        return false;
    }
    for (const auto& conflict : document.conflicts) {
        if (same_text(conflict.kind, "IED")) {
            error = "Canonical SCL export rejected an ambiguous IED model: " + conflict.description;
            return false;
        }
    }
    for (const auto& entry : document.model_entries) {
        if (entry.ied_name.empty() || entry.ld_inst.empty() || entry.ln_class.empty() ||
            entry.do_name.empty() || entry.da_name.empty() ||
            entry.functional_constraint.empty() || entry.cdc.empty() || entry.basic_type.empty()) {
            error = "Canonical SCL export rejected an unresolved structural leaf: " + entry.signal_reference;
            return false;
        }
        if (entry.do_name.find('.') != std::string::npos) {
            error = "Canonical SCL export cannot reconstruct nested SDO parent CDC/type hierarchy for " +
                entry.signal_reference + "; use exact source preservation for this document.";
            return false;
        }
        if (same_text(entry.basic_type, "Enum")) {
            const auto is_control_model = same_text(entry.da_name, "ctlModel") ||
                same_text(entry.enum_type, "CtlModelKind") || same_text(entry.type_id, "CtlModelKind");
            if (!is_control_model) {
                error = "Canonical SCL export cannot reconstruct EnumType ordinals for " + entry.signal_reference +
                    "; only the modeled ctlModel domain is currently losslessly reconstructable.";
                return false;
            }
        }
    }
    return true;
}

inline std::string scalar_type_attribute(const DaNode& node) {
    if (same_text(node.basic_type, "Enum")) {
        const auto type = !node.enum_type.empty() ? node.enum_type : node.type_id;
        return type.empty() ? std::string{} : " type=\"" + xml_escape(type) + "\"";
    }
    return node.type_id.empty() ? std::string{} : " type=\"" + xml_escape(node.type_id) + "\"";
}

inline void write_da_type(
    std::ostringstream& out,
    const SclLogicalNode& ln,
    const std::string& do_path,
    const DaNode& parent,
    const std::string& da_path,
    const int indent) {
    const auto id = da_type_id(ln, do_path, da_path);
    out << std::string(static_cast<std::size_t>(indent), ' ')
        << "<DAType id=\"" << xml_escape(id) << "\">\n";
    for (const auto& child : parent.children) {
        const auto child_path = da_path.empty() ? child.name : da_path + "." + child.name;
        out << std::string(static_cast<std::size_t>(indent + 2), ' ')
            << "<BDA name=\"" << xml_escape(child.name) << "\"";
        if (!child.children.empty()) {
            out << " bType=\"Struct\" type=\""
                << xml_escape(da_type_id(ln, do_path, child_path)) << "\" />\n";
        } else {
            out << " bType=\"" << xml_escape(child.basic_type) << "\""
                << scalar_type_attribute(child) << " />\n";
        }
    }
    out << std::string(static_cast<std::size_t>(indent), ' ') << "</DAType>\n";
    for (const auto& child : parent.children) {
        if (!child.children.empty()) {
            const auto child_path = da_path.empty() ? child.name : da_path + "." + child.name;
            write_da_type(out, ln, do_path, child, child_path, indent);
        }
    }
}

inline void write_do_type(
    std::ostringstream& out,
    const SclLogicalNode& ln,
    const DoNode& node,
    const std::string& path,
    const int indent) {
    out << std::string(static_cast<std::size_t>(indent), ' ')
        << "<DOType id=\"" << xml_escape(do_type_id(ln, path)) << "\" cdc=\""
        << xml_escape(node.cdc) << "\">\n";
    for (const auto& child : node.children) {
        const auto child_path = path.empty() ? child.name : path + "." + child.name;
        out << std::string(static_cast<std::size_t>(indent + 2), ' ')
            << "<SDO name=\"" << xml_escape(child.name) << "\" type=\""
            << xml_escape(do_type_id(ln, child_path)) << "\" />\n";
    }
    for (const auto& attribute : node.attributes) {
        out << std::string(static_cast<std::size_t>(indent + 2), ' ')
            << "<DA name=\"" << xml_escape(attribute.name) << "\"";
        if (!attribute.children.empty()) {
            out << " bType=\"Struct\" type=\""
                << xml_escape(da_type_id(ln, path, attribute.name)) << "\"";
        } else {
            out << " bType=\"" << xml_escape(attribute.basic_type) << "\""
                << scalar_type_attribute(attribute);
        }
        out << " fc=\"" << xml_escape(attribute.functional_constraint) << "\" />\n";
    }
    out << std::string(static_cast<std::size_t>(indent), ' ') << "</DOType>\n";

    for (const auto& child : node.children) {
        const auto child_path = path.empty() ? child.name : path + "." + child.name;
        write_do_type(out, ln, child, child_path, indent);
    }
    for (const auto& attribute : node.attributes) {
        if (!attribute.children.empty()) write_da_type(out, ln, path, attribute, attribute.name, indent);
    }
}

inline void write_data_type_templates(
    std::ostringstream& out,
    const SclDocument& document,
    const std::vector<LnTypeModel>& models) {
    out << "  <DataTypeTemplates>\n";
    for (const auto& model : models) {
        const auto& ln = *model.logical_node;
        out << "    <LNodeType id=\"" << xml_escape(ln_type_id(ln))
            << "\" lnClass=\"" << xml_escape(ln.ln_class) << "\">\n";
        for (const auto& data_object : model.data_objects) {
            out << "      <DO name=\"" << xml_escape(data_object.name) << "\" type=\""
                << xml_escape(do_type_id(ln, data_object.name)) << "\" />\n";
        }
        out << "    </LNodeType>\n";
    }
    for (const auto& model : models) {
        const auto& ln = *model.logical_node;
        for (const auto& data_object : model.data_objects) {
            write_do_type(out, ln, data_object, data_object.name, 4);
        }
    }

    std::vector<std::string> control_model_enum_ids;
    for (const auto& entry : document.model_entries) {
        if (!same_text(entry.basic_type, "Enum") ||
            !(same_text(entry.da_name, "ctlModel") || same_text(entry.enum_type, "CtlModelKind") ||
              same_text(entry.type_id, "CtlModelKind"))) {
            continue;
        }
        const auto id = !entry.enum_type.empty() ? entry.enum_type :
            (!entry.type_id.empty() ? entry.type_id : std::string{"CtlModelKind"});
        if (std::find(control_model_enum_ids.begin(), control_model_enum_ids.end(), id) ==
            control_model_enum_ids.end()) {
            control_model_enum_ids.push_back(id);
        }
    }
    for (const auto& enum_id : control_model_enum_ids) {
        out << "    <EnumType id=\"" << xml_escape(enum_id) << "\">\n"
            << "      <EnumVal ord=\"0\">status-only</EnumVal>\n"
            << "      <EnumVal ord=\"1\">direct-with-normal-security</EnumVal>\n"
            << "      <EnumVal ord=\"2\">sbo-with-normal-security</EnumVal>\n"
            << "      <EnumVal ord=\"3\">direct-with-enhanced-security</EnumVal>\n"
            << "      <EnumVal ord=\"4\">sbo-with-enhanced-security</EnumVal>\n"
            << "    </EnumType>\n";
    }
    out << "  </DataTypeTemplates>\n";
}

inline void write_address(
    std::ostringstream& out,
    const SclStreamAddress& address,
    const int indent) {
    const auto spaces = std::string(static_cast<std::size_t>(indent), ' ');
    out << spaces << "<Address>\n";
    if (!address.destination_mac_text.empty()) {
        out << spaces << "  <P type=\"MAC-Address\">"
            << xml_escape(address.destination_mac_text) << "</P>\n";
    }
    if (!address.app_id_text.empty()) {
        out << spaces << "  <P type=\"APPID\">" << xml_escape(address.app_id_text) << "</P>\n";
    }
    if (address.vlan_id) {
        out << spaces << "  <P type=\"VLAN-ID\">" << *address.vlan_id << "</P>\n";
    }
    if (address.vlan_priority) {
        out << spaces << "  <P type=\"VLAN-PRIORITY\">"
            << static_cast<unsigned>(*address.vlan_priority) << "</P>\n";
    }
    out << spaces << "</Address>\n";
}

inline void write_communication(std::ostringstream& out, const SclDocument& document) {
    if (document.goose_streams.empty() && document.sampled_values_streams.empty()) return;
    out << "  <Communication>\n"
        << "    <SubNetwork name=\"ARStackCanonical\" type=\"8-MMS\">\n";
    for (const auto& ied : document.ieds) {
        bool has_stream = false;
        for (const auto& stream : document.goose_streams) {
            if (stream.ied_name == ied.name) { has_stream = true; break; }
        }
        if (!has_stream) {
            for (const auto& stream : document.sampled_values_streams) {
                if (stream.ied_name == ied.name) { has_stream = true; break; }
            }
        }
        if (!has_stream) continue;
        out << "      <ConnectedAP iedName=\"" << xml_escape(ied.name) << "\" apName=\"AP1\">\n";
        for (const auto& stream : document.goose_streams) {
            if (stream.ied_name != ied.name) continue;
