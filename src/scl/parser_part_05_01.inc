                        ? data_set->reference
                        : build_data_set_reference(ied_name, ld_inst, "LLN0", data_set_name);
                    stream.configuration_revision = uint_attribute(*control, "confRev");
                    if (const auto address = address_index.find(stream_address_key(
                            "GOOSE", ied_name, ld_inst, name)); address != address_index.end()) {
                        stream.address = address->second;
                    }
                    if (data_set != nullptr) {
                        stream.entries = data_set->entries;
                    }
                    stream.min_time_milliseconds = uint_attribute(*control, "minTime");
                    stream.max_time_milliseconds = uint_attribute(*control, "maxTime");
                    document.goose_streams.push_back(std::move(stream));
                }

                for (const auto* control : direct_children(*logical_node_zero, "SampledValueControl")) {
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
                            "SV " + ied_name + ld_inst + "/LLN0$SV$" + name +
                            " references missing DataSet '" + data_set_name + "'.");
                    }

                    SclSampledValuesStream stream;
                    stream.kind = "SV";
                    stream.ied_name = ied_name;
                    stream.ld_inst = ld_inst;
                    stream.control_name = name;
                    stream.control_block_reference = ied_name + ld_inst + "/LLN0$SV$" + name;
                    stream.smv_id = attribute(control, "smvID");
                    stream.sv_id = attribute(control, "svID");
                    if (stream.sv_id.empty()) {
                        stream.sv_id = stream.smv_id;
                    }
                    stream.data_set_name = data_set_name;
                    stream.data_set_reference = data_set != nullptr
                        ? data_set->reference
                        : build_data_set_reference(ied_name, ld_inst, "LLN0", data_set_name);
