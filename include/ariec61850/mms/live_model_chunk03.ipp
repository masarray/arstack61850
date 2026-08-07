        MmsLiveDataAttribute attribute;
        attribute.object_reference = point.user_reference();
        attribute.attribute_path = MmsLiveReferenceParser::data_attribute_path(point.data_object_path);
        attribute.functional_constraint = point.functional_constraint;
        attribute.mms_reference = point.mms_reference();
        attribute.mms_item_name = point.mms_item_name;
        attribute.source = point.source;
        std::string type_source;
        if (const auto* type = resolve_type(point, discovery.variable_types, type_source)) {
            attribute.scl_basic_type = type->scl_basic_type();
            attribute.mms_type = type->mms_type_name();
            attribute.mms_type_signature = type->signature();
            attribute.type_discovery_status = "Exact";
            attribute.type_source = type_source;
            attribute.type_confidence = MmsLiveModelConfidence::exact;
        } else {
            attribute.scl_basic_type = guess_scl_basic_type(
                attribute.attribute_path,
                attribute.functional_constraint);
            attribute.type_source = "NameListHeuristic";
            attribute.type_confidence = MmsLiveModelConfidence::low;
        }
        hierarchy[point.domain][point.logical_node][object_name].emplace(
            key(attribute.object_reference), std::move(attribute));
    }
    for (auto& [domain, nodes] : hierarchy) {
        MmsLiveLogicalDevice ld;
        ld.mms_domain = domain;
        const auto alias = document.identity.logical_device_aliases.find(domain);
        ld.instance = alias == document.identity.logical_device_aliases.end() ? domain : alias->second;
        for (auto& [node_name, objects] : nodes) {
            const auto parsed = MmsLiveReferenceParser::parse_logical_node_name(node_name);
            MmsLiveLogicalNode ln;
            ln.name = parsed.name;
            ln.prefix = parsed.prefix;
            ln.logical_node_class = parsed.logical_node_class;
            ln.instance = parsed.instance;
            ln.proposed_type_id = document.identity.ied_name + "_" + node_name;
            for (auto& [object_name, object_attributes] : objects) {
                MmsLiveDataObject object;
                object.name = object_name;
                object.reference = domain + "/" + node_name + "." + object_name;
                for (auto& [_, attribute] : object_attributes) {
                    ++ln.functional_constraint_counts[attribute.functional_constraint];
                    object.attributes.push_back(std::move(attribute));
                }
                const auto [cdc, score] = infer_cdc(ln.logical_node_class, object.name, object.attributes);
                object.inferred_cdc = cdc;
                object.cdc_confidence = score;
                object.confidence = score >= 0.85 ? MmsLiveModelConfidence::high
                    : score >= 0.60 ? MmsLiveModelConfidence::medium
                    : score > 0 ? MmsLiveModelConfidence::low
                    : MmsLiveModelConfidence::unknown;
                ln.data_objects.push_back(std::move(object));
            }
            ld.logical_nodes.push_back(std::move(ln));
        }
        document.logical_devices.push_back(std::move(ld));
    }

    // Match the C# builder: the inventory itself is valuable evidence.  A DataSet
    // remains present in the model even when GetNamedVariableListAttributes was
    // intentionally skipped or failed; only its member list remains unavailable.
    for (const auto& candidate : discovery.report_inventory.data_sets) {
        MmsLiveDataSet data_set;
        data_set.reference = candidate.reference;
        data_set.domain = candidate.domain;
        data_set.logical_node = candidate.logical_node;
        data_set.name = candidate.name;

        const auto evidence = std::find_if(
            discovery.data_set_directories.begin(),
            discovery.data_set_directories.end(),
            [&candidate](const auto& item) {
                return same(item.candidate.reference, candidate.reference);
            });
        if (evidence != discovery.data_set_directories.end() && evidence->success()) {
            data_set.deletable = evidence->directory->deletable;
            for (std::size_t index = 0U;
                 index < evidence->directory->members.size();
                 ++index) {
                const auto& member = evidence->directory->members[index];
                data_set.members.push_back({
                    index,
                    member.user_reference,
                    member.functional_constraint,
                    member.mms_reference,
                    member.confidence >= 100U
                        ? MmsLiveModelConfidence::exact
                        : MmsLiveModelConfidence::medium});
            }
        }
        document.data_sets.push_back(std::move(data_set));
    }

    // Likewise, preserve every BRCB/URCB discovered from the RP/BR name inventory.
    // A successful read enriches it; an unprobed RCB remains status=Discovered.
    for (const auto& candidate : discovery.report_inventory.report_controls) {
        MmsLiveReportControl control;
        control.reference = candidate.reference;
        control.domain = candidate.domain;
        control.logical_node = candidate.logical_node;
        control.name = candidate.name;
        control.buffered = candidate.buffered;
        control.status = "Discovered";

        const auto evidence = std::find_if(
            discovery.report_controls.begin(),
            discovery.report_controls.end(),
            [&candidate](const auto& item) {
                return same(item.candidate.reference, candidate.reference);
            });
        if (evidence != discovery.report_controls.end()) {
            control.status = evidence->success() ? "Read" : "Unreadable";
            if (evidence->state) {
                control.data_set_reference = evidence->state->data_set_reference;
                std::replace(
                    control.data_set_reference.begin(),
                    control.data_set_reference.end(), '$', '.');
                control.report_id = evidence->state->report_id;
                control.configuration_revision = optional_u64(
                    evidence->state->configuration_revision);
                control.trigger_options = bit_names(evidence->state->trigger_options);
                control.optional_fields = bit_names(evidence->state->optional_fields);
                control.buffer_time_ms = optional_u64(evidence->state->buffer_time_ms);
                control.integrity_period_ms = optional_u64(
                    evidence->state->integrity_period_ms);
                control.enabled_state = optional_bool(evidence->state->report_enabled);
                control.reservation_state = optional_bool(evidence->state->reserved);
                control.reservation_time_seconds = optional_u64(
                    evidence->state->reservation_time_seconds);
            }
        }
        document.report_controls.push_back(std::move(control));
    }

    for (auto& data_set : document.data_sets) {
        for (const auto& control : document.report_controls) {
            if (!control.data_set_reference.empty() &&
                same(control.data_set_reference, data_set.reference)) {
                data_set.used_by_report_controls.push_back(control.reference);
            }
        }
        std::sort(
            data_set.used_by_report_controls.begin(),
            data_set.used_by_report_controls.end(),
            [](const auto& left, const auto& right) {
                return lower(left) < lower(right);
            });
    }
