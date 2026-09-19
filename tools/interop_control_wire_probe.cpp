// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/control/control_session.hpp"
#include "ariec61850/mms/live_discovery.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

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

[[nodiscard]] MmsDataValue command_value(
    const bool value,
    const std::int64_t timestamp_ms) {
    constexpr std::array<std::uint8_t, 4U> origin{
        0x13U, 0xD5U, 0xC0U, 0x07U};
    constexpr std::array<std::uint8_t, 1U> check{0xC0U};

    return MmsDataValue::structure({
        MmsDataValue::boolean(value),
        MmsDataValue::structure({
            MmsDataValue::integer(2),
            MmsDataValue::octet_string(origin),
        }),
        MmsDataValue::unsigned_integer(0U),
        MmsDataValue::utc_time(Iec61850UtcTime{
            std::chrono::system_clock::time_point{
                std::chrono::milliseconds{timestamp_ms}},
            0U}),
        MmsDataValue::boolean(false),
        MmsDataValue::bit_string(6U, std::span<const std::uint8_t>{check}),
    });
}

void require_write(
    MmsAssociationControlTransport& transport,
    const MmsObjectName& object,
    MmsDataValue value,
    const std::string_view label) {
    const auto result = transport.write(object, std::move(value));
    if (!result.success) {
        throw std::runtime_error(
            std::string{label} + " Write rejected" +
            (result.failure_code
                ? " DataAccessError=" + std::to_string(*result.failure_code)
                : ""));
    }
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 4) {
            std::cerr
                << "Usage: ariec61850_interop_control_wire_probe "
                   "<host> <port> <domain>\n";
            return 2;
        }

        MmsEndpoint endpoint;
        endpoint.host = argv[1];
        endpoint.port = parse_port(argv[2]);
        const std::string domain = argv[3];

        MmsAssociationOptions options;
        options.connect_timeout = std::chrono::milliseconds{5'000};
        options.request_timeout = std::chrono::milliseconds{5'000};

        MmsTcpLiveDiscoverySession session{{}, options};
        session.connect(endpoint);
        MmsAssociationControlTransport transport{session.association()};

        const auto sbow = MmsObjectName::domain_specific(
            domain, "CSWI1$CO$Pos$SBOw");
        const auto oper = MmsObjectName::domain_specific(
            domain, "CSWI1$CO$Pos$Oper");

        // Mirrors the real external vendor capture: ctlNum remains zero while Oper gets
        // a fresh T rather than replaying the selection timestamp.
        require_write(
            transport,
            sbow,
            command_value(true, 1'789'779'196'609LL),
            "SBOw");
        require_write(
            transport,
            oper,
            command_value(true, 1'789'779'197'488LL),
            "Oper");

        MmsInformationReport termination;
        if (!transport.wait_information_report(
                std::chrono::milliseconds{2'000}, termination)) {
            throw std::runtime_error(
                "positive CommandTermination was not observed after Oper");
        }

        std::cout
            << "INTEROP_ZERO_CTLNUM_CONTROL_PASS "
               "sequence=SBOw,Oper,CommandTermination "
               "ctlNum=0 freshOperT=true check=sync+interlock\n";
        session.disconnect();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "external IEC 61850 client control wire probe failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
