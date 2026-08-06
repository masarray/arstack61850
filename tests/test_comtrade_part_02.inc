    check_close(samples[0].analog_values[1], 1.0);
    check_close(samples[1].analog_values[0], 90.0);
    check_close(samples[1].analog_values[1], -5.0);
    CHECK(samples[0].digital_values == std::vector<std::uint8_t>({1U, 0U}));
    CHECK(samples[1].digital_values == std::vector<std::uint8_t>({0U, 1U}));
}

void reader_loads_binary_int16_and_status_words() {
    using namespace ar::iec61850::comtrade;
    const auto configuration = Reader{}.parse_configuration(configuration_text("BINARY"));
    std::vector<std::uint8_t> bytes;
    append_i32(bytes, 1);
    append_i32(bytes, 0);
    append_i16(bytes, 10000);
    append_i16(bytes, 1000);
    append_u16(bytes, 0x0001U);
    append_i32(bytes, 2);
    append_i32(bytes, 250);
    append_i16(bytes, 9000);
    append_i16(bytes, -5000);
    append_u16(bytes, 0x0002U);

    const auto samples = Reader{}.read_binary_data(bytes, configuration);
    CHECK(samples.size() == 2U);
    check_close(samples[0].analog_values[0], 100.0);
    check_close(samples[1].analog_values[1], -5.0);
    CHECK(samples[0].digital_values == std::vector<std::uint8_t>({1U, 0U}));
    CHECK(samples[1].digital_values == std::vector<std::uint8_t>({0U, 1U}));
}

void reader_loads_binary32_and_float32() {
    using namespace ar::iec61850::comtrade;
    auto binary32 = Reader{}.parse_configuration(configuration_text("BINARY32"));
    std::vector<std::uint8_t> integer_bytes;
    append_i32(integer_bytes, 1);
    append_i32(integer_bytes, 0);
    append_i32(integer_bytes, 10000);
    append_i32(integer_bytes, -5000);
    append_u16(integer_bytes, 0x0003U);
    const auto integer_samples = Reader{}.read_binary_data(integer_bytes, binary32);
    check_close(integer_samples[0].analog_values[0], 100.0);
    check_close(integer_samples[0].analog_values[1], -5.0);

    auto float32 = Reader{}.parse_configuration(configuration_text("FLOAT32"));
    std::vector<std::uint8_t> float_bytes;
    append_i32(float_bytes, 1);
    append_i32(float_bytes, 0);
    append_float(float_bytes, 10000.0F);
    append_float(float_bytes, -5000.0F);
    append_u16(float_bytes, 0x0002U);
    const auto float_samples = Reader{}.read_binary_data(float_bytes, float32);
    check_close(float_samples[0].analog_values[0], 100.0);
    check_close(float_samples[0].analog_values[1], -5.0);
    CHECK(float_samples[0].digital_values == std::vector<std::uint8_t>({0U, 1U}));
}

void reader_uses_multirate_schedule_for_missing_timestamps() {
    using namespace ar::iec61850::comtrade;
    const auto configuration = Reader{}.parse_configuration(
        configuration_text("ASCII", "2\n1000,2\n2000,4", "2"));
    const auto samples = Reader{}.read_ascii_data(
        "1,invalid,0,0,0,0\n"
        "2,invalid,0,0,0,0\n"
        "3,invalid,0,0,0,0\n"
        "4,invalid,0,0,0,0\n",
        configuration);
    check_close(samples[0].timestamp_seconds, 0.0);
    check_close(samples[1].timestamp_seconds, 0.001);
    check_close(samples[2].timestamp_seconds, 0.002);
    check_close(samples[3].timestamp_seconds, 0.0025);
}

void channel_mapper_resolves_voltage_and_current_phase_keys() {
    using namespace ar::iec61850::comtrade;
    std::vector<AnalogChannel> channels(3U);
    channels[0].index = 1U;
    channels[0].name = "Bus Voltage";
    channels[0].phase = "A";
    channels[0].circuit_component = "BUS";
    channels[0].unit = "V";
    channels[1].index = 2U;
    channels[1].name = "Feeder Current";
    channels[1].phase = "B";
    channels[1].circuit_component = "F1";
    channels[1].unit = "A";
    channels[2].index = 3U;
    channels[2].name = "Residual Current";
    channels[2].phase = "N";
    channels[2].circuit_component = "F1";
    channels[2].unit = "A";
    const auto map = ChannelMapper::create_default_map(channels);
    CHECK(map.at("Va") == 0U);
    CHECK(map.at("Ib") == 1U);
    CHECK(map.at("In") == 2U);
}

void scl_mapper_prefers_semantics_then_ordered_fallback() {
    using namespace ar::iec61850;
    auto configuration = comtrade::Reader{}.parse_configuration(configuration_text("ASCII"));
    scl::SclSampledValuesStream stream;
    stream.entries.resize(4U);
    stream.entries[0].index = 0U;
    stream.entries[0].signal_reference = "IED/LD0/TVTR1.Vol.instMag.i";
    stream.entries[0].ied_name = "IED";
    stream.entries[0].ld_inst = "LD0";
    stream.entries[0].ln_class = "TVTR";
    stream.entries[0].ln_inst = "1";
    stream.entries[0].do_name = "Vol";
    stream.entries[0].da_name = "instMag.i";
    stream.entries[0].functional_constraint = "MX";
    stream.entries[0].cdc = "SAV";
    stream.entries[0].basic_type = "INT32";
    stream.entries[1] = stream.entries[0];
    stream.entries[1].index = 1U;
    stream.entries[1].signal_reference = "IED/LD0/TCTR1.Amp.instMag.i";
    stream.entries[1].ln_class = "TCTR";
    stream.entries[1].do_name = "Amp";
    stream.entries[2] = stream.entries[0];
    stream.entries[2].index = 2U;
    stream.entries[2].signal_reference = "IED/LD0/UNKNOWN1.Value";
