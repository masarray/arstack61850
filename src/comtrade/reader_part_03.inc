        configuration.total_channel_count > maximum_channels) {
        throw std::runtime_error("COMTRADE channel count exceeds the supported limit.");
    }
    const auto counted_total = static_cast<std::uint64_t>(configuration.analog_channel_count) +
                               static_cast<std::uint64_t>(configuration.digital_channel_count);
    if (counted_total != configuration.total_channel_count) {
        configuration.warnings.push_back(
            "Declared total channel count does not equal analog plus digital channel counts.");
    }

    configuration.analog_channels.reserve(configuration.analog_channel_count);
    for (std::uint32_t index = 0U; index < configuration.analog_channel_count; ++index) {
        if (cursor >= lines.size()) {
            throw std::runtime_error("COMTRADE CFG ended inside analog channel definitions.");
        }
        const auto values = split_csv(lines[cursor++]);
        AnalogChannel channel;
        channel.index = parse_u32(field(values, 0U), index + 1U);
        channel.name = field(values, 1U);
        channel.phase = field(values, 2U);
        channel.circuit_component = field(values, 3U);
        channel.unit = field(values, 4U);
        channel.multiplier = parse_double(field(values, 5U), 1.0);
        channel.offset = parse_double(field(values, 6U), 0.0);
        channel.skew_microseconds = parse_double(field(values, 7U), 0.0);
        channel.minimum = parse_double(field(values, 8U), 0.0);
        channel.maximum = parse_double(field(values, 9U), 0.0);
        channel.primary = parse_double(field(values, 10U), 1.0);
        channel.secondary = parse_double(field(values, 11U), 1.0);
        channel.scaling_identifier = field(values, 12U);
        configuration.analog_channels.push_back(std::move(channel));
    }

    configuration.digital_channels.reserve(configuration.digital_channel_count);
    for (std::uint32_t index = 0U; index < configuration.digital_channel_count; ++index) {
        if (cursor >= lines.size()) {
            throw std::runtime_error("COMTRADE CFG ended inside digital channel definitions.");
        }
        const auto values = split_csv(lines[cursor++]);
        DigitalChannel channel;
        channel.index = parse_u32(field(values, 0U), index + 1U);
        channel.name = field(values, 1U);
        channel.phase = field(values, 2U);
        channel.circuit_component = field(values, 3U);
        channel.normal_state = parse_u32(field(values, 4U), 0U) != 0U;
        configuration.digital_channels.push_back(std::move(channel));
    }

    if (cursor >= lines.size()) {
        throw std::runtime_error("COMTRADE CFG ended before nominal frequency.");
    }
    configuration.line_frequency_hz = parse_double(lines[cursor++], 0.0);

    if (cursor >= lines.size()) {
        throw std::runtime_error("COMTRADE CFG ended before sample-rate count.");
    }
    const auto rate_count = parse_u32(trim_copy(lines[cursor++]), 0U);
    if (rate_count > 10000U) {
        throw std::runtime_error("COMTRADE sample-rate segment count exceeds the supported limit.");
    }
    configuration.sample_rates.reserve(rate_count == 0U ? 1U : rate_count);
    std::uint32_t previous_last{};
    for (std::uint32_t index = 0U; index < rate_count; ++index) {
        if (cursor >= lines.size()) {
            throw std::runtime_error("COMTRADE CFG ended inside sample-rate definitions.");
        }
        const auto values = split_csv(lines[cursor++]);
        SampleRate rate;
        rate.rate_hz = parse_double(field(values, 0U), 0.0);
        rate.last_sample_number = parse_u32(field(values, 1U), 0U);
        if (rate.rate_hz < 0.0 || !std::isfinite(rate.rate_hz)) {
            throw std::runtime_error("COMTRADE sample rate is invalid.");
        }
        if (rate.last_sample_number < previous_last) {
            configuration.warnings.push_back("Sample-rate end sample numbers are not monotonic.");
        }
        previous_last = rate.last_sample_number;
        configuration.sample_rates.push_back(rate);
    }
    if (configuration.sample_rates.empty() && !configuration.analog_channels.empty()) {
        configuration.sample_rates.push_back({0.0, 0U});
    }

    if (cursor < lines.size()) {
        configuration.start_timestamp_text = trim_copy(lines[cursor++]);
    }
    if (cursor < lines.size()) {
        configuration.trigger_timestamp_text = trim_copy(lines[cursor++]);
    }
    if (cursor < lines.size()) {
        configuration.data_file_type_text = uppercase(field(split_csv(lines[cursor++]), 0U));
    }
    configuration.data_file_type = parse_data_type(configuration.data_file_type_text);
    if (configuration.data_file_type == DataFileType::unknown) {
        configuration.warnings.push_back(
            "Unsupported DAT type '" + configuration.data_file_type_text + "'.");
    }
    if (cursor < lines.size()) {
        configuration.time_multiplier = parse_double(field(split_csv(lines[cursor++]), 0U), 1.0);
    }
    if (!std::isfinite(configuration.time_multiplier) || configuration.time_multiplier == 0.0) {
        configuration.time_multiplier = 1.0;
        configuration.warnings.push_back("Invalid time multiplier was normalized to 1.0.");
    }

    if (cursor < lines.size()) {
        const auto time_codes = split_csv(lines[cursor++]);
        configuration.time_code = field(time_codes, 0U);
        configuration.local_code = field(time_codes, 1U);
    }
    if (cursor < lines.size()) {
        const auto quality = split_csv(lines[cursor]);
        configuration.time_quality = field(quality, 0U);
        configuration.leap_second = field(quality, 1U);
    }

    return configuration;
}

std::vector<Sample> Reader::read_ascii_data(
    std::string_view text,
    const Configuration& configuration) const {
    if (text.size() > maximum_data_bytes) {
        throw std::runtime_error("COMTRADE DAT exceeds the maximum supported size.");
    }
    if (configuration.data_file_type != DataFileType::ascii) {
        throw std::runtime_error("COMTRADE configuration does not describe ASCII data.");
    }

    const auto lines = split_lines(text);
    if (lines.size() > maximum_samples) {
        throw std::runtime_error("COMTRADE DAT sample count exceeds the supported limit.");
    }
    std::vector<Sample> samples;
    samples.reserve(lines.size());
    const auto required_fields = checked_add(
        2U,
        checked_add(configuration.analog_channels.size(), configuration.digital_channels.size()));

    for (const auto line : lines) {
        const auto values = split_csv(line);
        if (values.size() < required_fields) {
            throw std::runtime_error("COMTRADE ASCII DAT record has too few fields.");
        }
        Sample sample;
        sample.number = parse_u32(field(values, 0U), static_cast<std::uint32_t>(samples.size() + 1U));
        const auto raw_timestamp = parse_double(field(values, 1U),
                                                std::numeric_limits<double>::quiet_NaN());
        sample.timestamp_seconds = normalized_timestamp(
            configuration, raw_timestamp, sample.number, samples.size(), samples);
        sample.analog_values.reserve(configuration.analog_channels.size());
