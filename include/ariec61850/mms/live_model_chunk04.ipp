            control.reservation_state = optional_bool(evidence.state->reserved);
            control.reservation_time_seconds = optional_u64(evidence.state->reservation_time_seconds);
        }
        document.report_controls.push_back(std::move(control));
    }

    auto& coverage = document.coverage;
    coverage.logical_device_count = document.logical_devices.size();
    for (const auto& ld : document.logical_devices) {
        coverage.logical_node_count += ld.logical_nodes.size();
        for (const auto& ln : ld.logical_nodes) {
            coverage.data_object_count += ln.data_objects.size();
            for (const auto& object : ln.data_objects) {
                coverage.data_attribute_count += object.attributes.size();
                coverage.exact_mms_type_count += static_cast<std::size_t>(std::count_if(
                    object.attributes.begin(), object.attributes.end(), [](const auto& a) {
                        return a.type_confidence == MmsLiveModelConfidence::exact;
                    }));
            }
        }
    }
    coverage.data_set_count = document.data_sets.size();
    coverage.report_control_count = document.report_controls.size();
    coverage.buffered_report_control_count = static_cast<std::size_t>(std::count_if(
        document.report_controls.begin(), document.report_controls.end(), [](const auto& r) { return r.buffered; }));
    coverage.unbuffered_report_control_count = coverage.report_control_count - coverage.buffered_report_control_count;
    coverage.variable_type_read_attempt_count = discovery.variable_types.size();
    coverage.variable_type_read_success_count = static_cast<std::size_t>(std::count_if(
        discovery.variable_types.begin(), discovery.variable_types.end(), [](const auto& e) { return e.success(); }));
    coverage.variable_type_read_failure_count = coverage.variable_type_read_attempt_count - coverage.variable_type_read_success_count;
    for (const auto& diagnostic : discovery.diagnostics)
        document.warnings.push_back({"DiscoveryDiagnostic", {}, diagnostic});
    if (coverage.variable_type_read_failure_count)
        document.warnings.push_back({"VariableTypeReadFailure", {},
            std::to_string(coverage.variable_type_read_failure_count) + " type probes failed."});
    document.summary = "Live MMS model: LD=" + std::to_string(coverage.logical_device_count) +
        ", LN=" + std::to_string(coverage.logical_node_count) +
        ", DA=" + std::to_string(coverage.data_attribute_count) +
        ", DataSet=" + std::to_string(coverage.data_set_count) +
        ", RCB=" + std::to_string(coverage.report_control_count) + ".";
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
        << (identity.ambiguous ? "true" : "false") << "},"
        << "\"accessPointName\":\"" << json(access_point_name) << "\","
        << "\"summary\":\"" << json(summary) << "\","
        << "\"fingerprint\":\"" << canonical_fingerprint_hex() << "\","
        << "\"coverage\":{\"logicalDeviceCount\":" << coverage.logical_device_count
