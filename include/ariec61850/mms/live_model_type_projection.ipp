// SPDX-License-Identifier: GPL-3.0-or-later

    // Pure projection port of ARIEC61850 LiveIedModelDiscoveryBuilder:
    // ProposedLnTypeId / ProposedDoTypeId, BuildTypeTemplates and
    // BuildVariableTypeDiscoveries.  This consumes evidence already present in
    // MmsLiveDiscoveryResult and never causes additional MMS traffic.
    const auto safe_id_part = [](const std::string_view raw) {
        auto value = trim(std::string{raw});
        if (value.empty()) {
            return std::string{"X"};
        }
        std::transform(value.begin(), value.end(), value.begin(), [](const char character) {
            return std::isalnum(static_cast<unsigned char>(character)) != 0
                ? character
                : '_';
        });
        return value;
    };

    for (auto& ld : document.logical_devices) {
        for (auto& ln : ld.logical_nodes) {
            const auto ln_class = ln.logical_node_class.empty()
                ? ln.name
                : ln.logical_node_class;
            ln.proposed_type_id =
                "LN_" + safe_id_part(ln_class) + "_" + safe_id_part(ln.name);

            MmsLiveTypeTemplateCandidate ln_template;
            ln_template.template_kind = "LNodeType";
            ln_template.id = ln.proposed_type_id;
            ln_template.source_reference = ln.name;
            ln_template.inferred_type = ln_class;
            ln_template.confidence = 1.0;
            for (const auto& object : ln.data_objects) {
                ln_template.members.push_back(object.name);
            }
            document.type_templates.push_back(std::move(ln_template));

            for (auto& object : ln.data_objects) {
                object.proposed_do_type_id =
                    "DO_" + safe_id_part(object.inferred_cdc) + "_" +
                    safe_id_part(ln_class) + "_" + safe_id_part(object.name);

                if (!options.include_low_confidence_templates &&
                    (object.confidence == MmsLiveModelConfidence::low ||
                     object.confidence == MmsLiveModelConfidence::unknown)) {
                    continue;
                }

                MmsLiveTypeTemplateCandidate do_template;
                do_template.template_kind = "DOType";
                do_template.id = object.proposed_do_type_id;
                do_template.source_reference = object.reference;
                do_template.inferred_type = object.inferred_cdc;
                do_template.confidence = object.cdc_confidence;
                for (const auto& attribute : object.attributes) {
                    auto type = attribute.scl_basic_type;
                    if (!attribute.mms_type.empty()) {
                        type += "/" + attribute.mms_type;
                    }
                    const auto source =
                        attribute.type_confidence == MmsLiveModelConfidence::exact
                            ? "exact"
                            : "heuristic";
                    do_template.members.push_back(
                        attribute.attribute_path + " [" + attribute.functional_constraint +
                        "] " + type + " (" + source + ")");
                }
                document.type_templates.push_back(std::move(do_template));
            }
        }
    }

    std::vector<const MmsVariableTypeEvidence*> ordered_type_evidence;
    ordered_type_evidence.reserve(discovery.variable_types.size());
    for (const auto& evidence : discovery.variable_types) {
        ordered_type_evidence.push_back(&evidence);
    }
    std::sort(
        ordered_type_evidence.begin(),
        ordered_type_evidence.end(),
        [](const auto* left, const auto* right) {
            return lower(left->variable.reference()) < lower(right->variable.reference());
        });

    for (const auto* evidence : ordered_type_evidence) {
        MmsLiveVariableTypeDiscovery projected;
        projected.reference = evidence->variable.reference();
        projected.domain = evidence->variable.domain;
        projected.mms_item_name = evidence->variable.item;
        projected.success = evidence->success();
        projected.message = evidence->error;

        if (const auto point = MmsLiveReferenceParser::parse_variable(
                evidence->variable.domain,
                evidence->variable.item,
                "GetVariableAccessAttributes",
                100U)) {
            projected.functional_constraint = point->functional_constraint;
        }

        if (evidence->attributes) {
            projected.mms_type = evidence->attributes->type.mms_type_name();
            projected.scl_basic_type = evidence->attributes->type.scl_basic_type();
            projected.type_signature = evidence->attributes->type.signature();
            projected.mms_deletable = evidence->attributes->mms_deletable;
        }

        document.variable_type_discoveries.push_back(std::move(projected));
    }
