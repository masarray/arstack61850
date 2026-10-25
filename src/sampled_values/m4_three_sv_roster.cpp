// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/m4_three_sv_roster.hpp"
#include "ariec61850/sampled_values/esp32p4_profile_support.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace ar::iec61850::sampled_values {
namespace {

[[nodiscard]] bool same_wire_identity(
    const CompiledSvDeviceProfile& lhs,
    const CompiledSvDeviceProfile& rhs) noexcept {
    const auto id = [](const CompiledSvDeviceProfile& p) {
        return std::string_view{p.sv_id.data(), p.sv_id_length};
    };
    return lhs.app_id == rhs.app_id &&
           lhs.destination_mac == rhs.destination_mac &&
           lhs.vlan_present == rhs.vlan_present &&
           (!lhs.vlan_present ||
            (lhs.vlan_id == rhs.vlan_id &&
             lhs.vlan_priority == rhs.vlan_priority)) &&
           lhs.transport_mode == rhs.transport_mode &&
           id(lhs) == id(rhs);
}

} // namespace

M4ThreeSvCompileResult M4ThreeSvRosterCompiler::compile(
    const scl::SclDocument& document,
    const SvPublisherProfileCompileContext& context) {
    M4ThreeSvCompileResult result;
    if (document.sampled_values_streams.size() != m4_three_sv_capacity) {
        result.errors.push_back(
            "M4.0 requires exactly three SCL SampledValueControl streams; found " +
            std::to_string(document.sampled_values_streams.size()));
        return result;
    }

    // The family is deliberate engineering intent, never inferred from a
    // filename, APPID, DataSet, or vendor metadata.
    if (context.profile_family != SvProfileFamily::legacy_9_2le &&
        context.profile_family != SvProfileFamily::iec61850_9_2) {
        result.errors.push_back(
            "Select an explicit supported IEC 61850-9-2/9-2LE profile family.");
        return result;
    }
    if (context.sample_counter_modulus !=
        std::optional<std::uint16_t>{static_cast<std::uint16_t>(4000U)}) {
        result.errors.push_back(
            "M4.0 needs an explicitly validated 4000-sample counter modulus "
            "for one shared PTP-aligned second.");
        return result;
    }

    M4ThreeSvRoster candidate;
    for (std::size_t i = 0U; i < m4_three_sv_capacity; ++i) {
        const auto& stream = document.sampled_values_streams[i];
        const std::string name = stream.ied_name + " / " + stream.control_name;
        const auto compiled = SvPublisherProfileCompiler::compile(stream, context);
        for (const auto& error : compiled.errors) {
            result.errors.push_back(name + ": " + error);
        }
        if (!compiled.ok()) continue;

        const SvPublisherProfile& p = *compiled.profile;
        if (p.timing.exact_sample_rate_hz() !=
                std::optional<std::uint64_t>{m4_three_sv_rate_hz} ||
            p.timing.exact_frame_rate_hz() !=
                std::optional<std::uint64_t>{m4_three_sv_rate_hz} ||
            p.timing.asdus_per_frame != 1U ||
            p.sample_counter_modulus != context.sample_counter_modulus ||
            !p.asdu_options.sample_synchronized ||
            p.payload_size_bytes != 64U || p.channels.size() != 16U) {
            result.errors.push_back(
                name + ": M4.0 needs synchronized 4000 Hz 4I+4V, one "
                "ASDU/frame and 64-byte canonical payload.");
            continue;
        }
        if (classify_esp32p4_sv_profile(p) != Esp32P4SvProfileSupport::ready) {
            result.errors.push_back(
                name + ": host SCL profile is not supported by the "
                "ESP32-P4 4I+4V device boundary.");
            continue;
        }

        const auto device = compile_esp32p4_device_profile(p);
        if (!device.ok()) {
            result.errors.push_back(
                name + ": failed to compile validated binary device profile.");
            for (const auto& error : device.errors) {
                result.errors.push_back(name + ": " + error);
            }
            continue;
        }

        candidate.streams[i] = M4CompiledSvStream{
            stream.ied_name, stream.control_block_reference, *device.profile};

        // The real SCD repeats APPID 0x4000 on three DIFFERENT MAC addresses;
        // APPID-only uniqueness would wrongly reject this valid lab scenario.
        for (std::size_t previous = 0U; previous < i; ++previous) {
            if (stream.ied_name == candidate.streams[previous].ied_name &&
                stream.control_block_reference ==
                    candidate.streams[previous].control_block_reference) {
                result.errors.push_back(
                    name + ": duplicate IED-qualified control block.");
            }
            if (same_wire_identity(candidate.streams[i].device_profile,
                                   candidate.streams[previous].device_profile)) {
                result.errors.push_back(
                    name + ": duplicate complete SV wire identity.");
            }
        }
    }
    if (result.errors.empty()) result.roster = std::move(candidate);
    return result;
}

} // namespace ar::iec61850::sampled_values
