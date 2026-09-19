                    report.configuration_revision = uint_attribute(*control, "confRev");
                    report.buffer_time_milliseconds = uint_attribute(*control, "bufTime");
                    report.integrity_period_milliseconds = uint_attribute(*control, "intgPd");
                    if (binding.data_set != nullptr) {
                        report.entries = binding.data_set->entries;
                    }
                    document.report_controls.push_back(std::move(report));
                }

                if (is(logical_node, "LN0")) {
                    for (const auto* control : direct_children(logical_node, "SettingControl")) {
                        SclSettingControl setting;
                        setting.ied_name = ied_name;
                        setting.ld_inst = ld_inst;
                        setting.logical_node_path = logical_node_path;
                        setting.control_block_reference =
                            ied_name + ld_inst + "/" + logical_node_path + "$SP$SGCB";
                        setting.number_of_setting_groups =
                            optional_uint_attribute(*control, "numOfSGs");
                        setting.active_setting_group =
                            optional_uint_attribute(*control, "actSG");
                        if (!setting.valid()) {
                            document.warnings.push_back(
                                "SettingControl '" + setting.control_block_reference +
                                "' has invalid or missing numOfSGs/actSG and will not be "
                                "operational in simulator service projection.");
                        }
                        document.setting_controls.push_back(std::move(setting));
                    }
                }
            }
        }
    }

    std::unordered_map<std::string, std::size_t> ied_counts;
    for (const auto& ied : document.ieds) {
        if (!ied.name.empty()) {
            ++ied_counts[normalized_key(ied.name)];
        }
    }
    for (const auto& [key, count] : ied_counts) {
        if (count > 1U) {
            const auto original = std::find_if(
                document.ieds.begin(),
                document.ieds.end(),
                [&key](const SclIed& ied) { return normalized_key(ied.name) == key; });
            const auto name = original == document.ieds.end() ? key : original->name;
            document.conflicts.push_back({
                "IED",
                name,
                "Duplicate IED name '" + name + "' appears " + std::to_string(count) + " times.",
            });
        }
    }

    const auto detect_address_conflicts = [&document](const std::string& kind, const auto& streams) {
        std::unordered_map<std::uint16_t, std::vector<std::string>> controls;
        for (const auto& stream : streams) {
            if (stream.address.app_id) {
                controls[*stream.address.app_id].push_back(stream.control_block_reference);
            }
        }
        for (const auto& [app_id, references] : controls) {
            if (references.size() <= 1U) {
                continue;
            }
            std::ostringstream hex;
            hex << std::uppercase << std::hex;
            hex.width(4);
            hex.fill('0');
            hex << app_id;
            std::string names;
            for (std::size_t index = 0U; index < references.size(); ++index) {
                if (index != 0U) {
                    names += ", ";
                }
                names += references[index];
            }
            document.conflicts.push_back({
                kind,
                "APPID 0x" + hex.str(),
                "Duplicate " + kind + " APPID 0x" + hex.str() + ": " + names + ".",
            });
        }
    };
    detect_address_conflicts("GOOSE", document.goose_streams);
