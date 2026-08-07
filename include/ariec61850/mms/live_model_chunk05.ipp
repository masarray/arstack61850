        << ",\"logicalNodeCount\":" << coverage.logical_node_count
        << ",\"dataObjectCount\":" << coverage.data_object_count
        << ",\"dataAttributeCount\":" << coverage.data_attribute_count
        << ",\"dataSetCount\":" << coverage.data_set_count
        << ",\"reportControlCount\":" << coverage.report_control_count
        << ",\"variableTypeReadAttemptCount\":" << coverage.variable_type_read_attempt_count
        << ",\"variableTypeReadSuccessCount\":" << coverage.variable_type_read_success_count
        << ",\"variableTypeReadFailureCount\":" << coverage.variable_type_read_failure_count
        << ",\"exactMmsTypeCount\":" << coverage.exact_mms_type_count << "},\"logicalDevices\":[";
    for (std::size_t i = 0; i < logical_devices.size(); ++i) {
        if (i) out << ',';
        const auto& ld = logical_devices[i];
        out << "{\"mmsDomain\":\"" << json(ld.mms_domain) << "\",\"inst\":\""
            << json(ld.instance) << "\",\"logicalNodes\":[";
        for (std::size_t j = 0; j < ld.logical_nodes.size(); ++j) {
            if (j) out << ',';
            const auto& ln = ld.logical_nodes[j];
            out << "{\"name\":\"" << json(ln.name) << "\",\"prefix\":\"" << json(ln.prefix)
                << "\",\"lnClass\":\"" << json(ln.logical_node_class) << "\",\"lnInst\":\""
                << json(ln.instance) << "\",\"dataObjects\":[";
            for (std::size_t k = 0; k < ln.data_objects.size(); ++k) {
                if (k) out << ',';
                const auto& object = ln.data_objects[k];
                out << "{\"reference\":\"" << json(object.reference) << "\",\"name\":\""
                    << json(object.name) << "\",\"inferredCdc\":\"" << json(object.inferred_cdc)
                    << "\",\"cdcConfidence\":" << object.cdc_confidence << ",\"attributes\":[";
                for (std::size_t m = 0; m < object.attributes.size(); ++m) {
                    if (m) out << ',';
                    const auto& a = object.attributes[m];
                    out << "{\"objectReference\":\"" << json(a.object_reference)
                        << "\",\"attributePath\":\"" << json(a.attribute_path)
                        << "\",\"functionalConstraint\":\"" << json(a.functional_constraint)
                        << "\",\"mmsReference\":\"" << json(a.mms_reference)
                        << "\",\"sclBType\":\"" << json(a.scl_basic_type)
                        << "\",\"mmsType\":\"" << json(a.mms_type)
                        << "\",\"mmsTypeSignature\":\"" << json(a.mms_type_signature) << "\"}";
                }
                out << "]}";
            }
            out << "]}";
        }
        out << "]}";
    }
    out << "],\"dataSets\":[";
    for (std::size_t i = 0; i < data_sets.size(); ++i) {
        if (i) out << ',';
        out << "{\"reference\":\"" << json(data_sets[i].reference) << "\",\"memberCount\":"
            << data_sets[i].members.size() << ",\"members\":[";
        for (std::size_t j = 0; j < data_sets[i].members.size(); ++j) {
            if (j) out << ',';
            out << "{\"index\":" << j << ",\"reference\":\""
                << json(data_sets[i].members[j].reference) << "\",\"functionalConstraint\":\""
                << json(data_sets[i].members[j].functional_constraint) << "\"}";
        }
        out << "]}";
    }
    out << "],\"reportControls\":[";
    for (std::size_t i = 0; i < report_controls.size(); ++i) {
        if (i) out << ',';
        const auto& r = report_controls[i];
        out << "{\"reference\":\"" << json(r.reference) << "\",\"buffered\":"
            << (r.buffered ? "true" : "false") << ",\"dataSetReference\":\""
            << json(r.data_set_reference) << "\",\"reportId\":\"" << json(r.report_id) << "\"}";
    }
    out << "],\"warnings\":[";
    for (std::size_t i = 0; i < warnings.size(); ++i) {
        if (i) out << ',';
        out << "{\"code\":\"" << json(warnings[i].code) << "\",\"reference\":\""
            << json(warnings[i].reference) << "\",\"message\":\"" << json(warnings[i].message) << "\"}";
    }
    out << "]}";
    return out.str();
}

inline std::size_t MmsLiveModelParityResult::blocking_finding_count() const noexcept {
    return static_cast<std::size_t>(std::count_if(findings.begin(), findings.end(), [](const auto& item) {
        return item.severity == MmsLiveModelFindingSeverity::error;
    }));
}
inline MmsLiveModelParityResult MmsLiveModelParityComparer::compare(
    const MmsLiveModelDocument& expected,
    const MmsLiveModelDocument& observed) {
    using namespace live_model_detail;
    MmsLiveModelParityResult result;
    result.expected_ied_name = expected.identity.ied_name;
    result.observed_ied_name = observed.identity.ied_name;
    const auto expected_attributes = attributes(expected);
    const auto observed_attributes = attributes(observed);
    result.expected_attribute_count = expected_attributes.size();
    result.observed_attribute_count = observed_attributes.size();
