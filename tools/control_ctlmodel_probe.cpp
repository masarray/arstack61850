// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"
#include "ariec61850/mms/data_codec.hpp"
#include "ariec61850/mms/live_discovery.hpp"

#include <array>
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

namespace {

using namespace ar::iec61850;
using namespace ar::iec61850::control;
using namespace ar::iec61850::mms;

[[nodiscard]] const char* kind_name(const MmsDataKind kind) noexcept {
    switch (kind) {
    case MmsDataKind::array: return "array";
    case MmsDataKind::structure: return "structure";
    case MmsDataKind::boolean: return "boolean";
    case MmsDataKind::bit_string: return "bit-string";
    case MmsDataKind::integer: return "integer";
    case MmsDataKind::unsigned_integer: return "unsigned-integer";
    case MmsDataKind::floating_point: return "floating-point";
    case MmsDataKind::octet_string: return "octet-string";
    case MmsDataKind::visible_string: return "visible-string";
    case MmsDataKind::binary_time: return "binary-time";
    case MmsDataKind::bcd: return "bcd";
    case MmsDataKind::boolean_array: return "boolean-array";
    case MmsDataKind::object_id: return "object-id";
    case MmsDataKind::mms_string: return "mms-string";
    case MmsDataKind::utc_time: return "utc-time";
    case MmsDataKind::unknown: return "unknown";
    }
    return "unknown";
}

[[nodiscard]] std::string raw_hex(const MmsDataValue& value) {
    std::ostringstream output;
    output << std::uppercase << std::hex << std::setfill('0');
    for (const auto byte : value.raw_value()) {
        output << std::setw(2) << static_cast<unsigned>(byte);
    }
    return output.str();
}

[[nodiscard]] std::optional<std::int64_t> numeric_value(const MmsDataValue& value) noexcept {
    if (value.kind() == MmsDataKind::integer) {
        if (const auto* number = std::get_if<std::int64_t>(&value.value())) return *number;
    }
    if (value.kind() == MmsDataKind::unsigned_integer) {
        if (const auto* number = std::get_if<std::uint64_t>(&value.value())) {
            if (*number <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                return static_cast<std::int64_t>(*number);
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::string ctl_model_name(const std::optional<std::int64_t> value) {
    if (!value) return "not-decoded";
    switch (*value) {
    case 0: return "status-only";
    case 1: return "direct-normal";
    case 2: return "sbo-normal";
    case 3: return "direct-enhanced";
    case 4: return "sbo-enhanced";
    default: return "out-of-standard-range";
    }
}

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0UL || parsed > 65'535UL) {
        throw std::invalid_argument("MMS TCP port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

int run_self_test() {
    const auto value = MmsDataValue::integer(2);
    if (std::string{kind_name(value.kind())} != "integer" ||
        ctl_model_name(numeric_value(value)) != "sbo-normal") {
        std::cerr << "CTLMODEL_RAW_SELF_TEST FAIL\n";
        return 1;
    }
    std::cout << "CTLMODEL_RAW_SELF_TEST PASS readOnly=true controlWritePath=false\n";
    return 0;
}

void print_usage() {
    std::cout
        << "Usage:\n"
        << "  ariec61850_control_ctlmodel_probe <host> [port] --object LD/LN.DO\n\n"
        << "Safety: read-only diagnostic. Performs one MMS Read of CF/.../ctlModel.\n"
        << "It has no Write action and no arm token.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view{argv[1]} == "--self-test") return run_self_test();
        if (argc < 2 || std::string_view{argv[1]} == "--help" ||
            std::string_view{argv[1]} == "-h") {
            print_usage();
            return argc > 1 ? 0 : 2;
        }

        MmsEndpoint endpoint;
        endpoint.host = argv[1];
        endpoint.port = 102U;
        int argument = 2;
        if (argument < argc && std::string_view{argv[argument]}.find("--") != 0U) {
            endpoint.port = parse_port(argv[argument]);
            ++argument;
        }

        std::string object_text;
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (argument >= argc) throw std::invalid_argument(option + " requires a value.");
            const std::string value = argv[argument++];
            if (option == "--object") object_text = value;
            else throw std::invalid_argument("Unknown option: " + option);
        }
        if (object_text.empty()) throw std::invalid_argument("--object is required.");

        ControlObjectReference object;
        if (!try_parse_control_object_reference(object_text, object)) {
            throw std::invalid_argument("Control object must use LD/LN.DO form.");
        }
        std::array<char, 1'024U> item_buffer{};
        std::size_t item_bytes{};
        if (!build_control_item(object, "CF", "ctlModel", item_buffer, item_bytes)) {
            throw std::runtime_error("Cannot build ctlModel MMS item.");
        }
        const std::string item{item_buffer.data(), item_bytes};

        MmsAssociationOptions association_options;
        association_options.connect_timeout = std::chrono::milliseconds{5'000};
        association_options.request_timeout = std::chrono::milliseconds{5'000};
        MmsTcpLiveDiscoverySession live_session{{}, association_options};
        live_session.connect(endpoint);
        MmsAssociationControlTransport transport{live_session.association()};

        const auto mms_object = MmsObjectName::domain_specific(std::string{object.domain}, item);
        const auto value = transport.read(mms_object);
        if (!value) {
            throw std::runtime_error("MMS Read returned no ctlModel value.");
        }

        const auto numeric = numeric_value(*value);
        std::cout << "CTLMODEL_RAW object=" << object_text
                  << " mms=" << mms_object.reference()
                  << " kind=" << kind_name(value->kind())
                  << " display=\"" << MmsDataCodec::to_display_string(*value) << "\""
                  << " rawHex=" << (raw_hex(*value).empty() ? "<none>" : raw_hex(*value));
        if (value->unknown_tag_number()) {
            std::cout << " unknownTag=" << *value->unknown_tag_number();
        }
        if (numeric) {
            std::cout << " numeric=" << *numeric;
        }
        std::cout << " interpreted=" << ctl_model_name(numeric) << '\n';
        std::cout << "CTLMODEL_RAW_RESULT readOnly=true mmsControlWriteCount=0\n";
        std::cout << "PCAP_HINT filter=\"tcp.port == " << endpoint.port
                  << " && ip.addr == " << endpoint.host << "\"\n";

        live_session.disconnect();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "ctlModel raw probe failed: " << exception.what() << '\n';
        return 1;
    }
}
