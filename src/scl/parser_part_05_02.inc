                    stream.configuration_revision = uint_attribute(*control, "confRev");
                    if (const auto address = address_index.find(stream_address_key(
                            "SV", ied_name, ld_inst, name)); address != address_index.end()) {
                        stream.address = address->second;
                    }
                    if (data_set != nullptr) {
                        stream.entries = data_set->entries;
                    }
                    stream.sample_rate = ushort_attribute(*control, "smpRate");
                    stream.sample_mode = attribute(control, "smpMod");
                    stream.no_asdu = std::max<std::uint16_t>(
                        1U,
                        ushort_attribute(*control, "nofASDU"));
                    if (const auto* options = first_child(*control, "SmvOpts"); options != nullptr) {
                        stream.smv_options.element_present = true;
                        stream.smv_options.refresh_time = bool_attribute(*options, "refreshTime");
                        stream.smv_options.sample_synchronized =
                            bool_attribute(*options, "sampleSynchronized");
                        stream.smv_options.sample_rate = bool_attribute(*options, "sampleRate");
                        stream.smv_options.data_set = bool_attribute(*options, "dataSet");
                        stream.smv_options.security = bool_attribute(*options, "security");
                        stream.smv_options.synch_source_id =
                            bool_attribute(*options, "synchSourceId");
                    }
                    document.sampled_values_streams.push_back(std::move(stream));
                }
            }

            for (const auto& logical_node : logical_device->children) {
                if (!is(logical_node, "LN0") && !is(logical_node, "LN")) {
                    continue;
                }
                const auto logical_node_path = build_logical_node_path(logical_node);
                for (const auto* control : direct_children(logical_node, "ReportControl")) {
                    const auto data_set_name = attribute(control, "datSet");
                    const auto binding = SclDataSetReferenceResolver::resolve(
                        document.data_sets,
                        ied_name,
                        ld_inst,
                        logical_node_path,
                        data_set_name);
                    const auto buffered = bool_attribute(*control, "buffered");

                    SclReportControl report;
                    report.ied_name = ied_name;
                    report.ld_inst = ld_inst;
                    report.logical_node_path = logical_node_path;
                    report.name = attribute(control, "name");
                    report.report_id = attribute(control, "rptID");
                    report.data_set_name = data_set_name;
                    report.data_set_reference = binding.canonical_reference;
                    report.data_set_binding_status = binding.status;
                    report.control_block_reference = ied_name + ld_inst + "/" + logical_node_path +
                        (buffered ? "$BR$" : "$RP$") + report.name;
                    report.buffered = buffered;
                    report.indexed = !same(attribute(control, "indexed"), "false");