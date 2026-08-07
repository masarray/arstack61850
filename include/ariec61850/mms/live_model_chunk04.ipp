    auto& coverage = document.coverage;
    coverage.logical_device_count = document.logical_devices.size();
    for (const auto& ld : document.logical_devices) {
        coverage.logical_node_count += ld.logical_nodes.size();
        for (const auto& ln : ld.logical_nodes) {
            coverage.data_object_count += ln.data_objects.size();
            for (const auto& object : ln.data_objects) {
                switch (object.confidence) {
                case MmsLiveModelConfidence::high:
                    ++coverage.high_confidence_cdc_count;
                    break;
                case MmsLiveModelConfidence::medium:
                    ++coverage.medium_confidence_cdc_count;
                    break;
                case MmsLiveModelConfidence::low:
                    ++coverage.low_confidence_cdc_count;
                    break;
                case MmsLiveModelConfidence::exact:
                    ++coverage.high_confidence_cdc_count;
                    break;
                case MmsLiveModelConfidence::unknown:
                    ++coverage.unknown_cdc_count;
                    break;
                }
                coverage.data_attribute_count += object.attributes.size();
                coverage.exact_functional_constraint_count +=
                    static_cast<std::size_t>(std::count_if(
                        object.attributes.begin(), object.attributes.end(), [](const auto& attribute) {
                            return attribute.functional_constraint_confidence ==
                                MmsLiveModelConfidence::exact;
                        }));
                coverage.exact_mms_type_count += static_cast<std::size_t>(std::count_if(
                    object.attributes.begin(), object.attributes.end(), [](const auto& attribute) {
                        return attribute.type_confidence == MmsLiveModelConfidence::exact;
                    }));
            }
        }
    }
    coverage.data_set_count = document.data_sets.size();
    coverage.report_control_count = document.report_controls.size();
    coverage.buffered_report_control_count = static_cast<std::size_t>(std::count_if(
        document.report_controls.begin(), document.report_controls.end(),
        [](const auto& report_control) { return report_control.buffered; }));
    coverage.unbuffered_report_control_count =
        coverage.report_control_count - coverage.buffered_report_control_count;
    coverage.report_control_bound_count = static_cast<std::size_t>(std::count_if(
        document.report_controls.begin(), document.report_controls.end(),
        [](const auto& report_control) {
            return same(report_control.data_set_binding_status, "Bound");
        }));
    coverage.report_control_unbound_count = static_cast<std::size_t>(std::count_if(
        document.report_controls.begin(), document.report_controls.end(),
        [](const auto& report_control) {
            return same(report_control.data_set_binding_status, "Unbound");
        }));
    coverage.report_control_binding_read_failed_count = static_cast<std::size_t>(std::count_if(
        document.report_controls.begin(), document.report_controls.end(),
        [](const auto& report_control) {
            return same(report_control.data_set_binding_status, "ReadFailed");
        }));
    coverage.report_control_binding_not_read_count = static_cast<std::size_t>(std::count_if(
        document.report_controls.begin(), document.report_controls.end(),
        [](const auto& report_control) {
            return same(report_control.data_set_binding_status, "NotRead");
        }));
    coverage.goose_control_block_count = document.goose_control_blocks.size();
    coverage.sampled_value_control_block_count =
        document.sampled_value_control_blocks.size();
    coverage.setting_group_control_count = document.setting_group_controls.size();
    coverage.log_control_count = document.log_controls.size();
    coverage.variable_type_read_attempt_count = discovery.variable_types.size();
    coverage.variable_type_read_success_count = static_cast<std::size_t>(std::count_if(
        discovery.variable_types.begin(), discovery.variable_types.end(),
        [](const auto& evidence) { return evidence.success(); }));
    coverage.variable_type_read_failure_count =
        coverage.variable_type_read_attempt_count - coverage.variable_type_read_success_count;

    for (const auto& diagnostic : discovery.diagnostics) {
        document.warnings.push_back({"DiscoveryDiagnostic", {}, diagnostic});
    }
    if (coverage.variable_type_read_failure_count != 0U) {
        document.warnings.push_back({
            "VariableTypeReadFailure",
            {},
            std::to_string(coverage.variable_type_read_failure_count) +
                " type probes failed."});
    }
    for (const auto& data_set : document.data_sets) {
        if (data_set.members.empty()) {
            document.warnings.push_back({
                "DATASET_MEMBERS_NOT_READ",
                data_set.reference,
                "DataSet exists but member directory was not read in this run."});
        }
    }
    if (coverage.goose_control_block_count +
            coverage.sampled_value_control_block_count +
            coverage.setting_group_control_count +
            coverage.log_control_count != 0U) {
        document.warnings.push_back({
            "CONTROL_BLOCK_VALUE_READ_PENDING",
            {},
            "GO/SV/SG/LG control blocks were inventoried from live FC attribute names. "
            "Attribute values such as DatSet, GoID, svID, APPID, and multicast address "
            "are not read yet and remain companion evidence until the deep value reader is implemented."});
    }

    // Do not warn merely because an RCB is Unbound. ARIEC61850 supports a
    // DynamicDataSet selection mode in which an empty DatSet is an intentional
    // runtime slot state. Selection/claim safety is evaluated separately from
    // the structural live model.

    document.summary =
        "Live IED model: LD=" + std::to_string(coverage.logical_device_count) +
        ", LN=" + std::to_string(coverage.logical_node_count) +
        ", DO=" + std::to_string(coverage.data_object_count) +
        ", DA=" + std::to_string(coverage.data_attribute_count) +
        ", DataSets=" + std::to_string(coverage.data_set_count) +
        ", RCB=" + std::to_string(coverage.report_control_count) +
        ", GoCB=" + std::to_string(coverage.goose_control_block_count) +
        ", SVCB=" + std::to_string(coverage.sampled_value_control_block_count) +
        ", SGCB=" + std::to_string(coverage.setting_group_control_count) +
        ", LCB=" + std::to_string(coverage.log_control_count) + ".";
    return document;
}

inline std::string MmsLiveModelDocument::canonical_manifest() const {
    std::ostringstream out;
    out << "ARIEC61850-LIVE-MODEL|1\nIDENTITY|" << identity.ied_name << '\n';
    for (const auto& ld : logical_devices) for (const auto& ln : ld.logical_nodes)
        for (const auto& object : ln.data_objects) for (const auto& attribute : object.attributes)
            out << "DA|" << attribute.object_reference << '|' << attribute.functional_constraint
                << '|' << attribute.mms_type_signature << '\n';
    for (const auto& data_set : data_sets)
        out << "DS|" << data_set.reference << '|' << data_set.members.size() << '\n';
    for (const auto& control : report_controls)
        out << "RCB|" << control.reference << '|' << (control.buffered ? "B" : "U") << '\n';
    const auto append_controls = [&out](const auto& controls) {
        for (const auto& control : controls) {
            out << "CB|" << control.kind << '|' << control.reference << '|'
                << control.attributes.size() << '\n';
        }
    };
    append_controls(goose_control_blocks);
    append_controls(sampled_value_control_blocks);
    append_controls(setting_group_controls);
    append_controls(log_controls);
    return out.str();
}
inline std::uint64_t MmsLiveModelDocument::canonical_fingerprint() const {
    std::uint64_t value{14695981039346656037ULL};
    for (const char raw_byte : canonical_manifest()) {
        const auto byte = static_cast<unsigned char>(raw_byte);
        value ^= byte;
        value *= 1099511628211ULL;
    }
    return value;
}
inline std::string MmsLiveModelDocument::canonical_fingerprint_hex() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << canonical_fingerprint();
    return out.str();
}
inline std::string MmsLiveModelDocument::to_json() const {
    using namespace live_model_detail;
    std::ostringstream out;
    out << '{' << "\"schemaVersion\":\"" << json(schema_version) << "\","
        << "\"source\":\"" << json(source) << "\","
        << "\"host\":\"" << json(endpoint.host) << "\",\"port\":" << endpoint.port << ','
        << "\"iedName\":\"" << json(identity.ied_name) << "\","
        << "\"iedIdentity\":{\"iedName\":\"" << json(identity.ied_name)
        << "\",\"source\":\"" << json(identity.source) << "\",\"confidence\":\""
        << confidence(identity.confidence) << "\",\"isAmbiguous\":"
        << (identity.ambiguous ? "true" : "false") << ",\"candidateNames\":[";
    for (std::size_t index = 0U; index < identity.candidate_names.size(); ++index) {
        if (index) out << ',';
        out << '"' << json(identity.candidate_names[index]) << '"';
    }
    out << "],\"logicalDeviceAliases\":{";
    std::size_t alias_index = 0U;
    for (const auto& [domain, alias] : identity.logical_device_aliases) {
        if (alias_index++) out << ',';
        out << '"' << json(domain) << "\":\"" << json(alias) << '"';
    }
    out << "},\"evidence\":[";
    for (std::size_t index = 0U; index < identity.evidence.size(); ++index) {
        if (index) out << ',';
        out << '"' << json(identity.evidence[index]) << '"';
    }
    out << "]},"
        << "\"accessPointName\":\"" << json(access_point_name) << "\","
        << "\"summary\":\"" << json(summary) << "\","
        << "\"fingerprint\":\"" << canonical_fingerprint_hex() << "\","
        << "\"coverage\":{\"logicalDeviceCount\":" << coverage.logical_device_count
