// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/mms/control_block_read.hpp"
#include "ariec61850/mms/live_discovery.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace {

using namespace ar::iec61850;

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0UL || parsed > 65'535UL) {
        throw std::invalid_argument("MMS TCP port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

[[nodiscard]] std::size_t parse_limit(
    const std::string& option,
    const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0ULL ||
        parsed > static_cast<unsigned long long>(
            std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(option + " requires a positive bounded integer.");
    }
    return static_cast<std::size_t>(parsed);
}

[[nodiscard]] mms::MmsControlBlockKind parse_kind(const std::string& value) {
    if (value == "goose" || value == "go") {
        return mms::MmsControlBlockKind::goose;
    }
    if (value == "sv" || value == "sampled-value") {
        return mms::MmsControlBlockKind::sampled_value;
    }
    if (value == "sg" || value == "setting-group") {
        return mms::MmsControlBlockKind::setting_group;
    }
    if (value == "log" || value == "lg") {
        return mms::MmsControlBlockKind::log;
    }
    throw std::invalid_argument(
        "--kind requires goose, sv, sg, or log.");
}

[[nodiscard]] std::string hex_bytes(const std::vector<std::uint8_t>& bytes) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : bytes) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

[[nodiscard]] std::string value_text(const mms::MmsDataValue& value) {
    switch (value.kind()) {
    case mms::MmsDataKind::boolean:
        return std::get<bool>(value.value()) ? "true" : "false";
    case mms::MmsDataKind::integer:
        return std::to_string(std::get<std::int64_t>(value.value()));
    case mms::MmsDataKind::unsigned_integer:
        return std::to_string(std::get<std::uint64_t>(value.value()));
    case mms::MmsDataKind::floating_point: {
        std::ostringstream output;
        if (const auto* float_value = std::get_if<float>(&value.value())) {
            output << *float_value;
        } else if (const auto* double_value = std::get_if<double>(&value.value())) {
            output << *double_value;
        }
        return output.str();
    }
    case mms::MmsDataKind::visible_string:
    case mms::MmsDataKind::mms_string:
        return std::get<std::string>(value.value());
    case mms::MmsDataKind::array:
    case mms::MmsDataKind::structure: {
        std::ostringstream output;
        output << '[';
        for (std::size_t index = 0U; index < value.children().size(); ++index) {
            if (index != 0U) {
                output << ',';
            }
            output << value_text(value.children()[index]);
        }
        output << ']';
        return output.str();
    }
    case mms::MmsDataKind::bit_string:
    case mms::MmsDataKind::octet_string:
    case mms::MmsDataKind::binary_time:
    case mms::MmsDataKind::bcd:
    case mms::MmsDataKind::boolean_array:
    case mms::MmsDataKind::object_id:
    case mms::MmsDataKind::unknown:
        return "0x" + hex_bytes(value.raw_value());
    case mms::MmsDataKind::utc_time:
        return "<utc-time>";
    }
    return "<unsupported>";
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_control_block_read_probe <host> [port] [options]\n"
        << "Options:\n"
        << "  --control REF          Read only this discovered control reference.\n"
        << "  --kind KIND            Filter: goose, sv, sg, or log.\n"
        << "  --max-control-blocks N Bound control blocks read (default 10).\n"
        << "  --max-attributes N     Bound attributes per block (default 64).\n"
        << "  --timeout-ms N         Connect/request timeout (default 5000).\n\n"
        << "This tool sends GetNameList plus MMS Read only. It never writes/enables "
        << "a control block and does not infer SCL-only APPID/MAC/VLAN values.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2) {
            print_usage();
            return 2;
        }

        mms::MmsEndpoint endpoint;
        endpoint.host = argv[1];
        endpoint.port = 102U;
        int argument = 2;
        if (argument < argc && std::string_view{argv[argument]}.find("--") != 0U) {
            endpoint.port = parse_port(argv[argument]);
            ++argument;
        }

        std::string selected_reference;
        std::optional<mms::MmsControlBlockKind> selected_kind;
        std::size_t maximum_controls{10U};
        std::size_t maximum_attributes{64U};
        std::chrono::milliseconds timeout{5'000};

        while (argument < argc) {
            const std::string option = argv[argument++];
            if (option == "--help" || option == "-h") {
                print_usage();
                return 0;
            }
            if (argument >= argc) {
                throw std::invalid_argument(option + " requires a value.");
            }
            const std::string value = argv[argument++];
            if (option == "--control") {
                selected_reference = value;
            } else if (option == "--kind") {
                selected_kind = parse_kind(value);
            } else if (option == "--max-control-blocks") {
                maximum_controls = parse_limit(option, value);
            } else if (option == "--max-attributes") {
                maximum_attributes = parse_limit(option, value);
            } else if (option == "--timeout-ms") {
                const auto parsed = parse_limit(option, value);
                if (parsed > static_cast<std::size_t>(
                        std::numeric_limits<std::int64_t>::max())) {
                    throw std::invalid_argument("--timeout-ms is too large.");
                }
                timeout = std::chrono::milliseconds{
                    static_cast<std::int64_t>(parsed)};
            } else {
                throw std::invalid_argument("Unknown option: " + option);
            }
        }

        mms::MmsAssociationOptions association_options;
        association_options.connect_timeout = timeout;
        association_options.request_timeout = timeout;
        mms::MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);

        mms::MmsLiveDiscoveryOptions discovery_options;
        discovery_options.probe_variable_types = false;
        discovery_options.read_data_set_directories = false;
        discovery_options.probe_report_controls = false;
        const auto discovery = session.discover(discovery_options);
        auto candidates = mms::MmsControlBlockInventoryBuilder::build(discovery.names);

        candidates.erase(
            std::remove_if(
                candidates.begin(), candidates.end(), [&](const auto& candidate) {
                    if (selected_kind && candidate.kind != *selected_kind) {
                        return true;
                    }
                    return !selected_reference.empty() &&
                           candidate.reference != selected_reference;
                }),
            candidates.end());

        std::cout << "Read-only control-block value probe: endpoint="
                  << endpoint.host << ':' << endpoint.port
                  << ", associationProfile="
                  << (discovery.association_profile.empty()
                          ? std::string{"unknown"}
                          : discovery.association_profile)
                  << ", associationAttempts=" << discovery.association_attempts.size()
                  << ", discovered=" << candidates.size() << ".\n";

        if (candidates.empty()) {
            session.disconnect();
            std::cout << "No matching GO/SV/SG/LG control blocks were exposed by live MMS NameList.\n";
            return selected_reference.empty() ? 0 : 3;
        }

        mms::MmsControlBlockReadClient reader{session.association()};
        const auto count = std::min(maximum_controls, candidates.size());
        std::size_t complete{};
        std::size_t partial{};
        std::size_t failed{};
        for (std::size_t index = 0U; index < count; ++index) {
            const auto result = reader.read(
                candidates[index], maximum_attributes);
            if (result.complete()) {
                ++complete;
            } else if (result.exchange_success() &&
                       result.successful_attribute_count() != 0U) {
                ++partial;
            } else {
                ++failed;
            }

            std::cout << "CB " << result.candidate.reference
                      << " kind=" << mms::mms_control_block_kind_name(
                             result.candidate.kind)
                      << " attributes=" << result.successful_attribute_count()
                      << '/' << result.attributes.size()
                      << " status=" << (result.complete() ? "Complete"
                          : result.successful_attribute_count() != 0U ? "Partial"
                          : "Failed") << '\n';
            for (const auto& attribute : result.attributes) {
                std::cout << "  " << attribute.attribute_path << '=';
                if (attribute.value) {
                    std::cout << value_text(*attribute.value);
                } else if (attribute.failure_code) {
                    std::cout << "<access-failure:" << *attribute.failure_code << '>';
                } else {
                    std::cout << "<not-returned>";
                }
                std::cout << " [" << attribute.variable.item << "]\n";
            }
            if (!result.error.empty()) {
                std::cout << "  error=" << result.error << '\n';
            }
        }

        session.disconnect();
        std::cout << "Control-block read summary: attempted=" << count
                  << ", complete=" << complete
                  << ", partial=" << partial
                  << ", failed=" << failed << ".\n";
        return failed == count ? 4 : 0;
    } catch (const std::exception& exception) {
        std::cerr << "Control-block read probe failed: " << exception.what() << '\n';
        return 1;
    }
}
