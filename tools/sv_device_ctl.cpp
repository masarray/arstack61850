// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows_serial_control.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using ar::iec61850::tools::WindowsSerialControlPort;

struct Options final {
    std::string device;
    std::string command;
    std::string scenario;
    std::uint32_t timeout_ms{3'000U};
    bool help{};
};

[[nodiscard]] std::uint32_t parse_timeout(const std::string& value) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoul(value, &consumed, 10);
    if (consumed != value.size() || parsed < 100U || parsed > 60'000U) {
        throw std::invalid_argument("--timeout-ms requires 100..60000.");
    }
    return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] Options parse_options(const int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto require_value = [&](const std::string_view option) -> std::string {
            if (index + 1 >= argc) {
                throw std::invalid_argument(std::string{option} + " requires a value.");
            }
            ++index;
            return argv[index];
        };

        if (argument == "--device") {
            options.device = require_value("--device");
        } else if (argument == "--timeout-ms") {
            options.timeout_ms = parse_timeout(require_value("--timeout-ms"));
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (options.command.empty()) {
            options.command = argument;
        } else if (options.scenario.empty()) {
            options.scenario = argument;
        } else {
            throw std::invalid_argument("Unexpected argument: " + argument);
        }
    }

    if (!options.help && options.device.empty()) {
        throw std::invalid_argument("--device COMx is required.");
    }
    if (!options.help && options.command.empty()) {
        throw std::invalid_argument("A device command is required.");
    }
    return options;
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_sv_device_ctl --device COMx <command> [scenario] [options]\n\n"
        << "Control an ESP32-P4 ARStack61850 SV injector over USB Serial/JTAG.\n\n"
        << "Commands:\n"
        << "  capabilities\n"
        << "  status\n"
        << "  configure normal\n"
        << "  configure protection-fault\n"
        << "  arm\n"
        << "  start\n"
        << "  stop\n"
        << "  stats\n\n"
        << "Options:\n"
        << "  --device COMx       Windows COM port for ESP32-P4 USB Serial/JTAG.\n"
        << "  --timeout-ms N      Response timeout, 100..60000 (default 3000).\n"
        << "  --help              Show this help.\n\n"
        << "Examples:\n"
        << "  ariec61850_sv_device_ctl --device COM7 status\n"
        << "  ariec61850_sv_device_ctl --device COM7 arm\n"
        << "  ariec61850_sv_device_ctl --device COM7 start\n"
        << "  ariec61850_sv_device_ctl --device COM7 stats\n"
        << "  ariec61850_sv_device_ctl --device COM7 stop\n";
}

[[nodiscard]] std::string make_request(const Options& options) {
    const auto& command = options.command;
    if (command == "capabilities" || command == "status" ||
        command == "arm" || command == "start" ||
        command == "stop" || command == "stats") {
        if (!options.scenario.empty()) {
            throw std::invalid_argument(command + " does not accept a scenario argument.");
        }
        return "{\"command\":\"" + command + "\"}";
    }

    if (command == "configure") {
        if (options.scenario != "normal" &&
            options.scenario != "protection-fault") {
            throw std::invalid_argument(
                "configure requires scenario normal or protection-fault.");
        }
        return "{\"command\":\"configure\",\"scenario\":\"" +
            options.scenario + "\"}";
    }

    throw std::invalid_argument(
        "Unsupported command '" + command +
        "'. Use capabilities, status, configure, arm, start, stop, or stats.");
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.help) {
            print_usage();
            return 0;
        }

        const auto request = make_request(options);
        WindowsSerialControlPort serial;
        if (!serial.open(options.device)) {
            throw std::runtime_error(serial.error());
        }
        if (!serial.write_line(request)) {
            throw std::runtime_error(serial.error());
        }

        std::string response;
        if (!serial.read_arctrl_line(response, options.timeout_ms)) {
            throw std::runtime_error(serial.error());
        }

        // Output only the JSON object. This keeps the executable easy to consume
        // from PowerShell, a future GUI, or automated test tooling.
        std::cout << response << '\n';
        return response.find("\"ok\":true") != std::string::npos ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "ariec61850_sv_device_ctl: " << error.what() << '\n';
        return 1;
    }
}
