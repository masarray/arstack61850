inline void write_fcda(std::ostringstream& out, const SclDataSetEntry& entry) {
    out << "              <FCDA";
    if (!entry.ied_name.empty()) out << " iedName=\"" << xml_escape(entry.ied_name) << "\"";
    if (!entry.ld_inst.empty()) out << " ldInst=\"" << xml_escape(entry.ld_inst) << "\"";
    if (!entry.prefix.empty()) out << " prefix=\"" << xml_escape(entry.prefix) << "\"";
    if (!entry.ln_class.empty()) out << " lnClass=\"" << xml_escape(entry.ln_class) << "\"";
    if (!entry.ln_inst.empty()) out << " lnInst=\"" << xml_escape(entry.ln_inst) << "\"";
    if (!entry.do_name.empty()) out << " doName=\"" << xml_escape(entry.do_name) << "\"";
    if (!entry.da_name.empty()) out << " daName=\"" << xml_escape(entry.da_name) << "\"";
    if (!entry.functional_constraint.empty()) {
        out << " fc=\"" << xml_escape(entry.functional_constraint) << "\"";
    }
    out << " />\n";
}

inline void write_data_sets(
    std::ostringstream& out,
    const SclDocument& document,
    const SclLogicalNode& ln) {
    for (const auto& data_set : document.data_sets) {
        if (data_set.ied_name != ln.ied_name || data_set.ld_inst != ln.ld_inst ||
            data_set.logical_node_path != ln.name) continue;
        out << "            <DataSet name=\"" << xml_escape(data_set.name) << "\">\n";
        for (const auto& entry : data_set.entries) write_fcda(out, entry);
        out << "            </DataSet>\n";
    }
}

inline void write_reports(
    std::ostringstream& out,
    const SclDocument& document,
    const SclLogicalNode& ln) {
    for (const auto& report : document.report_controls) {
        if (report.ied_name != ln.ied_name || report.ld_inst != ln.ld_inst ||
            report.logical_node_path != ln.name) continue;
        out << "            <ReportControl name=\"" << xml_escape(report.name) << "\"";
        if (!report.data_set_name.empty()) out << " datSet=\"" << xml_escape(report.data_set_name) << "\"";
        if (!report.report_id.empty()) out << " rptID=\"" << xml_escape(report.report_id) << "\"";
        out << " confRev=\"" << report.configuration_revision << "\""
            << " buffered=\"" << bool_text(report.buffered) << "\""
            << " indexed=\"" << bool_text(report.indexed) << "\""
            << " bufTime=\"" << report.buffer_time_milliseconds << "\""
            << " intgPd=\"" << report.integrity_period_milliseconds << "\" />\n";
    }
}

inline void write_goose_and_smv(
    std::ostringstream& out,
    const SclDocument& document,
    const SclLogicalNode& ln) {
    if (!same_text(ln.ln_class, "LLN0")) return;
    for (const auto& stream : document.goose_streams) {
        if (stream.ied_name != ln.ied_name || stream.ld_inst != ln.ld_inst) continue;
        out << "            <GSEControl name=\"" << xml_escape(stream.control_name) << "\"";
        if (!stream.data_set_name.empty()) out << " datSet=\"" << xml_escape(stream.data_set_name) << "\"";
        if (!stream.go_id.empty()) out << " appID=\"" << xml_escape(stream.go_id) << "\"";
        out << " confRev=\"" << stream.configuration_revision << "\" type=\"GOOSE\""
            << " minTime=\"" << stream.min_time_milliseconds << "\""
            << " maxTime=\"" << stream.max_time_milliseconds << "\" />\n";
    }
    for (const auto& stream : document.sampled_values_streams) {
        if (stream.ied_name != ln.ied_name || stream.ld_inst != ln.ld_inst) continue;
        out << "            <SampledValueControl name=\"" << xml_escape(stream.control_name) << "\"";
        if (!stream.data_set_name.empty()) out << " datSet=\"" << xml_escape(stream.data_set_name) << "\"";
        if (!stream.smv_id.empty()) out << " smvID=\"" << xml_escape(stream.smv_id) << "\"";
        if (!stream.sv_id.empty() && stream.sv_id != stream.smv_id) {
            out << " svID=\"" << xml_escape(stream.sv_id) << "\"";
        }
        out << " confRev=\"" << stream.configuration_revision << "\""
            << " smpRate=\"" << stream.sample_rate << "\""
            << " nofASDU=\"" << stream.no_asdu << "\"";
        if (!stream.sample_mode.empty()) out << " smpMod=\"" << xml_escape(stream.sample_mode) << "\"";
        if (!stream.smv_options.element_present) {
            out << " multicast=\"true\" />\n";
        } else {
            out << " multicast=\"true\">\n"
                << "              <SmvOpts refreshTime=\"" << bool_text(stream.smv_options.refresh_time)
                << "\" sampleSynchronized=\"" << bool_text(stream.smv_options.sample_synchronized)
                << "\" sampleRate=\"" << bool_text(stream.smv_options.sample_rate)
                << "\" dataSet=\"" << bool_text(stream.smv_options.data_set)
                << "\" security=\"" << bool_text(stream.smv_options.security)
                << "\" synchSourceId=\"" << bool_text(stream.smv_options.synch_source_id)
                << "\" />\n"
                << "            </SampledValueControl>\n";
        }
    }
}

struct InstanceNode final {
    std::map<std::string, InstanceNode, std::less<>> children;
    std::map<std::string, std::string, std::less<>> values;
};

inline void write_instance_node(
    std::ostringstream& out,
    const InstanceNode& node,
    const int indent,
    const bool top_level) {
    for (const auto& [name, child] : node.children) {
        const auto spaces = std::string(static_cast<std::size_t>(indent), ' ');
        out << spaces << '<' << (top_level ? "DOI" : "SDI") << " name=\""
            << xml_escape(name) << "\">\n";
        for (const auto& [da_name, value] : child.values) {
            out << spaces << "  <DAI name=\"" << xml_escape(da_name) << "\"><Val>"
                << xml_escape(value) << "</Val></DAI>\n";
        }
        write_instance_node(out, child, indent + 2, false);
        out << spaces << "</" << (top_level ? "DOI" : "SDI") << ">\n";
    }
}

inline void write_configured_values(
    std::ostringstream& out,
    const SclDocument& document,
    const SclLogicalNode& ln) {
    InstanceNode root;
    for (const auto& entry : document.model_entries) {
        if (!entry_matches_ln(entry, ln) || entry.configured_value.empty()) continue;
        const auto do_segments = split_path(entry.do_name);
        const auto da_segments = split_path(entry.da_name);
        if (do_segments.empty() || da_segments.size() != 1U) continue;
        auto* node = &root;
        for (const auto& segment : do_segments) node = &node->children[segment];
        node->values[da_segments.front()] = entry.configured_value;
    }
    write_instance_node(out, root, 12, true);
}

inline void write_setting_control(
    std::ostringstream& out,
    const SclDocument& document,
    const SclLogicalNode& ln) {
    if (!same_text(ln.ln_class, "LLN0")) return;
    const auto found = std::find_if(
        document.setting_controls.begin(), document.setting_controls.end(),
        [&](const auto& setting) {
            return setting.ied_name == ln.ied_name &&
                   setting.ld_inst == ln.ld_inst &&
                   setting.logical_node_path == ln.name &&
                   setting.valid();
        });
    if (found == document.setting_controls.end()) return;
    out << "            <SettingControl numOfSGs=\"" << *found->number_of_setting_groups
        << "\" actSG=\"" << *found->active_setting_group << "\" />\n";
}

inline void write_logical_node(
    std::ostringstream& out,
    const SclDocument& document,
    const SclLogicalNode& ln) {
    const auto zero = same_text(ln.ln_class, "LLN0");
    out << "          <" << (zero ? "LN0" : "LN");
    if (!zero && !ln.prefix.empty()) out << " prefix=\"" << xml_escape(ln.prefix) << "\"";
    out << " lnClass=\"" << xml_escape(ln.ln_class) << "\"";
    if (!zero && !ln.ln_inst.empty()) out << " inst=\"" << xml_escape(ln.ln_inst) << "\"";
    out << " lnType=\"" << xml_escape(ln_type_id(ln)) << "\">\n";
    write_data_sets(out, document, ln);
    write_goose_and_smv(out, document, ln);
    write_setting_control(out, document, ln);
    write_reports(out, document, ln);
    write_configured_values(out, document, ln);
    out << "          </" << (zero ? "LN0" : "LN") << ">\n";
}

inline void write_ieds(std::ostringstream& out, const SclDocument& document) {
    for (const auto& ied : document.ieds) {
        out << "  <IED name=\"" << xml_escape(ied.name) << "\"";
        if (!ied.manufacturer.empty()) out << " manufacturer=\"" << xml_escape(ied.manufacturer) << "\"";
        if (!ied.type.empty()) out << " type=\"" << xml_escape(ied.type) << "\"";
        if (!ied.config_version.empty()) out << " configVersion=\"" << xml_escape(ied.config_version) << "\"";
        const auto access_point = std::find_if(
            document.mms_access_points.begin(), document.mms_access_points.end(),
            [&](const auto& value) { return value.ied_name == ied.name; });
        const auto ap_name =
            access_point != document.mms_access_points.end() &&
            !access_point->access_point_name.empty()
                ? access_point->access_point_name
                : std::string{"AP1"};
        out << ">\n    <AccessPoint name=\"" << xml_escape(ap_name)
            << "\">\n      <Server>\n";

        std::vector<std::string> ld_order;
        for (const auto& ln : document.logical_nodes) {
            if (ln.ied_name != ied.name) continue;
            if (std::find(ld_order.begin(), ld_order.end(), ln.ld_inst) == ld_order.end()) {
                ld_order.push_back(ln.ld_inst);
            }
        }
        for (const auto& ld_inst : ld_order) {
            out << "        <LDevice inst=\"" << xml_escape(ld_inst) << "\">\n";
            for (const auto& ln : document.logical_nodes) {
                if (ln.ied_name == ied.name && ln.ld_inst == ld_inst) {
                    write_logical_node(out, document, ln);
                }
            }
            out << "        </LDevice>\n";
        }
        out << "      </Server>\n    </AccessPoint>\n  </IED>\n";
    }
}

} // namespace exporter_detail

class SclExporter final {
public:
    [[nodiscard]] static SclCanonicalExportResult validate(
        const SclDocument& document,
        const SclCanonicalExportOptions& options) {
        SclCanonicalExportResult result;
        result.success = exporter_detail::validate_model(document, options, result.error);
        return result;
    }

    [[nodiscard]] static SclCanonicalExportResult canonical(
        const SclDocument& document,
        const SclCanonicalExportOptions& options) {
        using namespace exporter_detail;
        SclCanonicalExportResult result;
        if (!validate_model(document, options, result.error)) return result;

        const auto models = build_ln_models(document);
        std::ostringstream out;
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<SCL"
            << edition_attributes(options.edition) << ">\n";
        out << "  <Header id=\"" << xml_escape(document.header_id.empty() ? "ARSTACK_CANONICAL" : document.header_id)
            << "\"";
        if (!document.header_version.empty()) out << " version=\"" << xml_escape(document.header_version) << "\"";
        if (!document.header_revision.empty()) out << " revision=\"" << xml_escape(document.header_revision) << "\"";
        out << " />\n";
        write_communication(out, document);
        write_ieds(out, document);
        write_data_type_templates(out, document, models);
        out << "</SCL>\n";

        result.xml = out.str();
        result.success = true;
        result.warnings.push_back(
            "Canonical " + profile_name(options.profile) +
            " export preserves the modeled ARStack semantic surface, not unknown vendor extension XML or unmodeled Services/Substation content.");
        return result;
    }
};

} // namespace ar::iec61850::scl
