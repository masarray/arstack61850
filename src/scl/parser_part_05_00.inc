    // Build the complete structural leaf inventory from DataTypeTemplates before
    // resolving service bindings. This mirrors the C# simulator profile builder:
    // DataSets/GOOSE/reports enrich the model, but they never define which
    // LD/LN/DO/DA leaves exist in the simulated IED.
    std::size_t model_entry_index = 1U;
    for (const auto* ied : direct_children(root, "IED")) {
        const auto ied_name = attribute(ied, "name");
        for (const auto* logical_device : descendants(*ied, "LDevice")) {
            const auto ld_inst = attribute(logical_device, "inst");
            for (const auto& logical_node : logical_device->children) {
                if (!is(logical_node, "LN0") && !is(logical_node, "LN")) {
                    continue;
                }
                const auto prefix = attribute(&logical_node, "prefix");
                const auto ln_class = is(logical_node, "LN0")
                    ? std::string{"LLN0"}
                    : attribute(&logical_node, "lnClass");
                const auto ln_inst = attribute(&logical_node, "inst");
                const auto leaves = type_index.expand_logical_node(
                    ied_name,
                    ld_inst,
                    prefix,
                    ln_class,
                    ln_inst);
                for (const auto& leaf : leaves) {
                    if (!leaf.resolution.resolved ||
                        leaf.resolution.functional_constraint.empty()) {
                        document.warnings.push_back(
                            "Structural SCL leaf omitted because its type/FC could not be resolved: " +
                            build_signal_reference(
                                ied_name,
                                ld_inst,
                                prefix,
                                ln_class,
                                ln_inst,
                                leaf.do_name,
                                leaf.da_name,
                                leaf.resolution.functional_constraint) + ".");
                        continue;
                    }
                    document.model_entries.push_back({
                        model_entry_index++,
                        build_signal_reference(
                            ied_name,
                            ld_inst,
                            prefix,
                            ln_class,
                            ln_inst,
                            leaf.do_name,
                            leaf.da_name,
                            leaf.resolution.functional_constraint),
                        ied_name,
                        ld_inst,
                        prefix,
                        ln_class,
                        ln_inst,
                        leaf.do_name,
                        leaf.da_name,
                        leaf.resolution.functional_constraint,
                        leaf.resolution.cdc,
                        leaf.resolution.basic_type,
                        leaf.resolution.type_id,
                        leaf.resolution.enum_type,
                        is_quality_attribute(leaf.da_name),
                        is_timestamp_attribute(leaf.da_name),
                        type_index.configured_value(
                            ied_name,
                            ld_inst,
                            prefix,
                            ln_class,
                            ln_inst,
                            leaf.do_name,
                            leaf.da_name),
                    });
                }
            }
        }
    }

    if (const auto* communication = first_child(root, "Communication"); communication != nullptr) {
        for (const auto* connected_ap : descendants(*communication, "ConnectedAP")) {
            const auto ied_name = attribute(connected_ap, "iedName");
            for (const auto* gse : direct_children(*connected_ap, "GSE")) {
                address_index[stream_address_key(
                    "GOOSE",
                    ied_name,
                    attribute(gse, "ldInst"),
                    attribute(gse, "cbName"))] = build_address(*gse);
            }
            for (const auto* smv : direct_children(*connected_ap, "SMV")) {
                address_index[stream_address_key(
                    "SV",
                    ied_name,
                    attribute(smv, "ldInst"),
                    attribute(smv, "cbName"))] = build_address(*smv);
            }
        }
    }

    for (const auto* ied : direct_children(root, "IED")) {
        const auto ied_name = attribute(ied, "name");
        for (const auto* logical_device : descendants(*ied, "LDevice")) {
            const auto ld_inst = attribute(logical_device, "inst");
            for (const auto* logical_node_zero : direct_children(*logical_device, "LN0")) {
                for (const auto* control : direct_children(*logical_node_zero, "GSEControl")) {
                    const auto name = attribute(control, "name");
                    const auto data_set_name = attribute(control, "datSet");
                    const auto* data_set = resolve_data_set(
                        document.data_sets,
                        data_set_index,
                        ied_name,
                        ld_inst,
                        "LLN0",
                        data_set_name);
                    if (data_set == nullptr && !data_set_name.empty()) {
                        document.warnings.push_back(
                            "GOOSE " + ied_name + ld_inst + "/LLN0$GO$" + name +
                            " references missing DataSet '" + data_set_name + "'.");
                    }

                    SclGooseStream stream;
                    stream.kind = "GOOSE";
                    stream.ied_name = ied_name;
                    stream.ld_inst = ld_inst;
                    stream.control_name = name;
                    stream.control_block_reference = ied_name + ld_inst + "/LLN0$GO$" + name;
                    stream.go_id = attribute(control, "appID");
                    stream.data_set_name = data_set_name;
                    stream.data_set_reference = data_set != nullptr
