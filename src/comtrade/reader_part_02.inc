    return DataFileType::unknown;
}

std::size_t checked_add(const std::size_t left, const std::size_t right) {
    if (right > std::numeric_limits<std::size_t>::max() - left) {
        throw std::runtime_error("COMTRADE record length overflow.");
    }
    return left + right;
}

std::size_t checked_multiply(const std::size_t left, const std::size_t right) {
    if (left != 0U && right > std::numeric_limits<std::size_t>::max() / left) {
        throw std::runtime_error("COMTRADE record length overflow.");
    }
    return left * right;
}

double sample_time_from_schedule(
    const Configuration& configuration,
    const std::uint32_t sample_number,
    const std::size_t zero_based_index) {
    if (configuration.sample_rates.empty()) {
        return static_cast<double>(zero_based_index);
    }

    const auto normalized_number = sample_number == 0U
                                       ? static_cast<std::uint64_t>(zero_based_index) + 1U
                                       : static_cast<std::uint64_t>(sample_number);
    std::uint64_t segment_first = 1U;
    double elapsed = 0.0;
    for (const auto& rate : configuration.sample_rates) {
        if (!(rate.rate_hz > 0.0) || !std::isfinite(rate.rate_hz)) {
            continue;
        }
        const auto segment_last = static_cast<std::uint64_t>(rate.last_sample_number);
        if (segment_last >= segment_first && normalized_number <= segment_last) {
            return elapsed + static_cast<double>(normalized_number - segment_first) / rate.rate_hz;
        }
        if (segment_last >= segment_first) {
            elapsed += static_cast<double>(segment_last - segment_first + 1U) / rate.rate_hz;
            segment_first = segment_last + 1U;
        }
    }

    for (auto iterator = configuration.sample_rates.rbegin();
         iterator != configuration.sample_rates.rend(); ++iterator) {
        if (iterator->rate_hz > 0.0 && std::isfinite(iterator->rate_hz)) {
            return elapsed + static_cast<double>(normalized_number - segment_first) / iterator->rate_hz;
        }
    }
    return static_cast<double>(zero_based_index);
}

double normalized_timestamp(
    const Configuration& configuration,
    const double raw_timestamp,
    const std::uint32_t sample_number,
    const std::size_t index,
    const std::vector<Sample>& samples) {
    const auto multiplier = configuration.time_multiplier == 0.0 ? 1.0 : configuration.time_multiplier;
    double timestamp = std::isfinite(raw_timestamp) && raw_timestamp >= 0.0
                           ? raw_timestamp * multiplier / 1000000.0
                           : sample_time_from_schedule(configuration, sample_number, index);
    if (!samples.empty() && timestamp <= samples.back().timestamp_seconds) {
        timestamp = sample_time_from_schedule(configuration, sample_number, index);
    }
    if (!samples.empty() && timestamp <= samples.back().timestamp_seconds) {
        double rate = configuration.sample_rates.empty() ? 0.0 : configuration.sample_rates.front().rate_hz;
        if (!(rate > 0.0)) {
            rate = 1.0;
        }
        timestamp = samples.back().timestamp_seconds + (1.0 / rate);
    }
    return timestamp;
}

std::uint16_t read_u16_le(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
}

std::int16_t read_i16_le(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return std::bit_cast<std::int16_t>(read_u16_le(bytes, offset));
}

std::uint32_t read_u32_le(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
           (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
           (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
}

std::int32_t read_i32_le(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return std::bit_cast<std::int32_t>(read_u32_le(bytes, offset));
}

float read_float32_le(const std::span<const std::uint8_t> bytes, const std::size_t offset) {
    return std::bit_cast<float>(read_u32_le(bytes, offset));
}

std::string read_text_file(const std::filesystem::path& path, const std::size_t limit) {
    const auto size = std::filesystem::file_size(path);
    if (size > limit) {
        throw std::runtime_error("COMTRADE file exceeds the configured size limit: " + path.string());
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open COMTRADE file: " + path.string());
    }
    return {std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

std::vector<std::uint8_t> read_binary_file(const std::filesystem::path& path, const std::size_t limit) {
    const auto size = std::filesystem::file_size(path);
    if (size > limit) {
        throw std::runtime_error("COMTRADE file exceeds the configured size limit: " + path.string());
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("Unable to open COMTRADE file: " + path.string());
    }
    const std::string raw{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    return {raw.begin(), raw.end()};
}

} // namespace

Configuration Reader::parse_configuration(std::string_view text, std::string source_name) const {
    if (text.size() > maximum_configuration_bytes) {
        throw std::runtime_error("COMTRADE CFG exceeds the maximum supported size.");
    }
    const auto lines = split_lines(text);
    if (lines.size() < 8U) {
        throw std::runtime_error("COMTRADE CFG is too short.");
    }

    Configuration configuration;
    configuration.source_name = std::move(source_name);
    std::size_t cursor = 0U;

    const auto identity = split_csv(lines[cursor++]);
    configuration.station_name = field(identity, 0U);
    configuration.device_id = field(identity, 1U);
    configuration.revision_year = parse_u32(field(identity, 2U), 1999U);

    const auto counts = split_csv(lines[cursor++]);
    configuration.total_channel_count = parse_channel_count(field(counts, 0U));
    configuration.analog_channel_count = parse_typed_count(field(counts, 1U), 'A');
    configuration.digital_channel_count = parse_typed_count(field(counts, 2U), 'D');
    if (configuration.analog_channel_count > maximum_channels ||
        configuration.digital_channel_count > maximum_channels ||
