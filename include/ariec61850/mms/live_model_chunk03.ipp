        auto& object_attributes =
            hierarchy[point.domain][point.logical_node][object_name];
        const auto attribute_path =
            MmsLiveReferenceParser::data_attribute_path(point.data_object_path);
        // Match ARIEC61850: a top-level FC point proves the DataObject exists,
        // but it is not itself a DataAttribute. Keeping the empty ObjectMap entry
        // preserves DO inventory without synthesizing one DA per DO.
        if (attribute_path.empty()) {
            continue;
        }

        MmsLiveDataAttribute attribute;
        attribute.object_reference = point.user_reference();
        attribute.attribute_path = attribute_path;
        attribute.functional_constraint = point.functional_constraint;
        attribute.mms_reference = point.mms_reference();
        attribute.mms_item_name = point.mms_item_name;
        attribute.source = point.source;
        std::string type_source;
        if (const auto* type = resolve_type(point, discovery.variable_types, type_source)) {
            attribute.scl_basic_type = type->scl_basic_type();
            attribute.mms_type = type->mms_type_name();
            // A type resolved from an LN-root probe is a child member of the
            // returned structure, so its internal name is useful while walking
            // the tree. The C# direct-variable oracle exports the selected type
            // itself, however, without that top-level member-name prefix. Clear
            // only the selected node name; nested structure member names remain.
            auto projected_type = *type;
            projected_type.name.clear();
            attribute.mms_type_signature = projected_type.signature();
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
        object_attributes.emplace(
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

    // Port of ARIEC61850 BuildControlBlockInventory.  This is deliberately
    // inventory-only: FC attribute names are enough to prove the control block
    // exists, but no enable/value/address attribute is read in this phase.
    for (const auto& ld : document.logical_devices) {
        for (const auto& ln : ld.logical_nodes) {
            for (const auto& object : ln.data_objects) {
                std::set<std::string, std::less<>> functional_constraints;
                for (const auto& attribute : object.attributes) {
                    functional_constraints.insert(attribute.functional_constraint);
                }

                for (const auto& functional_constraint : functional_constraints) {
                    std::string kind;
                    if (same(functional_constraint, "GO")) {
                        kind = "GSEControl";
                    } else if (same(functional_constraint, "MS") ||
                               same(functional_constraint, "US")) {
                        kind = "SampledValueControl";
                    } else if (same(functional_constraint, "SG") ||
                               same(functional_constraint, "SE") ||
                               (same(functional_constraint, "SP") &&
                                same(object.name, "SGCB"))) {
                        kind = "SettingGroupControl";
                    } else if (same(functional_constraint, "LG")) {
                        kind = "LogControl";
                    } else {
                        continue;
                    }

                    MmsLiveControlBlock control;
                    control.kind = kind;
                    control.reference = ld.mms_domain + "/" + ln.name + "." +
                        functional_constraint + "." + object.name;
                    control.domain = ld.mms_domain;
                    control.logical_node = ln.name;
                    control.name = object.name;
                    control.functional_constraint = functional_constraint;

                    for (const auto& attribute : object.attributes) {
                        if (same(attribute.functional_constraint, functional_constraint) &&
                            !attribute.attribute_path.empty()) {
                            if (std::none_of(
                                    control.attributes.begin(),
                                    control.attributes.end(),
                                    [&attribute](const auto& existing) {
                                        return same(existing, attribute.attribute_path);
                                    })) {
                                control.attributes.push_back(attribute.attribute_path);
                            }
                        }
                    }
                    std::sort(
                        control.attributes.begin(),
                        control.attributes.end(),
                        [](const auto& left, const auto& right) {
                            return lower(left) < lower(right);
                        });

                    const auto has_attribute_name = [&](const std::string_view name) {
                        return std::any_of(
                            control.attributes.begin(),
                            control.attributes.end(),
                            [name](const auto& attribute) {
                                return same(attribute, name);
                            });
                    };
                    const auto has_any_address_attribute =
                        has_attribute_name("DstAddress") ||
                        has_attribute_name("Addr") ||
                        has_attribute_name("APPID") ||
                        has_attribute_name("MAC-Address");
                    const auto has_enable_attribute = std::any_of(
                        control.attributes.begin(),
                        control.attributes.end(),
                        [](const auto& attribute) {
                            return ends_with_ci(attribute, "Ena");
                        });
                    const auto has_data_set_attribute = has_attribute_name("DatSet");
                    const auto has_conf_rev_attribute = has_attribute_name("ConfRev");

                    control.data_set_reference_status = has_data_set_attribute
                        ? "AttributePresentValueNotRead"
                        : "AttributeNotPresentInNameList";
                    control.address_status = has_any_address_attribute
                        ? "AddressAttributesPresentValueNotRead"
                        : "NotDiscovered";
                    control.discovery_status = "AttributeInventoryOnly";
                    control.message =
                        kind +
                        " discovered from live FC attribute names. Attribute values are not read in this phase. DatSetAttr=" +
                        (has_data_set_attribute ? "yes" : "no") +
                        ", ConfRevAttr=" + (has_conf_rev_attribute ? "yes" : "no") +
                        ", enableAttr=" + (has_enable_attribute ? "yes" : "no") + ".";

                    if (same(kind, "GSEControl")) {
                        document.goose_control_blocks.push_back(std::move(control));
                    } else if (same(kind, "SampledValueControl")) {
                        document.sampled_value_control_blocks.push_back(std::move(control));
                    } else if (same(kind, "SettingGroupControl")) {
                        document.setting_group_controls.push_back(std::move(control));
                    } else if (same(kind, "LogControl")) {
                        document.log_controls.push_back(std::move(control));
                    }
                }
            }
        }
    }

    const auto sort_controls = [](auto& controls) {
        std::sort(controls.begin(), controls.end(), [](const auto& left, const auto& right) {
            return lower(left.reference) < lower(right.reference);
        });
    };
    sort_controls(document.goose_control_blocks);
    sort_controls(document.sampled_value_control_blocks);
    sort_controls(document.setting_group_controls);
    sort_controls(document.log_controls);

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

    // Preserve every BRCB/URCB discovered from RP/BR inventory. Runtime DataSet
    // binding is modelled separately from RCB read success. This is essential for
    // dynamic-report IEDs: a successful DatSet read that returns an empty string is
    // a valid Unbound state, not a missing/corrupt DataSet.
    for (const auto& candidate : discovery.report_inventory.report_controls) {
        MmsLiveReportControl control;
        // Match the C# public model reference. The FC segment is part of the RCB
        // identity (and distinguishes RP from BR); the internal candidate remains
        // decomposed so wire requests continue to use '$' ObjectName addressing.
        control.reference = candidate.domain + "/" + candidate.logical_node + "." +
            candidate.functional_constraint + "." + candidate.name;
        control.domain = candidate.domain;
        control.logical_node = candidate.logical_node;
        control.name = candidate.name;
        control.buffered = candidate.buffered;
        control.status = "Discovered";
        control.data_set_binding_status = "NotRead";
        control.data_set_binding_message =
            "RCB DatSet binding was not read in this discovery run.";

        const auto evidence = std::find_if(
            discovery.report_controls.begin(),
            discovery.report_controls.end(),
            [&candidate](const auto& item) {
                return same(item.candidate.reference, candidate.reference);
            });
        if (evidence != discovery.report_controls.end()) {
            control.status = evidence->success() ? "Read" : "Unreadable";
            if (!evidence->success()) {
                control.data_set_binding_status = "ReadFailed";
                control.data_set_binding_message = evidence->error.empty()
                    ? "RCB read failed before DatSet binding could be established."
                    : evidence->error;
            }
            if (evidence->state) {
                const bool data_set_requested = std::any_of(
                    evidence->requested_attributes.begin(),
                    evidence->requested_attributes.end(),
                    [](const auto& attribute) { return same(attribute, "DatSet"); });
                const bool data_set_read_failed = std::any_of(
                    evidence->state->diagnostics.begin(),
                    evidence->state->diagnostics.end(),
                    [](const auto& diagnostic) {
                        return starts_with_ci(diagnostic, "DatSet read failed");
                    });

                control.data_set_reference = evidence->state->data_set_reference;
                std::replace(
                    control.data_set_reference.begin(),
                    control.data_set_reference.end(), '$', '.');

                if (!data_set_requested) {
                    control.data_set_binding_status = "NotRead";
                    control.data_set_binding_message =
                        "The RCB was probed, but DatSet was not exposed/requested for this RCB.";
                } else if (data_set_read_failed) {
                    control.data_set_binding_status = "ReadFailed";
                    control.data_set_binding_message =
                        "The RCB was read, but its DatSet attribute returned an access failure.";
                } else if (control.data_set_reference.empty()) {
                    control.data_set_binding_status = "Unbound";
                    control.data_set_binding_message =
                        "DatSet was read successfully and is empty. This is a valid unbound runtime state and may represent an available dynamic RCB slot; selection still requires reservation/ownership checks.";
                } else {
                    control.data_set_binding_status = "Bound";
                    control.data_set_binding_message =
                        "DatSet was read successfully and references a DataSet.";
                }

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
            if (same(control.data_set_binding_status, "Bound") &&
                !control.data_set_reference.empty() &&
                same(control.data_set_reference, data_set.reference)) {
                data_set.used_by_report_controls.push_back(control.reference);
            }
        }
        for (const auto& control : document.goose_control_blocks) {
            if (!control.data_set_reference.empty() &&
                same(control.data_set_reference, data_set.reference)) {
                data_set.used_by_goose_controls.push_back(control.reference);
            }
        }
        for (const auto& control : document.sampled_value_control_blocks) {
            if (!control.data_set_reference.empty() &&
                same(control.data_set_reference, data_set.reference)) {
                data_set.used_by_sampled_value_controls.push_back(control.reference);
            }
        }
        const auto sort_references = [](auto& references) {
            std::sort(references.begin(), references.end(), [](const auto& left, const auto& right) {
                return lower(left) < lower(right);
            });
        };
        sort_references(data_set.used_by_report_controls);
        sort_references(data_set.used_by_goose_controls);
        sort_references(data_set.used_by_sampled_value_controls);
    }
