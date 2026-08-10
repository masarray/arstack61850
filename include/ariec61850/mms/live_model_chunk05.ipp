        << ",\"logicalNodeCount\":" << coverage.logical_node_count
        << ",\"dataObjectCount\":" << coverage.data_object_count
        << ",\"dataAttributeCount\":" << coverage.data_attribute_count
        << ",\"dataSetCount\":" << coverage.data_set_count
        << ",\"reportControlCount\":" << coverage.report_control_count
        << ",\"reportControlBoundCount\":" << coverage.report_control_bound_count
        << ",\"reportControlUnboundCount\":" << coverage.report_control_unbound_count
        << ",\"reportControlBindingNotReadCount\":" << coverage.report_control_binding_not_read_count
        << ",\"reportControlBindingReadFailedCount\":" << coverage.report_control_binding_read_failed_count
        << ",\"gooseControlBlockCount\":" << coverage.goose_control_block_count
        << ",\"sampledValueControlBlockCount\":" << coverage.sampled_value_control_block_count
        << ",\"settingGroupControlCount\":" << coverage.setting_group_control_count
        << ",\"logControlCount\":" << coverage.log_control_count
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
                << json(ln.instance) << "\",\"proposedLnTypeId\":\""
                << json(ln.proposed_type_id) << "\",\"dataObjects\":[";
            for (std::size_t k = 0; k < ln.data_objects.size(); ++k) {
                if (k) out << ',';
                const auto& object = ln.data_objects[k];
                out << "{\"reference\":\"" << json(object.reference) << "\",\"name\":\""
                    << json(object.name) << "\",\"proposedDoTypeId\":\""
                    << json(object.proposed_do_type_id) << "\",\"inferredCdc\":\""
                    << json(object.inferred_cdc)
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
        const auto& data_set = data_sets[i];
        out << "{\"reference\":\"" << json(data_set.reference)
            << "\",\"domain\":\"" << json(data_set.domain)
            << "\",\"logicalNode\":\"" << json(data_set.logical_node)
            << "\",\"name\":\"" << json(data_set.name)
            << "\",\"memberCount\":" << data_set.members.size() << ",\"members\":[";
        for (std::size_t j = 0; j < data_set.members.size(); ++j) {
            if (j) out << ',';
            out << "{\"index\":" << j << ",\"reference\":\""
                << json(data_set.members[j].reference) << "\",\"functionalConstraint\":\""
                << json(data_set.members[j].functional_constraint) << "\"}";
        }
        const auto write_reference_array = [&out](const auto& references) {
            for (std::size_t index = 0U; index < references.size(); ++index) {
                if (index) out << ',';
                out << '"' << json(references[index]) << '"';
            }
        };
        out << "],\"usedByReportControls\":[";
        write_reference_array(data_set.used_by_report_controls);
        out << "],\"usedByGooseControls\":[";
        write_reference_array(data_set.used_by_goose_controls);
        out << "],\"usedBySampledValueControls\":[";
        write_reference_array(data_set.used_by_sampled_value_controls);
        out << "]}";
    }
    out << "],\"reportControls\":[";
    for (std::size_t i = 0; i < report_controls.size(); ++i) {
        if (i) out << ',';
        const auto& r = report_controls[i];
        out << "{\"reference\":\"" << json(r.reference)
            << "\",\"domain\":\"" << json(r.domain)
            << "\",\"logicalNode\":\"" << json(r.logical_node)
            << "\",\"name\":\"" << json(r.name)
            << "\",\"buffered\":" << (r.buffered ? "true" : "false")
            << ",\"dataSetReference\":\"" << json(r.data_set_reference)
            << "\",\"dataSetBindingStatus\":\"" << json(r.data_set_binding_status)
            << "\",\"dataSetBindingMessage\":\"" << json(r.data_set_binding_message)
            << "\",\"reportId\":\"" << json(r.report_id)
            << "\",\"confRev\":\"" << json(r.configuration_revision)
            << "\",\"enabledState\":\"" << json(r.enabled_state)
            << "\",\"reservationState\":\"" << json(r.reservation_state)
            << "\",\"reservationTimeSeconds\":\"" << json(r.reservation_time_seconds)
            << "\",\"status\":\"" << json(r.status) << "\"}";
    }
    out << ']';

    const auto write_control_blocks = [&out](
        const std::string_view property,
        const auto& controls) {
        out << ",\"" << property << "\":[";
        for (std::size_t index = 0U; index < controls.size(); ++index) {
            if (index) out << ',';
            const auto& control = controls[index];
            out << "{\"kind\":\"" << json(control.kind)
                << "\",\"reference\":\"" << json(control.reference)
                << "\",\"domain\":\"" << json(control.domain)
                << "\",\"logicalNode\":\"" << json(control.logical_node)
                << "\",\"name\":\"" << json(control.name)
                << "\",\"functionalConstraint\":\"" << json(control.functional_constraint)
                << "\",\"attributeCount\":" << control.attributes.size()
                << ",\"attributes\":[";
            for (std::size_t attribute_index = 0U;
                 attribute_index < control.attributes.size();
                 ++attribute_index) {
                if (attribute_index) out << ',';
                out << '"' << json(control.attributes[attribute_index]) << '"';
            }
            out << "],\"dataSetReference\":\"" << json(control.data_set_reference)
                << "\",\"dataSetReferenceStatus\":\"" << json(control.data_set_reference_status)
                << "\",\"controlId\":\"" << json(control.control_id)
                << "\",\"appId\":\"" << json(control.app_id)
                << "\",\"smvId\":\"" << json(control.smv_id)
                << "\",\"confRev\":\"" << json(control.configuration_revision)
                << "\",\"minimumTimeMs\":\"" << json(control.minimum_time_ms)
                << "\",\"maximumTimeMs\":\"" << json(control.maximum_time_ms)
                << "\",\"sampleRate\":\"" << json(control.sample_rate)
                << "\",\"sampleMode\":\"" << json(control.sample_mode)
                << "\",\"numberOfAsdu\":\"" << json(control.number_of_asdu)
                << "\",\"addressStatus\":\"" << json(control.address_status)
                << "\",\"discoveryStatus\":\"" << json(control.discovery_status)
                << "\",\"runtimeAttributes\":[";
            for (std::size_t runtime_index = 0U;
                 runtime_index < control.runtime_attributes.size();
                 ++runtime_index) {
                if (runtime_index) out << ',';
                const auto& runtime = control.runtime_attributes[runtime_index];
                out << "{\"attributePath\":\"" << json(runtime.attribute_path)
                    << "\",\"mmsReference\":\"" << json(runtime.mms_reference)
                    << "\",\"value\":\"" << json(runtime.value)
                    << "\",\"status\":\"" << json(runtime.status)
                    << "\",\"failureCode\":";
                if (runtime.failure_code) out << *runtime.failure_code;
                else out << "null";
                out << '}';
            }
            out << "],\"message\":\"" << json(control.message) << "\"}";
        }
        out << ']';
    };
    write_control_blocks("gooseControlBlocks", goose_control_blocks);
    write_control_blocks("sampledValueControlBlocks", sampled_value_control_blocks);
    write_control_blocks("settingGroupControls", setting_group_controls);
    write_control_blocks("logControls", log_controls);

    out << ",\"typeTemplates\":[";
    for (std::size_t index = 0U; index < type_templates.size(); ++index) {
        if (index) out << ',';
        const auto& type_template = type_templates[index];
        out << "{\"templateKind\":\"" << json(type_template.template_kind)
            << "\",\"id\":\"" << json(type_template.id)
            << "\",\"sourceReference\":\"" << json(type_template.source_reference)
            << "\",\"inferredType\":\"" << json(type_template.inferred_type)
            << "\",\"confidence\":" << type_template.confidence
            << ",\"members\":[";
        for (std::size_t member_index = 0U;
             member_index < type_template.members.size();
             ++member_index) {
            if (member_index) out << ',';
            out << '"' << json(type_template.members[member_index]) << '"';
        }
        out << "]}";
    }

    out << "],\"variableTypeDiscoveries\":[";
    for (std::size_t index = 0U; index < variable_type_discoveries.size(); ++index) {
        if (index) out << ',';
        const auto& variable_type = variable_type_discoveries[index];
        out << "{\"reference\":\"" << json(variable_type.reference)
            << "\",\"domain\":\"" << json(variable_type.domain)
            << "\",\"mmsItemName\":\"" << json(variable_type.mms_item_name)
            << "\",\"functionalConstraint\":\"" << json(variable_type.functional_constraint)
            << "\",\"isSuccess\":" << (variable_type.success ? "true" : "false")
            << ",\"mmsType\":\"" << json(variable_type.mms_type)
            << "\",\"sclBType\":\"" << json(variable_type.scl_basic_type)
            << "\",\"typeSignature\":\"" << json(variable_type.type_signature)
            << "\",\"isMmsDeletable\":";
        if (variable_type.mms_deletable.has_value()) {
            out << (*variable_type.mms_deletable ? "true" : "false");
        } else {
            out << "null";
        }
        out << ",\"message\":\"" << json(variable_type.message)
            << "\",\"source\":\"" << json(variable_type.source) << "\"}";
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