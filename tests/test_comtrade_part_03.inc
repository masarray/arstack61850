    stream.entries[2].ln_class = "GGIO";
    stream.entries[2].do_name = "Value";
    stream.entries[2].da_name.clear();
    stream.entries[3] = stream.entries[1];
    stream.entries[3].index = 3U;
    stream.entries[3].signal_reference = "IED/LD0/TCTR1.Amp.q";
    stream.entries[3].da_name = "q";
    stream.entries[3].basic_type = "Quality";
    stream.entries[3].is_quality = true;
    const auto report = comtrade::SclMapper::map(stream, configuration);
    CHECK(report.bindings.size() == 3U);
    CHECK(report.bindings[0].channel_key == "Va");
    CHECK(report.bindings[0].analog_channel_index == std::optional<std::size_t>{0U});
    CHECK(report.bindings[0].confidence == comtrade::MappingConfidence::semantic);
    CHECK(report.bindings[1].channel_key == "Ia");
    CHECK(report.bindings[1].analog_channel_index == std::optional<std::size_t>{1U});
    CHECK(report.bindings[1].confidence == comtrade::MappingConfidence::semantic);
    CHECK(!report.bindings[2].analog_channel_index.has_value());
    CHECK(!report.complete());
}


void reader_loads_csharp_derived_fixtures() {
    using namespace ar::iec61850::comtrade;
    const auto root = std::filesystem::path{ARIEC61850_SOURCE_DIR} /
                      "tests" / "fixtures" / "comtrade";
    const auto ascii = Reader{}.load(root / "simple_fault.cfg");
    CHECK(ascii.sample_count() == 40U);
    CHECK(ascii.configuration.analog_channels.size() == 6U);
    CHECK(ascii.default_channel_map.size() == 6U);
    check_close(ascii.duration_seconds(), 0.00975);
    check_close(ascii.samples[14].analog_values[3], 7.560441, 1.0e-6);

    const auto binary = Reader{}.load(root / "simple_fault_binary.cfg");
    CHECK(binary.sample_count() == 80U);
    CHECK(binary.configuration.analog_channels.size() == 8U);
    CHECK(binary.default_channel_map.size() == 8U);
    CHECK(binary.configuration.data_file_type == DataFileType::binary);
}

void dataset_indexing_and_validation_are_bounded() {
    using namespace ar::iec61850::comtrade;
    Dataset dataset;
    dataset.samples = {
        {1U, 0.0, {1.0}, {}},
        {2U, 0.5, {2.0}, {}},
    };
    CHECK(dataset.sample_count() == 2U);
    check_close(dataset.duration_seconds(), 0.5);
    CHECK(dataset.sample_by_index(-1, false).number == 1U);
    CHECK(dataset.sample_by_index(5, false).number == 2U);
    CHECK(dataset.sample_by_index(-1, true).number == 2U);

    check_throws([] {
        static_cast<void>(ar::iec61850::comtrade::Reader{}.parse_configuration("short"));
    });
    check_throws([] {
        const auto configuration = ar::iec61850::comtrade::Reader{}.parse_configuration(
            configuration_text("ASCII"));
        static_cast<void>(ar::iec61850::comtrade::Reader{}.read_ascii_data("1,0", configuration));
    });
    check_throws([] {
        const auto configuration = ar::iec61850::comtrade::Reader{}.parse_configuration(
            configuration_text("BINARY"));
        static_cast<void>(ar::iec61850::comtrade::Reader{}.read_binary_data({}, configuration));
    });
    check_throws([] {
        const auto configuration = ar::iec61850::comtrade::Reader{}.parse_configuration(
            configuration_text("UNKNOWN"));
        static_cast<void>(ar::iec61850::comtrade::Reader{}.read_binary_data(
            std::vector<std::uint8_t>(32U), configuration));
    });
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests{
        {"COMTRADE configuration", parser_extracts_configuration_metadata},
        {"COMTRADE ASCII", reader_loads_ascii_scaling_digital_and_time},
        {"COMTRADE BINARY", reader_loads_binary_int16_and_status_words},
        {"COMTRADE BINARY32/FLOAT32", reader_loads_binary32_and_float32},
        {"COMTRADE multi-rate timing", reader_uses_multirate_schedule_for_missing_timestamps},
        {"COMTRADE channel map", channel_mapper_resolves_voltage_and_current_phase_keys},
        {"COMTRADE SCL map", scl_mapper_prefers_semantics_then_ordered_fallback},
        {"COMTRADE C# fixtures", reader_loads_csharp_derived_fixtures},
        {"COMTRADE bounds", dataset_indexing_and_validation_are_bounded},
    };

    std::size_t passed{};
    for (const auto& [name, test] : tests) {
        try {
            test();
            ++passed;
            std::cout << "[PASS] " << name << '\n';
        } catch (const std::exception& error) {
            std::cerr << "[FAIL] " << name << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "Passed " << passed << '/' << tests.size() << " COMTRADE tests.\n";
    return 0;
}
