// SPDX-License-Identifier: GPL-3.0-or-later

#include "windows_serial_control.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using ar::iec61850::tools::WindowsSerialControlPort;

struct Options final {
    std::string device;
    std::vector<std::string> positional;
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
        } else {
            options.positional.push_back(argument);
        }
    }

    if (!options.help && options.device.empty()) {
        throw std::invalid_argument("--device COMx is required.");
    }
    if (!options.help && options.positional.empty()) {
        throw std::invalid_argument("A device command is required.");
    }
    return options;
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_sv_device_ctl --device COMx <command> [args] [options]\n\n"
        << "Control an ESP32-P4 ARStack61850 SV injector over USB Serial/JTAG.\n\n"
        << "Lifecycle commands:\n"
        << "  capabilities\n"
        << "  status\n"
        << "  configure normal\n"
        << "  configure protection-fault\n"
        << "  arm\n"
        << "  start\n"
        << "  stop\n"
        << "  stats\n\n"
        << "Realtime source commands:\n"
        << "  set-channel <channel> <field> <value>\n"
        << "  ramp-channel <channel> <field> <value> <durationSamples>\n\n"
        << "Channels: Ia Ib Ic In Va Vb Vc Vn\n"
        << "Fields: enabled rms dc phase frequency harmonic harmonic-order clip quality\n"
        << "  phase      = millidegrees\n"
        << "  frequency  = millihertz\n"
        << "  harmonic   = permyriad\n"
        << "  clip       = permyriad\n\n"
        << "Options:\n"
        << "  --device COMx       Windows COM port for ESP32-P4 USB Serial/JTAG.\n"
        << "  --timeout-ms N      Response timeout, 100..60000 (default 3000).\n"
        << "  --help              Show this help.\n\n"
        << "Examples:\n"
        << "  ariec61850_sv_device_ctl --device COM7 status\n"
        << "  ariec61850_sv_device_ctl --device COM7 arm\n"
        << "  ariec61850_sv_device_ctl --device COM7 start\n"
        << "  ariec61850_sv_device_ctl --device COM7 set-channel Ia rms 2500\n"
        << "  ariec61850_sv_device_ctl --device COM7 set-channel Va phase 15000\n"
        << "  ariec61850_sv_device_ctl --device COM7 ramp-channel Ia rms 5000 4000\n"
        << "  ariec61850_sv_device_ctl --device COM7 stop\n";
}

[[nodiscard]] bool valid_channel(const std::string_view channel) noexcept {
    return channel == "Ia" || channel == "Ib" || channel == "Ic" || channel == "In" ||
        channel == "Va" || channel == "Vb" || channel == "Vc" || channel == "Vn";
}

[[nodiscard]] std::string protocol_field(const std::string_view field) {
    if (field == "enabled") {
        return "enabled";
    }
    if (field == "rms") {
        return "rms";
    }
    if (field == "dc") {
        return "dc";
    }
    if (field == "phase") {
        return "phaseMilliDeg";
    }
    if (field == "frequency") {
        return "frequencyMilliHz";
    }
    if (field == "harmonic") {
        return "harmonicPermyriad";
    }
    if (field == "harmonic-order") {
        return "harmonicOrder";
    }
    if (field == "clip") {
        return "clipPermyriad";
    }
    if (field == "quality") {
        return "quality";
    }
    throw std::invalid_argument(
        "Unknown field. Use enabled, rms, dc, phase, frequency, harmonic, "
        "harmonic-order, clip, or quality.");
}

void validate_integer_text(const std::string& value, const std::string_view name) {
    if (value.empty()) {
        throw std::invalid_argument(std::string{name} + " requires an integer.");
    }
    std::size_t cursor = value.front() == '-' ? 1U : 0U;
    if (cursor == value.size()) {
        throw std::invalid_argument(std::string{name} + " requires an integer.");
    }
    for (; cursor < value.size(); ++cursor) {
        if (value[cursor] < '0' || value[cursor] > '9') {
            throw std::invalid_argument(std::string{name} + " requires an integer.");
        }
    }
}

[[nodiscard]] std::string make_request(const Options& options) {
    const auto& arguments = options.positional;
    const auto& command = arguments.front();

    if (command == "capabilities" || command == "status" ||
        command == "arm" || command == "start" ||
        command == "stop" || command == "stats") {
        if (arguments.size() != 1U) {
            throw std::invalid_argument(command + " does not accept positional arguments.");
        }
        return "{\"command\":\"" + command + "\"}";
    }

    if (command == "configure") {
        if (arguments.size() != 2U ||
            (arguments[1] != "normal" && arguments[1] != "protection-fault")) {
            throw std::invalid_argument(
                "configure requires scenario normal or protection-fault.");
        }
        return "{\"command\":\"configure\",\"scenario\":\"" +
            arguments[1] + "\"}";
    }

    if (command == "set-channel" || command == "ramp-channel") {
        const auto expected = command == "set-channel" ? 4U : 5U;
        if (arguments.size() != expected) {
            throw std::invalid_argument(
                command == "set-channel"
                    ? "set-channel requires <channel> <field> <value>."
                    : "ramp-channel requires <channel> <field> <value> <durationSamples>.");
        }
        if (!valid_channel(arguments[1])) {
            throw std::invalid_argument("Channel must be Ia, Ib, Ic, In, Va, Vb, Vc, or Vn.");
        }
        const auto field = protocol_field(arguments[2]);
        validate_integer_text(arguments[3], "value");

        auto request = "{\"command\":\"" + command + "\",\"channel\":\"" +
            arguments[1] + "\",\"" + field + "\":" + arguments[3];
        if (command == "ramp-channel") {
            validate_integer_text(arguments[4], "durationSamples");
            request += ",\"durationSamples\":" + arguments[4];
        }
        request += '}';
        return request;
    }

    throw std::invalid_argument(
        "Unsupported command. Use capabilities, status, configure, arm, start, "
        "stop, stats, set-channel, or ramp-channel.");
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
