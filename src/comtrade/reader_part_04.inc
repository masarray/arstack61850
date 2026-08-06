        for (std::size_t index = 0U; index < configuration.analog_channels.size(); ++index) {
            const auto raw = parse_double(field(values, 2U + index), 0.0);
            const auto& channel = configuration.analog_channels[index];
            sample.analog_values.push_back((raw * channel.multiplier) + channel.offset);
        }
        sample.digital_values.reserve(configuration.digital_channels.size());
        const auto digital_offset = 2U + configuration.analog_channels.size();
        for (std::size_t index = 0U; index < configuration.digital_channels.size(); ++index) {
            sample.digital_values.push_back(
                static_cast<std::uint8_t>(parse_u32(field(values, digital_offset + index), 0U) != 0U));
        }
        samples.push_back(std::move(sample));
    }
    if (samples.empty()) {
        throw std::runtime_error("COMTRADE ASCII DAT contains no readable samples.");
    }
    return samples;
}

std::vector<Sample> Reader::read_binary_data(
    const std::span<const std::uint8_t> bytes,
    const Configuration& configuration) const {
    if (bytes.size() > maximum_data_bytes) {
        throw std::runtime_error("COMTRADE DAT exceeds the maximum supported size.");
    }
    std::size_t analog_width{};
    switch (configuration.data_file_type) {
    case DataFileType::binary:
        analog_width = 2U;
        break;
    case DataFileType::binary32:
    case DataFileType::float32:
        analog_width = 4U;
        break;
    case DataFileType::ascii:
    case DataFileType::unknown:
        throw std::runtime_error("COMTRADE configuration does not describe supported binary data.");
    }

    const auto digital_word_count = (configuration.digital_channels.size() + 15U) / 16U;
    auto record_length = checked_add(8U, checked_multiply(configuration.analog_channels.size(), analog_width));
    record_length = checked_add(record_length, checked_multiply(digital_word_count, 2U));
    if (record_length <= 8U) {
        throw std::runtime_error("COMTRADE binary record layout is invalid.");
    }
    if (bytes.size() < record_length) {
        throw std::runtime_error("COMTRADE binary DAT contains no complete sample record.");
    }
    const auto record_count = bytes.size() / record_length;
    if (record_count > maximum_samples) {
        throw std::runtime_error("COMTRADE DAT sample count exceeds the supported limit.");
    }

    std::vector<Sample> samples;
    samples.reserve(record_count);
    for (std::size_t sample_index = 0U; sample_index < record_count; ++sample_index) {
        std::size_t offset = checked_multiply(sample_index, record_length);
        const auto raw_number = read_i32_le(bytes, offset);
        offset += 4U;
        const auto raw_timestamp = read_i32_le(bytes, offset);
        offset += 4U;

        Sample sample;
        sample.number = raw_number > 0 ? static_cast<std::uint32_t>(raw_number)
                                       : static_cast<std::uint32_t>(sample_index + 1U);
        sample.timestamp_seconds = normalized_timestamp(
            configuration,
            raw_timestamp >= 0 ? static_cast<double>(raw_timestamp)
                               : std::numeric_limits<double>::quiet_NaN(),
            sample.number,
            sample_index,
            samples);
        sample.analog_values.reserve(configuration.analog_channels.size());
        for (std::size_t channel_index = 0U;
             channel_index < configuration.analog_channels.size(); ++channel_index) {
            double raw{};
            switch (configuration.data_file_type) {
            case DataFileType::binary:
                raw = static_cast<double>(read_i16_le(bytes, offset));
                break;
            case DataFileType::binary32:
                raw = static_cast<double>(read_i32_le(bytes, offset));
                break;
            case DataFileType::float32:
                raw = static_cast<double>(read_float32_le(bytes, offset));
                break;
            case DataFileType::ascii:
            case DataFileType::unknown:
                break;
            }
            const auto& channel = configuration.analog_channels[channel_index];
            sample.analog_values.push_back((raw * channel.multiplier) + channel.offset);
            offset += analog_width;
        }

        sample.digital_values.assign(configuration.digital_channels.size(), 0U);
        for (std::size_t word_index = 0U; word_index < digital_word_count; ++word_index) {
            const auto word = read_u16_le(bytes, offset);
            offset += 2U;
            for (std::size_t bit = 0U; bit < 16U; ++bit) {
                const auto digital_index = word_index * 16U + bit;
                if (digital_index >= sample.digital_values.size()) {
                    break;
                }
                sample.digital_values[digital_index] =
                    static_cast<std::uint8_t>((word & (static_cast<std::uint16_t>(1U) << bit)) != 0U);
            }
        }
        samples.push_back(std::move(sample));
    }
    return samples;
}

Dataset Reader::load(const std::filesystem::path& configuration_path) const {
    if (configuration_path.empty()) {
        throw std::invalid_argument("COMTRADE CFG path is empty.");
    }
    if (!std::filesystem::is_regular_file(configuration_path)) {
        throw std::runtime_error("COMTRADE CFG file was not found: " + configuration_path.string());
    }

    const auto configuration_text = read_text_file(configuration_path, maximum_configuration_bytes);
    auto configuration = parse_configuration(configuration_text, configuration_path.filename().string());
    auto data_path = configuration_path;
    data_path.replace_extension(".dat");
    if (!std::filesystem::is_regular_file(data_path)) {
        data_path = configuration_path;
        data_path.replace_extension(".DAT");
    }
    if (!std::filesystem::is_regular_file(data_path)) {
        throw std::runtime_error("COMTRADE DAT file was not found next to the CFG file.");
    }

    std::vector<Sample> samples;
    if (configuration.data_file_type == DataFileType::ascii) {
        samples = read_ascii_data(read_text_file(data_path, maximum_data_bytes), configuration);
    } else {
        samples = read_binary_data(read_binary_file(data_path, maximum_data_bytes), configuration);
    }

    auto default_map = ChannelMapper::create_default_map(configuration.analog_channels);
    return {
        configuration_path,
        data_path,
        std::move(configuration),
        std::move(samples),
        std::move(default_map),
    };
}

} // namespace ar::iec61850::comtrade
