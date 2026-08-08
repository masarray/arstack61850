// SPDX-License-Identifier: GPL-3.0-or-later

#include "ariec61850/capture/pcap.hpp"
#include "ariec61850/embedded/io.hpp"
#include "ariec61850/ethernet/ethernet.hpp"
#include "ariec61850/sampled_values/deterministic_injector.hpp"
#include "ariec61850/sampled_values/frame.hpp"
#include "ariec61850/sampled_values/frame_codec.hpp"
#include "ariec61850/sampled_values/injector_presets.hpp"
#include "ariec61850/sampled_values/payload_writer.hpp"
#include "ariec61850/sampled_values/publisher.hpp"
#include "npcap_live.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
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
#include <vector>

namespace {

using namespace ar::iec61850;

constexpr std::uint32_t kSampleRateHz = 4'000U;
constexpr std::uint64_t kIntervalUs = 250U;
constexpr std::uint16_t kSampleCountWrap = 4'000U;
constexpr std::uint64_t kVirtualStartUs = 1'000'000U;
constexpr std::size_t kPayloadBytes =
    sampled_values::injector_channel_count *
    sampled_values::SampledValuesPayloadWriter::int32_quality_pair_bytes;

const ethernet::MacAddress kDestination{
    std::array<std::uint8_t, 6U>{0x01U, 0x0CU, 0xCDU, 0x04U, 0x00U, 0x01U}};
const ethernet::MacAddress kSource{
    std::array<std::uint8_t, 6U>{0x02U, 0x00U, 0x00U, 0x00U, 0x00U, 0x02U}};

#ifdef _WIN32
volatile std::sig_atomic_t gStopRequested = 0;

void handle_interrupt(const int signal) noexcept {
    if (signal == SIGINT) {
        gStopRequested = 1;
    }
}
#endif

enum class RunMode : std::uint8_t {
    loopback,
    live,
};

struct Options final {
    RunMode mode{RunMode::loopback};
    std::string scenario{"normal"};
    std::optional<std::uint64_t> frames;
    std::uint64_t drop_every{};
    std::string pcap_path;
    std::string interface_selector;
    bool continuous{};
    bool list_interfaces{};
    bool json{};
    bool capabilities{};
    bool help{};
};

struct ScenarioDefinition final {
    std::vector<sampled_values::InjectorScenarioSegment> segments;
    std::uint64_t default_frames{};
    bool loop{true};
};

struct LoopbackEvidence final {
    std::uint64_t tx_frames{};
    std::uint64_t rx_frames{};
    std::uint64_t injected_drops{};
    std::uint64_t decode_errors{};
    std::uint64_t identity_errors{};
    std::uint64_t payload_errors{};
    std::uint64_t sample_count_errors{};
    std::uint64_t gap_events{};
    std::uint64_t pcap_errors{};
};

struct LoopbackContext final {
    LoopbackEvidence evidence{};
    std::uint64_t drop_every{};
    std::uint16_t expected_sample_count{};
    std::array<std::uint8_t, kPayloadBytes> expected_payload{};
    std::optional<std::uint16_t> previous_received_count;
    std::uint64_t virtual_timestamp_us{};
    capture::PcapWriter* pcap_writer{};
};

[[nodiscard]] std::uint64_t parse_bounded_u64(
    const std::string_view option,
    const std::string& value,
    const std::uint64_t minimum,
    const std::uint64_t maximum) {
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed < minimum || parsed > maximum) {
        throw std::invalid_argument(
            std::string{option} + " requires an integer in range " +
            std::to_string(minimum) + ".." + std::to_string(maximum) + '.');
    }
    return static_cast<std::uint64_t>(parsed);
}

[[nodiscard]] RunMode parse_mode(const std::string_view value) {
    if (value == "loopback") {
        return RunMode::loopback;
    }
    if (value == "live") {
        return RunMode::live;
    }
    throw std::invalid_argument("--mode must be loopback or live.");
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

        if (argument == "--mode") {
            options.mode = parse_mode(require_value("--mode"));
        } else if (argument == "--scenario") {
            options.scenario = require_value("--scenario");
        } else if (argument == "--frames") {
            options.frames = parse_bounded_u64(
                "--frames", require_value("--frames"), 1U, 10'000'000U);
        } else if (argument == "--drop-every") {
            options.drop_every = parse_bounded_u64(
                "--drop-every", require_value("--drop-every"), 0U, 10'000'000U);
            if (options.drop_every == 1U) {
                throw std::invalid_argument(
                    "--drop-every must be 0 (disabled) or at least 2.");
            }
        } else if (argument == "--pcap") {
            options.pcap_path = require_value("--pcap");
        } else if (argument == "--interface") {
            options.interface_selector = require_value("--interface");
        } else if (argument == "--continuous") {
            options.continuous = true;
        } else if (argument == "--list-interfaces") {
            options.list_interfaces = true;
        } else if (argument == "--json") {
            options.json = true;
        } else if (argument == "--capabilities") {
            options.capabilities = true;
        } else if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else {
            throw std::invalid_argument("Unknown option: " + argument);
        }
    }

    if (options.continuous && options.frames.has_value()) {
        throw std::invalid_argument("--continuous and --frames are mutually exclusive.");
    }
    if (options.mode == RunMode::loopback && options.continuous) {
        throw std::invalid_argument(
            "--continuous is reserved for --mode live; loopback remains finite for regression safety.");
    }
    if (options.mode == RunMode::loopback && !options.interface_selector.empty()) {
        throw std::invalid_argument("--interface requires --mode live.");
    }
    if (options.mode == RunMode::live && options.drop_every != 0U) {
        throw std::invalid_argument(
            "--drop-every currently applies only to software loopback.");
    }
    return options;
}

void print_usage() {
    std::cout
        << "Usage: ariec61850_sv_injector [options]\n"
        << "\nDeterministic IEC 61850 Sampled Values injector.\n"
        << "Loopback is the default reference/oracle mode. Windows live mode uses\n"
        << "Npcap to transmit raw SV Ethernet frames on a selected NIC.\n\n"
        << "Options:\n"
        << "  --mode MODE         loopback | live (default loopback).\n"
        << "  --scenario NAME     normal | protection-fault (default normal).\n"
        << "  --frames N          Finite frame count; defaults to scenario length.\n"
        << "  --continuous        Live mode: transmit until Ctrl+C.\n"
        << "  --interface NAME    Live mode: exact or unique Npcap adapter selector.\n"
        << "  --list-interfaces   List Npcap adapters and exit.\n"
        << "  --drop-every N      Loopback: downstream loss every N TX frames (0=off).\n"
        << "  --pcap PATH         Save observed loopback or transmitted live frames.\n"
        << "  --json              Emit machine-readable final evidence.\n"
        << "  --capabilities      Emit the app/device control capability contract.\n"
        << "  --help              Show this help.\n\n"
        << "Live examples:\n"
        << "  ariec61850_sv_injector --list-interfaces\n"
        << "  ariec61850_sv_injector --mode live --interface \"Ethernet\" --frames 8000\n"
        << "  ariec61850_sv_injector --mode live --interface \"Ethernet\" --continuous\n";
}

void print_capabilities() {
    std::cout
        << "{\n"
        << "  \"schemaVersion\": \"arstack-sv-injector-control-v1\",\n"
        << "  \"engine\": \"sample-index-fixed-point\",\n"
#ifdef _WIN32
        << "  \"modes\": [\"software-loopback\", \"windows-npcap-live\"],\n"
#else
        << "  \"modes\": [\"software-loopback\"],\n"
#endif
        << "  \"liveTiming\": \"best-effort-no-catch-up\",\n"
        << "  \"scenarios\": [\"normal\", \"protection-fault\"],\n"
        << "  \"commandsPlannedForDeviceTransport\": "
           "[\"capabilities\", \"configure\", \"arm\", \"start\", \"stop\", \"status\", \"stats\"],\n"
        << "  \"profile\": {\"sampleRateHz\": 4000, \"sampleCountWrap\": 4000, "
           "\"channels\": 8, \"payload\": \"INT32+quality\"}\n"
        << "}\n";
}

[[nodiscard]] ScenarioDefinition make_scenario(const std::string_view name) {
    auto balanced = sampled_values::make_balanced_4i4v_profile();
    if (name == "normal") {
        return {{sampled_values::make_hold_segment(balanced)}, 8'000U, true};
    }

    if (name == "protection-fault") {
        auto fault = balanced;
        for (std::size_t channel = 0U; channel < 3U; ++channel) {
            fault[channel].rms_counts *= 4;
        }
        for (std::size_t channel = 4U; channel < 7U; ++channel) {
            fault[channel].rms_counts /= 4;
        }

        std::vector<sampled_values::InjectorScenarioSegment> segments;
        segments.reserve(3U);
        segments.push_back(sampled_values::make_hold_segment(balanced, 2'000U));
        segments.push_back(sampled_values::make_hold_segment(fault, 800U));
        segments.push_back(sampled_values::make_hold_segment(balanced, 2'000U));
        return {std::move(segments), 4'800U, true};
    }

    throw std::invalid_argument(
        "Unknown scenario '" + std::string{name} +
        "'. Supported: normal, protection-fault.");
}

[[nodiscard]] sampled_values::SampledValuesFrame make_frame() {
    sampled_values::SampledValueAsdu asdu;
    asdu.sv_id = "ARSTACK61850_INJECTOR";
    asdu.data_set_reference = "ARSTACK61850/LLN0$PhsMeas1";
    asdu.configuration_revision = 1U;
    asdu.sample_synchronization = 0U;
    asdu.sample_rate = std::uint16_t{kSampleRateHz};
    asdu.sample_mode = std::uint16_t{1U};
    asdu.sample_payload.resize(kPayloadBytes, 0U);

    return {
        kDestination,
        kSource,
        std::nullopt,
        0x4001U,
        0U,
        0U,
        sampled_values::SampledValuesPdu{{std::move(asdu)}}};
}

[[nodiscard]] bool frame_identity_matches(
    const sampled_values::SampledValuesFrame& frame) noexcept {
    if (frame.destination != kDestination || frame.source != kSource ||
        frame.vlan.has_value() || frame.app_id != 0x4001U ||
        frame.reserved1 != 0U || frame.reserved2 != 0U ||
        frame.pdu.asdus.size() != 1U) {
        return false;
    }

    const auto& asdu = frame.pdu.asdus.front();
    return asdu.sv_id == "ARSTACK61850_INJECTOR" &&
        asdu.data_set_reference == "ARSTACK61850/LLN0$PhsMeas1" &&
        asdu.configuration_revision == 1U &&
        asdu.sample_synchronization == 0U &&
        asdu.sample_rate == std::optional<std::uint16_t>{std::uint16_t{kSampleRateHz}} &&
        asdu.sample_mode == std::optional<std::uint16_t>{std::uint16_t{1U}};
}

embedded::IoResult loopback_transmit(
    void* context,
    const std::span<const std::uint8_t> bytes) noexcept {
    auto* loopback = static_cast<LoopbackContext*>(context);
    if (loopback == nullptr || bytes.empty()) {
        return {embedded::IoStatus::invalid_argument, 0U};
    }

    ++loopback->evidence.tx_frames;
    if (loopback->drop_every > 0U &&
        (loopback->evidence.tx_frames % loopback->drop_every) == 0U) {
        ++loopback->evidence.injected_drops;
        return {embedded::IoStatus::ok, bytes.size()};
    }

    sampled_values::SampledValuesFrame decoded;
    if (!sampled_values::SampledValuesFrameCodec::try_decode(bytes, decoded)) {
        ++loopback->evidence.decode_errors;
        return {embedded::IoStatus::ok, bytes.size()};
    }

    ++loopback->evidence.rx_frames;
    if (!frame_identity_matches(decoded)) {
        ++loopback->evidence.identity_errors;
    }

    if (decoded.pdu.asdus.size() == 1U) {
        const auto& asdu = decoded.pdu.asdus.front();
        if (asdu.sample_count != loopback->expected_sample_count) {
            ++loopback->evidence.sample_count_errors;
        }
        if (asdu.sample_payload.size() != loopback->expected_payload.size() ||
            !std::equal(
                asdu.sample_payload.begin(),
                asdu.sample_payload.end(),
                loopback->expected_payload.begin(),
                loopback->expected_payload.end())) {
            ++loopback->evidence.payload_errors;
        }

        if (loopback->previous_received_count.has_value()) {
            const auto previous = *loopback->previous_received_count;
            const auto expected_next = static_cast<std::uint16_t>(
                previous + 1U == kSampleCountWrap ? 0U : previous + 1U);
            if (asdu.sample_count != expected_next) {
                if (asdu.sample_count == loopback->expected_sample_count) {
                    ++loopback->evidence.gap_events;
                } else {
                    ++loopback->evidence.sample_count_errors;
                }
            }
        }
        loopback->previous_received_count = asdu.sample_count;
    }

    if (loopback->pcap_writer != nullptr) {
        try {
            const auto timestamp = std::chrono::system_clock::time_point{
                std::chrono::microseconds{loopback->virtual_timestamp_us}};
            loopback->pcap_writer->write_packet(timestamp, bytes);
        } catch (...) {
            ++loopback->evidence.pcap_errors;
            return {embedded::IoStatus::io_error, 0U};
        }
    }

    return {embedded::IoStatus::ok, bytes.size()};
}

[[nodiscard]] bool write_payload(
    sampled_values::SampledValueAsdu& asdu,
    const sampled_values::InjectorSample& sample) noexcept {
    if (asdu.sample_payload.size() != kPayloadBytes) {
        return false;
    }
    const auto payload = std::span<std::uint8_t>{
        asdu.sample_payload.data(), asdu.sample_payload.size()};
    for (std::size_t channel = 0U;
         channel < sampled_values::injector_channel_count;
         ++channel) {
        if (!sampled_values::SampledValuesPayloadWriter::write_int32_quality_pair(
                payload,
                channel,
                sample.values[channel],
                sample.qualities[channel])) {
            return false;
        }
    }
    return true;
}

#ifdef _WIN32
[[nodiscard]] std::uint64_t monotonic_us() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

void wait_until_monotonic_us(const std::uint64_t deadline_us) {
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

void print_loopback_human(
    const Options& options,
    const std::uint64_t frames,
    const LoopbackEvidence& evidence,
    const sampled_values::SampledValuesPublisherStatistics& publisher,
    const bool passed) {
    std::cout
        << "ARStack61850 deterministic SV injector\n\n"
        << "Mode              : SOFTWARE LOOPBACK\n"
        << "Scenario          : " << options.scenario << '\n'
        << "Engine            : sample-index fixed-point\n"
        << "Sample rate       : " << kSampleRateHz << " Hz\n"
        << "Virtual interval  : " << kIntervalUs << " us\n"
        << "Frames requested  : " << frames << "\n\n"
        << "TX frames         : " << evidence.tx_frames << '\n'
        << "RX frames         : " << evidence.rx_frames << '\n'
        << "Injected drops    : " << evidence.injected_drops << '\n'
        << "Observed gaps     : " << evidence.gap_events << '\n'
        << "Decode errors     : " << evidence.decode_errors << '\n'
        << "Identity errors   : " << evidence.identity_errors << '\n'
        << "Payload errors    : " << evidence.payload_errors << '\n'
        << "smpCnt errors     : " << evidence.sample_count_errors << '\n'
        << "Encode failures   : " << publisher.encode_failures << '\n'
        << "TX failures       : " << publisher.transmit_failures << '\n'
        << "Late virtual poll : " << publisher.late_polls << '\n';
    if (!options.pcap_path.empty()) {
        std::cout << "PCAP              : " << options.pcap_path << '\n';
    }
    std::cout << "\nRESULT            : "
              << (passed ? "LOOPBACK/PASSED" : "LOOPBACK/FAILED") << '\n';
}

void print_loopback_json(
    const Options& options,
    const std::uint64_t frames,
    const LoopbackEvidence& evidence,
    const sampled_values::SampledValuesPublisherStatistics& publisher,
    const bool passed) {
    std::cout
        << '{'
        << "\"schemaVersion\":\"arstack-sv-injector-evidence-v1\","
        << "\"status\":\"" << (passed ? "LOOPBACK/PASSED" : "LOOPBACK/FAILED") << "\","
        << "\"mode\":\"software-loopback\","
        << "\"scenario\":\"" << options.scenario << "\","
        << "\"engine\":\"sample-index-fixed-point\","
        << "\"sampleRateHz\":" << kSampleRateHz << ','
        << "\"intervalUs\":" << kIntervalUs << ','
        << "\"framesRequested\":" << frames << ','
        << "\"txFrames\":" << evidence.tx_frames << ','
        << "\"rxFrames\":" << evidence.rx_frames << ','
        << "\"injectedDrops\":" << evidence.injected_drops << ','
        << "\"gapEvents\":" << evidence.gap_events << ','
        << "\"decodeErrors\":" << evidence.decode_errors << ','
        << "\"identityErrors\":" << evidence.identity_errors << ','
        << "\"payloadErrors\":" << evidence.payload_errors << ','
        << "\"sampleCountErrors\":" << evidence.sample_count_errors << ','
        << "\"encodeFailures\":" << publisher.encode_failures << ','
        << "\"transmitFailures\":" << publisher.transmit_failures << ','
        << "\"lateVirtualPolls\":" << publisher.late_polls << ','
        << "\"pcapErrors\":" << evidence.pcap_errors
        << "}\n";
}

[[nodiscard]] int run_loopback(const Options& options) {
    auto scenario = make_scenario(options.scenario);
    const auto frames = options.frames.value_or(scenario.default_frames);
    sampled_values::DeterministicSvInjector injector(
        scenario.segments, kSampleRateHz, scenario.loop);
    if (!injector.valid()) {
        throw std::runtime_error("Deterministic injector configuration is invalid.");
    }

    std::ofstream pcap_output;
    std::optional<capture::PcapWriter> pcap_writer;
    if (!options.pcap_path.empty()) {
        pcap_output.open(options.pcap_path, std::ios::binary | std::ios::trunc);
        if (!pcap_output) {
            throw std::runtime_error("Cannot open PCAP output: " + options.pcap_path);
        }
        pcap_writer.emplace(pcap_output);
    }

    LoopbackContext loopback;
    loopback.drop_every = options.drop_every;
    loopback.pcap_writer = pcap_writer.has_value() ? &*pcap_writer : nullptr;
    embedded::RawEthernetPort port{&loopback, &loopback_transmit};

    auto frame = make_frame();
    std::array<std::uint8_t, 1'536U> frame_buffer{};
    sampled_values::SampledValuesPublisher publisher(
        frame,
        frame_buffer,
        port,
        sampled_values::SampledValuesPublisherConfig{
            kSampleRateHz,
            std::uint16_t{kSampleCountWrap},
            0U,
            true});
    if (!publisher.valid()) {
        throw std::runtime_error("SampledValuesPublisher configuration is invalid.");
    }

    sampled_values::InjectorSample sample{};
    for (std::uint64_t index = 0U; index < frames; ++index) {
        if (!injector.step(sample)) {
            throw std::runtime_error("Scenario finished before requested frame count.");
        }
        if (sample.sample_index != index) {
            throw std::runtime_error("Injector sample-index timeline diverged.");
        }

        auto& asdu = frame.pdu.asdus.front();
        if (!write_payload(asdu, sample)) {
            throw std::runtime_error("Could not build INT32+quality SV payload.");
        }

        loopback.expected_sample_count = static_cast<std::uint16_t>(
            sample.sample_index % static_cast<std::uint64_t>(kSampleCountWrap));
        std::copy(
            asdu.sample_payload.begin(),
            asdu.sample_payload.end(),
            loopback.expected_payload.begin());
        loopback.virtual_timestamp_us =
            kVirtualStartUs + sample.sample_index * kIntervalUs;

        const auto result = publisher.poll(loopback.virtual_timestamp_us);
        if (!result.sent() ||
            result.sample_count != loopback.expected_sample_count) {
            throw std::runtime_error("Publisher did not emit the expected virtual sample.");
        }
    }

    if (pcap_output.is_open()) {
        pcap_output.flush();
        if (!pcap_output) {
            ++loopback.evidence.pcap_errors;
        }
    }

    const auto expected_rx = frames - loopback.evidence.injected_drops;
    const auto expected_visible_gaps = options.drop_every == 0U
        ? 0U
        : (frames - 1U) / options.drop_every;
    const auto& statistics = publisher.statistics();
    const auto passed =
        loopback.evidence.tx_frames == frames &&
        loopback.evidence.rx_frames == expected_rx &&
        loopback.evidence.decode_errors == 0U &&
        loopback.evidence.identity_errors == 0U &&
        loopback.evidence.payload_errors == 0U &&
        loopback.evidence.sample_count_errors == 0U &&
        loopback.evidence.gap_events == expected_visible_gaps &&
        loopback.evidence.pcap_errors == 0U &&
        statistics.frames_sent == frames &&
        statistics.encode_failures == 0U &&
        statistics.transmit_failures == 0U &&
        statistics.late_polls == 0U;

    if (options.json) {
        print_loopback_json(options, frames, loopback.evidence, statistics, passed);
    } else {
        print_loopback_human(options, frames, loopback.evidence, statistics, passed);
    }
    return passed ? 0 : 2;
}

void print_adapters() {
    tools::NpcapLivePort live;
    std::vector<tools::LiveEthernetAdapter> adapters;
    if (!live.list_adapters(adapters)) {
        throw std::runtime_error(live.error());
    }

    std::cout << "Npcap adapters:\n";
    for (std::size_t index = 0U; index < adapters.size(); ++index) {
        const auto& adapter = adapters[index];
        std::cout << '[' << index << "] "
                  << (adapter.description.empty() ? "(no description)" : adapter.description)
                  << '\n'
                  << "    " << adapter.name << '\n';
    }
    if (adapters.empty()) {
        std::cout << "(none)\n";
    }
}

#ifdef _WIN32
void print_live_human(
    const Options& options,
    const tools::NpcapLivePort& live,
    const std::uint64_t frames_requested,
    const sampled_values::SampledValuesPublisherStatistics& statistics,
    const double elapsed_seconds,
    const bool passed) {
    const auto rate = elapsed_seconds > 0.0
        ? static_cast<double>(statistics.frames_sent) / elapsed_seconds
        : 0.0;

    std::cout
        << "\nARStack61850 deterministic SV injector\n\n"
        << "Mode              : LIVE ETHERNET / NPCAP\n"
        << "Scenario          : " << options.scenario << '\n'
        << "Engine            : sample-index fixed-point\n"
        << "Timing            : real-time best-effort, no catch-up bursts\n"
        << "Sample rate goal  : " << kSampleRateHz << " Hz\n"
        << "Interval goal     : " << kIntervalUs << " us\n"
        << "Adapter           : " << live.opened_name() << '\n';
    if (!live.opened_description().empty()) {
        std::cout << "Adapter desc      : " << live.opened_description() << '\n';
    }
    if (options.continuous) {
        std::cout << "Run length        : continuous until Ctrl+C\n";
    } else {
        std::cout << "Frames requested  : " << frames_requested << '\n';
    }
    std::cout
        << "\nTX frames         : " << statistics.frames_sent << '\n'
        << "Encode failures   : " << statistics.encode_failures << '\n'
        << "TX failures       : " << statistics.transmit_failures << '\n'
        << "Late polls        : " << statistics.late_polls << '\n'
        << "Max lateness      : " << statistics.maximum_lateness_us << " us\n"
        << "Elapsed           : " << std::fixed << std::setprecision(3)
        << elapsed_seconds << " s\n"
        << "Observed TX rate  : " << std::fixed << std::setprecision(1)
        << rate << " frames/s\n";
    if (!options.pcap_path.empty()) {
        std::cout << "TX PCAP           : " << options.pcap_path << '\n';
    }
    std::cout
        << "External verify   : subscriber/Wireshark (eth.type == 0x88ba)\n"
        << "\nRESULT            : "
        << (passed
            ? (options.continuous ? "LIVE/STOPPED" : "LIVE/PASSED")
            : "LIVE/FAILED")
        << '\n';
}

void print_live_json(
    const Options& options,
    const tools::NpcapLivePort& live,
    const std::uint64_t frames_requested,
    const sampled_values::SampledValuesPublisherStatistics& statistics,
    const double elapsed_seconds,
    const bool passed) {
    const auto rate = elapsed_seconds > 0.0
        ? static_cast<double>(statistics.frames_sent) / elapsed_seconds
        : 0.0;
    std::cout
        << '{'
        << "\"schemaVersion\":\"arstack-sv-injector-live-evidence-v1\","
        << "\"status\":\""
        << (passed
            ? (options.continuous ? "LIVE/STOPPED" : "LIVE/PASSED")
            : "LIVE/FAILED")
        << "\","
        << "\"mode\":\"windows-npcap-live\","
        << "\"scenario\":\"" << options.scenario << "\","
        << "\"engine\":\"sample-index-fixed-point\","
        << "\"timing\":\"best-effort-no-catch-up\","
        << "\"sampleRateHz\":" << kSampleRateHz << ','
        << "\"intervalUs\":" << kIntervalUs << ','
        << "\"continuous\":" << (options.continuous ? "true" : "false") << ','
        << "\"framesRequested\":" << frames_requested << ','
        << "\"txFrames\":" << statistics.frames_sent << ','
        << "\"encodeFailures\":" << statistics.encode_failures << ','
        << "\"transmitFailures\":" << statistics.transmit_failures << ','
        << "\"latePolls\":" << statistics.late_polls << ','
        << "\"maximumLatenessUs\":" << statistics.maximum_lateness_us << ','
        << "\"elapsedSeconds\":" << std::fixed << std::setprecision(6)
        << elapsed_seconds << ','
        << "\"observedTxRate\":" << std::fixed << std::setprecision(3) << rate << ','
        << "\"adapter\":\"" << live.opened_name() << "\""
        << "}\n";
}
#endif

[[nodiscard]] int run_live(const Options& options) {
#ifndef _WIN32
    (void)options;
    throw std::runtime_error("Live Npcap mode is available only on Windows.");
#else
    if (options.interface_selector.empty()) {
        throw std::invalid_argument(
            "--mode live requires --interface NAME. Use --list-interfaces first.");
    }

    tools::NpcapLivePort live;
    if (!live.open(options.interface_selector)) {
        throw std::runtime_error(live.error());
    }

    auto scenario = make_scenario(options.scenario);
    const auto frames_requested = options.continuous
        ? 0U
        : options.frames.value_or(scenario.default_frames);
    sampled_values::DeterministicSvInjector injector(
        scenario.segments, kSampleRateHz, scenario.loop);
    if (!injector.valid()) {
        throw std::runtime_error("Deterministic injector configuration is invalid.");
    }

    std::ofstream pcap_output;
    std::optional<capture::PcapWriter> pcap_writer;
    if (!options.pcap_path.empty()) {
        pcap_output.open(options.pcap_path, std::ios::binary | std::ios::trunc);
        if (!pcap_output) {
            throw std::runtime_error("Cannot open PCAP output: " + options.pcap_path);
        }
        pcap_writer.emplace(pcap_output);
    }

    auto frame = make_frame();
    std::array<std::uint8_t, 1'536U> frame_buffer{};
    sampled_values::SampledValuesPublisher publisher(
        frame,
        frame_buffer,
        live.raw_port(),
        sampled_values::SampledValuesPublisherConfig{
            kSampleRateHz,
            std::uint16_t{kSampleCountWrap},
            0U,
            true});
    if (!publisher.valid()) {
        throw std::runtime_error("SampledValuesPublisher configuration is invalid.");
    }

    if (!options.json) {
        std::cout
            << "Starting live SV transmission on:\n  " << live.opened_name() << '\n';
        if (!live.opened_description().empty()) {
            std::cout << "  " << live.opened_description() << '\n';
        }
        std::cout
            << "Profile: 50 Hz, 4000 samples/s, APPID 0x4001, dst 01:0C:CD:04:00:01\n";
        if (options.continuous) {
            std::cout << "Press Ctrl+C to stop cleanly.\n";
        }
    }

    gStopRequested = 0;
    const auto previous_handler = std::signal(SIGINT, handle_interrupt);
    const auto started_at = std::chrono::steady_clock::now();

    sampled_values::InjectorSample sample{};
    std::uint64_t index = 0U;
    while (gStopRequested == 0 &&
           (options.continuous || index < frames_requested)) {
        if (publisher.started()) {
            wait_until_monotonic_us(publisher.next_due_us());
            if (gStopRequested != 0) {
                break;
            }
        }

        if (!injector.step(sample)) {
            std::signal(SIGINT, previous_handler);
            throw std::runtime_error("Scenario finished before requested frame count.");
        }
        if (sample.sample_index != index) {
            std::signal(SIGINT, previous_handler);
            throw std::runtime_error("Injector sample-index timeline diverged.");
        }

        auto& asdu = frame.pdu.asdus.front();
        if (!write_payload(asdu, sample)) {
            std::signal(SIGINT, previous_handler);
            throw std::runtime_error("Could not build INT32+quality SV payload.");
        }

        sampled_values::SampledValuesPublishResult result{};
        do {
            const auto now_us = monotonic_us();
            result = publisher.poll(now_us);
            if (result.status == sampled_values::SampledValuesPublishStatus::not_due) {
                wait_until_monotonic_us(result.next_due_us);
            }
        } while (result.status == sampled_values::SampledValuesPublishStatus::not_due &&
                 gStopRequested == 0);

        if (gStopRequested != 0) {
            break;
        }
        if (!result.sent()) {
            std::signal(SIGINT, previous_handler);
            if (result.status == sampled_values::SampledValuesPublishStatus::transmit_failed) {
                throw std::runtime_error(
                    "Npcap transmit failed: " +
                    (live.error().empty() ? std::string{"unknown error"} : live.error()));
            }
            throw std::runtime_error("Live publisher failed to encode/transmit a frame.");
        }

        const auto expected_sample_count = static_cast<std::uint16_t>(
            sample.sample_index % static_cast<std::uint64_t>(kSampleCountWrap));
        if (result.sample_count != expected_sample_count) {
            std::signal(SIGINT, previous_handler);
            throw std::runtime_error("Live publisher smpCnt diverged from logical sample index.");
        }

        if (pcap_writer.has_value()) {
            try {
                pcap_writer->write_packet(
                    std::chrono::system_clock::now(),
                    std::span<const std::uint8_t>{frame_buffer.data(), result.frame_bytes});
            } catch (...) {
                std::signal(SIGINT, previous_handler);
                throw std::runtime_error("Could not write live TX PCAP evidence.");
            }
        }

        ++index;
    }

    const auto stopped_at = std::chrono::steady_clock::now();
    std::signal(SIGINT, previous_handler);

    if (pcap_output.is_open()) {
        pcap_output.flush();
        if (!pcap_output) {
            throw std::runtime_error("Could not flush live TX PCAP evidence.");
        }
    }

    const auto elapsed_seconds =
        std::chrono::duration<double>(stopped_at - started_at).count();
    const auto& statistics = publisher.statistics();
    const auto finite_complete = options.continuous || statistics.frames_sent == frames_requested;
    const auto passed = finite_complete &&
        statistics.encode_failures == 0U &&
        statistics.transmit_failures == 0U;

    if (options.json) {
        print_live_json(
            options, live, frames_requested, statistics, elapsed_seconds, passed);
    } else {
        print_live_human(
            options, live, frames_requested, statistics, elapsed_seconds, passed);
    }
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
        if (options.capabilities) {
            print_capabilities();
            return 0;
        }
        if (options.list_interfaces) {
            print_adapters();
            return 0;
        }
        return options.mode == RunMode::live
            ? run_live(options)
            : run_loopback(options);
    } catch (const std::exception& error) {
        std::cerr << "ariec61850_sv_injector: " << error.what() << '\n';
        return 1;
    }
}
