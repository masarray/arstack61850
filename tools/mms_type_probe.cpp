// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"
#include "ariec61850/mms/live_discovery.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace ar::iec61850;
using namespace ar::iec61850::control;
using namespace ar::iec61850::mms;

[[nodiscard]] std::uint16_t parse_port(const std::string& value) {
    std::size_t consumed{};
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0UL || parsed > 65'535UL) {
        throw std::invalid_argument("MMS TCP port must be in the range 1..65535.");
    }
    return static_cast<std::uint16_t>(parsed);
}

void json_string(const std::string_view value) {
    std::cout << '"';
    for (const auto ch : value) {
        if (ch == '"' || ch == '\\') {
            std::cout << '\\' << ch;
        } else if (ch == '\n') {
            std::cout << "\\n";
        } else if (ch == '\r') {
            std::cout << "\\r";
        } else if (ch == '\t') {
            std::cout << "\\t";
        } else {
            std::cout << ch;
        }
    }
    std::cout << '"';
}

void write_type(const MmsTypeSpecification& type) {
    std::cout << "{\"name\":";
    json_string(type.name);
    std::cout << ",\"kind\":";
    json_string(type.mms_type_name());
    std::cout << ",\"size\":";
    if (type.size) std::cout << *type.size;
    else std::cout << "null";
    std::cout << ",\"variableLength\":"
              << (type.variable_length ? "true" : "false")
              << ",\"children\":[";
    for (std::size_t index = 0U; index < type.children.size(); ++index) {
        if (index != 0U) std::cout << ',';
        write_type(type.children[index]);
    }
    std::cout << "]}";
}

void usage() {
    std::cout
        << "Usage: ariec61850_mms_type_probe <host> [port] "
           "--domain DOMAIN --item ITEM\n"
        << "Read-only: issues one GetVariableAccessAttributes request and prints "
           "the ordered TypeSpecification as JSON.\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 2 || std::string_view{argv[1]} == "--help" ||
            std::string_view{argv[1]} == "-h") {
            usage();
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

        std::string domain;
        std::string item;
        while (argument < argc) {
            const std::string option = argv[argument++];
            if (argument >= argc) throw std::invalid_argument(option + " requires a value.");
            const std::string value = argv[argument++];
            if (option == "--domain") domain = value;
            else if (option == "--item") item = value;
            else throw std::invalid_argument("Unknown option: " + option);
        }
        if (domain.empty() || item.empty()) {
            throw std::invalid_argument("--domain and --item are required.");
        }

        MmsAssociationOptions association_options;
        association_options.connect_timeout = std::chrono::milliseconds{5'000};
        association_options.request_timeout = std::chrono::milliseconds{5'000};
        MmsTcpLiveDiscoverySession session{{}, association_options};
        session.connect(endpoint);
        MmsAssociationControlTransport transport{session.association()};

        const auto specification = transport.variable_specification(
            MmsObjectName::domain_specific(domain, item));
        if (!specification) {
            throw std::runtime_error("GetVariableAccessAttributes returned no TypeSpecification.");
        }

        std::cout << "{\"domain\":";
        json_string(domain);
        std::cout << ",\"item\":";
        json_string(item);
        std::cout << ",\"type\":";
        write_type(*specification);
        std::cout << "}\n";
        session.disconnect();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "MMS type probe failed: " << exception.what() << '\n';
        return 1;
    }
}
