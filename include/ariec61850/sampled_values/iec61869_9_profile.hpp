// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "ariec61850/sampled_values/timing_semantics.hpp"
#include "ariec61850/scl/model.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ar::iec61850::sampled_values {

enum class SvEngineeringUnit : std::uint8_t {
    ampere,
    volt,
};

struct SvExactEngineeringScale final {
    // Engineering SI units represented by one raw INT32 count.
    // Current: 1/1000 A/count. Voltage: 1/100 V/count.
    std::int64_t numerator{};
    std::uint64_t denominator{1U};
    SvEngineeringUnit unit{SvEngineeringUnit::ampere};

    friend bool operator==(const SvExactEngineeringScale&,
                           const SvExactEngineeringScale&) = default;
};

inline constexpr SvExactEngineeringScale iec61869_9_current_scale{
    1, 1000U, SvEngineeringUnit::ampere};
inline constexpr SvExactEngineeringScale iec61869_9_voltage_scale{
    1, 100U, SvEngineeringUnit::volt};

enum class Iec61869_9Quantity : std::uint8_t {
    current,
    voltage,
};

enum class Iec61869_9VariantClass : std::uint8_t {
    backward_compatible,
    preferred,
};

enum class Iec61869_9StandardBasis : std::uint8_t {
    published_2016,
};

struct Iec61869_9Variant final {
    std::uint32_t sample_rate_hz{};
    std::uint16_t asdus_per_frame{};
    std::uint16_t current_quantity_count{};
    std::uint16_t voltage_quantity_count{};
    Iec61869_9VariantClass variant_class{Iec61869_9VariantClass::backward_compatible};

    [[nodiscard]] bool preferred() const noexcept {
        return variant_class == Iec61869_9VariantClass::preferred;
    }

    friend bool operator==(const Iec61869_9Variant&, const Iec61869_9Variant&) = default;
};

struct Iec61869_9ChannelBinding final {
    std::size_t measurement_entry_index{};
    std::size_t quality_entry_index{};
    Iec61869_9Quantity quantity{Iec61869_9Quantity::current};
    SvExactEngineeringScale engineering_scale{iec61869_9_current_scale};

    friend bool operator==(const Iec61869_9ChannelBinding&,
                           const Iec61869_9ChannelBinding&) = default;
};

struct Iec61869_9Profile final {
    Iec61869_9StandardBasis standard_basis{Iec61869_9StandardBasis::published_2016};
    std::optional<Iec61869_9Variant> variant;
    std::vector<Iec61869_9ChannelBinding> channels;

    [[nodiscard]] bool variant_resolved() const noexcept {
        return variant.has_value();
    }

    friend bool operator==(const Iec61869_9Profile&, const Iec61869_9Profile&) = default;
};

struct Iec61869_9ProfileResolution final {
    Iec61869_9Profile profile;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    [[nodiscard]] bool valid_dataset() const noexcept {
        return errors.empty() && !profile.channels.empty();
    }

    [[nodiscard]] bool complete() const noexcept {
        return valid_dataset() && profile.variant_resolved();
    }
};

[[nodiscard]] inline std::string iec61869_9_variant_code(
    const Iec61869_9Variant& variant) {
    return "F" + std::to_string(variant.sample_rate_hz) +
        "S" + std::to_string(variant.asdus_per_frame) +
        "I" + std::to_string(variant.current_quantity_count) +
        "U" + std::to_string(variant.voltage_quantity_count);
}

[[nodiscard]] constexpr std::optional<SvSampleMode>
iec61869_9_published_2016_sampling_basis(
    const std::uint32_t sample_rate_hz,
    const std::uint16_t asdus_per_frame) noexcept {
    // IEC 61869-9:2016 retains per-nominal-period encoding for the
    // backward-compatible families and the 96 kHz DC workaround. Preferred
    // 4.8/14.4 kHz families use samples-per-second.
    if ((sample_rate_hz == 4800U && asdus_per_frame == 2U) ||
        (sample_rate_hz == 14400U && asdus_per_frame == 6U)) {
        return SvSampleMode::samples_per_second;
    }
    if ((sample_rate_hz == 4000U && asdus_per_frame == 1U) ||
        (sample_rate_hz == 4800U && asdus_per_frame == 1U) ||
        (sample_rate_hz == 5760U && asdus_per_frame == 1U) ||
        (sample_rate_hz == 12800U && asdus_per_frame == 8U) ||
        (sample_rate_hz == 15360U && asdus_per_frame == 8U) ||
        (sample_rate_hz == 96000U && asdus_per_frame == 1U)) {
        return SvSampleMode::samples_per_period;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr bool
iec61869_9_published_2016_configured_rate_matches(
    const Iec61869_9Variant& variant,
    const SvPublicationTiming& timing) noexcept {
    if (timing.sampling_basis == SvSampleMode::samples_per_second) {
        return timing.configured_sample_rate == variant.sample_rate_hz;
    }
    if (timing.sampling_basis != SvSampleMode::samples_per_period) return false;

    switch (variant.sample_rate_hz) {
    case 4000U:
        return variant.asdus_per_frame == 1U &&
               timing.configured_sample_rate == 80U;
    case 4800U:
        return variant.asdus_per_frame == 1U &&
               (timing.configured_sample_rate == 80U ||
                timing.configured_sample_rate == 96U);
    case 5760U:
        return variant.asdus_per_frame == 1U &&
               timing.configured_sample_rate == 96U;
    case 12800U:
        return variant.asdus_per_frame == 8U &&
               timing.configured_sample_rate == 256U;
    case 15360U:
        return variant.asdus_per_frame == 8U &&
               timing.configured_sample_rate == 256U;
    case 96000U:
        return variant.asdus_per_frame == 1U &&
               timing.configured_sample_rate == 9600U;
    default:
        return false;
    }
}

[[nodiscard]] constexpr std::optional<Iec61869_9VariantClass>
iec61869_9_variant_class(
    const std::uint32_t sample_rate_hz,
    const std::uint16_t asdus_per_frame) noexcept {
    if ((sample_rate_hz == 4000U && asdus_per_frame == 1U) ||
        (sample_rate_hz == 4800U && asdus_per_frame == 1U) ||
        (sample_rate_hz == 5760U && asdus_per_frame == 1U) ||
        (sample_rate_hz == 12800U && asdus_per_frame == 8U) ||
        (sample_rate_hz == 15360U && asdus_per_frame == 8U)) {
        return Iec61869_9VariantClass::backward_compatible;
    }
    if ((sample_rate_hz == 4800U && asdus_per_frame == 2U) ||
        (sample_rate_hz == 14400U && asdus_per_frame == 6U) ||
        (sample_rate_hz == 96000U && asdus_per_frame == 1U)) {
        return Iec61869_9VariantClass::preferred;
    }
    return std::nullopt;
}

namespace iec61869_9_detail {

[[nodiscard]] inline std::string lower_copy(const std::string_view text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        result.push_back(static_cast<char>(
            std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

[[nodiscard]] inline bool same_text(
    const std::string_view left,
    const std::string_view right) {
    return lower_copy(left) == lower_copy(right);
}

struct EntrySemantic final {
    Iec61869_9Quantity quantity{Iec61869_9Quantity::current};
    bool measurement{};
    bool quality{};
};

[[nodiscard]] inline std::optional<EntrySemantic> classify(
    const scl::SclDataSetEntry& entry) {
    const auto ln_class = lower_copy(entry.ln_class);
    const auto do_name = lower_copy(entry.do_name);
    const auto da_name = lower_copy(entry.da_name);

    if (ln_class == "tctr" && do_name == "ampsv") {
        if (da_name == "instmag.i") return EntrySemantic{Iec61869_9Quantity::current, true, false};
        if (da_name == "q") return EntrySemantic{Iec61869_9Quantity::current, false, true};
    }
    if (ln_class == "tvtr" && do_name == "volsv") {
        if (da_name == "instmag.i") return EntrySemantic{Iec61869_9Quantity::voltage, true, false};
        if (da_name == "q") return EntrySemantic{Iec61869_9Quantity::voltage, false, true};
    }
    return std::nullopt;
}

[[nodiscard]] inline bool same_data_object(
    const scl::SclDataSetEntry& measurement,
    const scl::SclDataSetEntry& quality) {
    return same_text(measurement.ied_name, quality.ied_name) &&
        same_text(measurement.ld_inst, quality.ld_inst) &&
        same_text(measurement.prefix, quality.prefix) &&
        same_text(measurement.ln_class, quality.ln_class) &&
        same_text(measurement.ln_inst, quality.ln_inst) &&
        same_text(measurement.do_name, quality.do_name);
}

} // namespace iec61869_9_detail

[[nodiscard]] inline Iec61869_9ProfileResolution resolve_iec61869_9_profile(
    const std::span<const scl::SclDataSetEntry> entries,
    const SvPublicationTiming& timing) {
    Iec61869_9ProfileResolution result;

    if (entries.empty()) {
        result.errors.emplace_back("IEC 61869-9 DataSet is empty or unresolved.");
        return result;
    }
    if ((entries.size() % 2U) != 0U) {
        result.errors.emplace_back(
            "IEC 61869-9 DataSet must contain measurement/Quality pairs; member count is odd.");
        return result;
    }

    bool voltage_seen = false;
    std::size_t current_count{};
    std::size_t voltage_count{};

    for (std::size_t index = 0U; index < entries.size(); index += 2U) {
        const auto& measurement = entries[index];
        const auto& quality = entries[index + 1U];
        const auto measurement_semantic = iec61869_9_detail::classify(measurement);
        const auto quality_semantic = iec61869_9_detail::classify(quality);

        if (!measurement_semantic.has_value() || !measurement_semantic->measurement) {
            result.errors.push_back(
                "IEC 61869-9 DataSet member " + std::to_string(index) +
                " must be TCTR.AmpSv.instMag.i or TVTR.VolSv.instMag.i.");
            continue;
        }
        if (!quality_semantic.has_value() || !quality_semantic->quality ||
            quality_semantic->quantity != measurement_semantic->quantity ||
            !iec61869_9_detail::same_data_object(measurement, quality)) {
            result.errors.push_back(
                "IEC 61869-9 measurement member " + std::to_string(index) +
                " must be followed immediately by the corresponding AmpSv.q/VolSv.q Quality.");
            continue;
        }
        if (iec61869_9_detail::lower_copy(measurement.functional_constraint) != "mx" ||
            iec61869_9_detail::lower_copy(quality.functional_constraint) != "mx" ||
            iec61869_9_detail::lower_copy(measurement.cdc) != "sav" ||
            iec61869_9_detail::lower_copy(quality.cdc) != "sav") {
            result.errors.push_back(
                "IEC 61869-9 measurement/Quality pair must resolve as SAV with functional constraint MX.");
            continue;
        }
        if (measurement.is_quality ||
            iec61869_9_detail::lower_copy(measurement.basic_type) != "int32") {
            result.errors.push_back(
                "IEC 61869-9 sampled measurement must use INT32 instMag.i representation.");
            continue;
        }
        if (!quality.is_quality ||
            iec61869_9_detail::lower_copy(quality.basic_type) != "quality") {
            result.errors.push_back(
                "IEC 61869-9 sampled measurement Quality member must use the Quality basic type.");
            continue;
        }

        if (measurement_semantic->quantity == Iec61869_9Quantity::current) {
            if (voltage_seen) {
                result.errors.emplace_back(
                    "IEC 61869-9 requires all AmpSv current pairs to precede every VolSv voltage pair.");
                continue;
            }
            ++current_count;
            result.profile.channels.push_back({
                index,
                index + 1U,
                Iec61869_9Quantity::current,
                iec61869_9_current_scale,
            });
        } else {
            voltage_seen = true;
            ++voltage_count;
            result.profile.channels.push_back({
                index,
                index + 1U,
                Iec61869_9Quantity::voltage,
                iec61869_9_voltage_scale,
            });
        }
    }

    if (!result.errors.empty()) return result;
    if (current_count == 0U && voltage_count == 0U) {
        result.errors.emplace_back("IEC 61869-9 DataSet contains no current or voltage quantities.");
        return result;
    }
    if (current_count > std::numeric_limits<std::uint16_t>::max() ||
        voltage_count > std::numeric_limits<std::uint16_t>::max()) {
        result.errors.emplace_back("IEC 61869-9 quantity count exceeds the supported profile range.");
        return result;
    }

    if (!timing.resolved()) {
        result.warnings.emplace_back(
            "IEC 61869-9 DataSet semantics are resolved, but FfSsIiUu variant identity needs resolved sample/frame timing.");
        return result;
    }
    const auto sample_rate = timing.exact_sample_rate_hz();
    if (!sample_rate.has_value()) {
        result.errors.emplace_back(
            "IEC 61869-9 variant requires an integral samples-per-second rate.");
        return result;
    }
    const auto variant_class =
        iec61869_9_variant_class(*sample_rate, timing.asdus_per_frame);
    if (!variant_class.has_value()) {
        result.errors.push_back(
            "IEC 61869-9 does not recognize the configured F" +
            std::to_string(*sample_rate) + "S" +
            std::to_string(timing.asdus_per_frame) + " sampling/packetization combination.");
        return result;
    }

    result.profile.variant = Iec61869_9Variant{
        *sample_rate,
        timing.asdus_per_frame,
        static_cast<std::uint16_t>(current_count),
        static_cast<std::uint16_t>(voltage_count),
        *variant_class,
    };
    return result;
}

} // namespace ar::iec61850::sampled_values
