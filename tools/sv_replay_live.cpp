// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/publisher.hpp"
#include "ariec61850/sampled_values/replay_bundle.hpp"
#include "npcap_live.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

using namespace ar::iec61850;

constexpr std::uint64_t kMaximumBundleBytes = 2ULL * 1024ULL * 1024ULL * 1024ULL;

#ifdef _WIN32
constexpr std::uint16_t kSampleCountWrap = 4'000U;
constexpr std::size_t kFrameBufferBytes = 1'536U;
const ethernet::MacAddress kDestination{
    std::array<std::uint8_t, 6U>{0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U}};
const ethernet::MacAddress kSource{
    std::array<std::uint8_t, 6U>{0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U}};

volatile std::sig_atomic_t gStopRequested = 0;

void handle_interrupt(const int signal) noexcept {
    if (signal == SIGINT) {
        gStopRequested = 1;
    }
}
#endif

struct Options final {
    std::filesystem::path bundle_path;
    std::string interface_selector;
    bool continuous{};
    bool validate{};
    bool list_interfaces{};
    bool help{};
};

struct LoadedBundle final {
    sampled_values::ReplayBundleHeader header{};
    std::vector<std::uint8_t> bytes;

    [[nodiscard]] std::span<const std::uint8_t> payload(
        const std::uint64_t frame_index) const {
        if (frame_index >= header.frame_count) {
            throw std::out_of_range("Replay frame index exceeds bundle.");
        }
        const auto offset = static_cast<std::uint64_t>(
            sampled_values::replay_bundle_header_bytes) +
            frame_index * static_cast<std::uint64_t>(header.payload_bytes);
        const auto begin = static_cast<std::size_t>(offset);
        return std::span<const std::uint8_t>{
            bytes.data() + begin,
            static_cast<std::size_t>(header.payload_bytes)};
    }
};

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

        if (argument == "--bundle") {
            options.bundle_path = require_value("--bundle");
        } else if (argument == "--interface") {
            options.interface_selector = require_value("--interface");
        } else if (argument == "--continuous") {
            options.continuous = true;
        } else if (argument == "--validate") {
            options.validate = true;
        } else if (argument == "--list-interfaces") {
            options.list_interfaces = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            throw std::invalid_argument("Unknown option: " + argument);
        }
    }

    if (!options.help && !options.list_interfaces && options.bundle_path.empty()) {
        throw std::invalid_argument("--bundle PATH is required.");
    }
    if (!options.help && !options.list_interfaces && !options.validate &&
        options.interface_selector.empty()) {
        throw std::invalid_argument(
            "Live replay requires --interface NAME. Use --validate for offline bundle validation.");
    }
    return options;
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_sv_replay_live --bundle FILE [options]\n\n"
        << "Replay a normalized recorded-waveform bundle as IEC 61850 Sampled Values.\n\n"
        << "Options:\n"
        << "  --bundle PATH        Replay bundle produced by ariec61850_sv_replay_prepare.\n"
        << "  --interface NAME     Exact or unique Npcap adapter selector.\n"
        << "  --continuous         Repeat the bundle until Ctrl+C.\n"
        << "  --validate           Validate bundle offline and exit.\n"
        << "  --list-interfaces    List Npcap adapters and exit.\n"
        << "  --help               Show this help.\n\n"
        << "Examples:\n"
        << "  ariec61850_sv_replay_live --bundle fault.arsvr --validate\n"
        << "  ariec61850_sv_replay_live --bundle fault.arsvr --interface \"Ethernet\"\n"
        << "  ariec61850_sv_replay_live --bundle fault.arsvr --interface \"Ethernet\" --continuous\n";
}

[[nodiscard]] LoadedBundle load_bundle(const std::filesystem::path& path) {
    std::error_code error;
    const auto file_size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error("Could not read replay bundle size: " + error.message());
    }
    if (file_size < sampled_values::replay_bundle_header_bytes ||
        file_size > kMaximumBundleBytes ||
        file_size > static_cast<std::uintmax_t>(std::numeric_limits<std::size_t>::max())) {
        throw std::runtime_error("Replay bundle size is outside supported host limits.");
    }

    LoadedBundle bundle;
    bundle.bytes.resize(static_cast<std::size_t>(file_size));
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("Could not open replay bundle.");
    }
    input.read(
        reinterpret_cast<char*>(bundle.bytes.data()),
        static_cast<std::streamsize>(bundle.bytes.size()));
    if (!input || input.gcount() != static_cast<std::streamsize>(bundle.bytes.size())) {
        throw std::runtime_error("Could not read complete replay bundle.");
    }

    if (!sampled_values::decode_replay_bundle_header(
            std::span<const std::uint8_t>{bundle.bytes.data(), bundle.bytes.size()},
            bundle.header)) {
        throw std::runtime_error("Replay bundle header is invalid.");
    }
    if (bundle.header.sample_rate_hz != 4'000U) {
        throw std::runtime_error("Replay live v1 requires a 4000 Hz bundle.");
    }
    if (bundle.header.frame_count == 0U) {
        throw std::runtime_error("Replay bundle contains no frames.");
    }
    const auto expected = sampled_values::replay_bundle_expected_bytes(bundle.header);
    if (expected == 0U || expected != static_cast<std::uint64_t>(bundle.bytes.size())) {
        throw std::runtime_error("Replay bundle byte count does not match its header.");
    }
    return bundle;
}

#ifdef _WIN32
[[nodiscard]] sampled_values::SampledValuesFrame make_frame() {
    sampled_values::SampledValueAsdu asdu;
    asdu.sv_id = "ARSTACK61850_INJECTOR";
    asdu.data_set_reference = "ARSTACK61850/LLN0$PhsMeas1";
    asdu.configuration_revision = 1U;
    asdu.sample_synchronization = 0U;
    asdu.sample_rate = std::uint16_t{4'000U};
    asdu.sample_mode = std::uint16_t{1U};
    asdu.sample_payload.resize(sampled_values::replay_payload_bytes, 0U);

    return {
        kDestination,
        kSource,
        std::nullopt,
        0x4001U,
        0U,
        0U,
        sampled_values::SampledValuesPdu{{std::move(asdu)}}};
}
#endif

void print_bundle_summary(
    const std::filesystem::path& path,
    const LoadedBundle& bundle) {
    const auto duration = static_cast<double>(bundle.header.frame_count) /
        static_cast<double>(bundle.header.sample_rate_hz);
    std::cout
        << "Replay bundle      : " << path.string() << '\n'
        << "Sample rate        : " << bundle.header.sample_rate_hz << " Hz\n"
        << "Frames             : " << bundle.header.frame_count << '\n'
        << "Payload/frame      : " << bundle.header.payload_bytes << " bytes\n"
        << "Duration           : " << std::fixed << std::setprecision(6)
        << duration << " s\n";
}

void list_adapters() {
    tools::NpcapLivePort live;
    std::vector<tools::LiveEthernetAdapter> adapters;
    if (!live.list_adapters(adapters)) {
        throw std::runtime_error(live.error());
    }
    std::cout << "Npcap adapters:\n";
    for (std::size_t index = 0U; index < adapters.size(); ++index) {
        std::cout << '[' << index << "] "
                  << (adapters[index].description.empty()
                      ? "(no description)"
                      : adapters[index].description)
                  << '\n'
                  << "    " << adapters[index].name << '\n';
    }
}

#ifdef _WIN32
[[nodiscard]] std::uint64_t monotonic_us() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void wait_until(const std::uint64_t deadline_us) {
    while (gStopRequested == 0) {
        const auto now = monotonic_us();
        if (now >= deadline_us) {
            return;
        }
        const auto remaining = deadline_us - now;
        if (remaining > 2'000U) {
            std::this_thread::sleep_for(
                std::chrono::microseconds{static_cast<std::int64_t>(remaining - 1'000U)});
        } else if (remaining > 100U) {
            std::this_thread::yield();
        }
    }
}
#endif

[[nodiscard]] int run_live(const Options& options, const LoadedBundle& bundle) {
#ifndef _WIN32
    (void)options;
    (void)bundle;
    throw std::runtime_error("Live Npcap replay is available only on Windows.");
#else
    tools::NpcapLivePort live;
    if (!live.open(options.interface_selector)) {
        throw std::runtime_error(live.error());
    }

    auto frame = make_frame();
    std::array<std::uint8_t, kFrameBufferBytes> frame_buffer{};
    auto raw_port = live.raw_port();
    sampled_values::SampledValuesPublisher publisher(
        frame,
        frame_buffer,
        raw_port,
        sampled_values::SampledValuesPublisherConfig{
            bundle.header.sample_rate_hz,
            std::uint16_t{kSampleCountWrap},
            0U,
            true});
    if (!publisher.valid()) {
        throw std::runtime_error("SV replay publisher configuration is invalid.");
    }

    gStopRequested = 0;
    const auto previous_handler = std::signal(SIGINT, &handle_interrupt);
    const auto started_at = std::chrono::steady_clock::now();
    std::uint64_t logical_index{};

    while (gStopRequested == 0 &&
           (options.continuous || logical_index < bundle.header.frame_count)) {
        if (publisher.started()) {
            wait_until(publisher.next_due_us());
            if (gStopRequested != 0) {
                break;
            }
        }

        const auto replay_index = logical_index % bundle.header.frame_count;
        const auto payload = bundle.payload(replay_index);
        auto& destination = frame.pdu.asdus.front().sample_payload;
        std::copy(payload.begin(), payload.end(), destination.begin());

        sampled_values::SampledValuesPublishResult result{};
        do {
            result = publisher.poll(monotonic_us());
            if (result.status == sampled_values::SampledValuesPublishStatus::not_due) {
                wait_until(result.next_due_us);
            }
        } while (result.status == sampled_values::SampledValuesPublishStatus::not_due &&
                 gStopRequested == 0);

        if (gStopRequested != 0) {
            break;
        }
        if (!result.sent()) {
            std::signal(SIGINT, previous_handler);
            throw std::runtime_error(
                result.status == sampled_values::SampledValuesPublishStatus::transmit_failed
                    ? "Npcap replay transmit failed: " + live.error()
                    : "Replay publisher failed to encode/transmit a frame.");
        }

        const auto expected_count = static_cast<std::uint16_t>(
            logical_index % static_cast<std::uint64_t>(kSampleCountWrap));
        if (result.sample_count != expected_count) {
            std::signal(SIGINT, previous_handler);
            throw std::runtime_error("Replay smpCnt diverged from logical sample index.");
        }
        ++logical_index;
    }

    const auto stopped_at = std::chrono::steady_clock::now();
    std::signal(SIGINT, previous_handler);
    const auto elapsed = std::chrono::duration<double>(stopped_at - started_at).count();
    const auto& stats = publisher.statistics();
    const auto rate = elapsed > 0.0
        ? static_cast<double>(stats.frames_sent) / elapsed
        : 0.0;
    const auto passed = stats.encode_failures == 0U &&
        stats.transmit_failures == 0U &&
        (options.continuous || stats.frames_sent == bundle.header.frame_count);

    std::cout
        << "\nARStack61850 recorded-waveform live replay\n\n"
        << "Adapter            : " << live.opened_name() << '\n'
        << "Frames sent        : " << stats.frames_sent << '\n'
        << "Encode failures    : " << stats.encode_failures << '\n'
        << "TX failures        : " << stats.transmit_failures << '\n'
        << "Late polls         : " << stats.late_polls << '\n'
        << "Max lateness       : " << stats.maximum_lateness_us << " us\n"
        << "Observed TX rate   : " << std::fixed << std::setprecision(1)
        << rate << " frames/s\n"
        << "RESULT             : "
        << (passed
            ? (options.continuous ? "REPLAY-LIVE/STOPPED" : "REPLAY-LIVE/PASSED")
            : "REPLAY-LIVE/FAILED")
        << '\n';
    return passed ? 0 : 2;
#endif
}

} // namespace

int main(const int argc, char** argv) {
    try {
        const auto options = parse_options(argc, argv);
        if (options.help) {
            print_usage();
            return 0;
        }
        if (options.list_interfaces) {
            list_adapters();
            return 0;
        }

        const auto bundle = load_bundle(options.bundle_path);
        print_bundle_summary(options.bundle_path, bundle);
        if (options.validate) {
            std::cout << "RESULT             : REPLAY-BUNDLE/VALID\n";
            return 0;
        }
        return run_live(options, bundle);
    } catch (const std::exception& error) {
        std::cerr << "ariec61850_sv_replay_live: " << error.what() << '\n';
        return 1;
    }
}
