// SPDX-License-Identifier: GPL-3.0-or-later
// Production Studio SCD-model integration test: no ESP32, Qt Quick, or COM port.
#include "SclProfileModel.hpp"
#include "ariec61850/sampled_values/m4_three_sv_binary.hpp"
#include "ariec61850/sampled_values/profile_identity.hpp"

#include <QCoreApplication>
#include <QUrl>
#include <array>
#include <cstdint>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <string>

namespace {
using namespace ar::iec61850::sampled_values;

void require(bool value, const char* message) {
    if (!value) throw std::runtime_error{message};
}

std::array<CompiledSvDeviceProfile, m4_three_sv_capacity>
decode(const QByteArray& bytes) {
    require(!bytes.isEmpty(), "ARM3 candidate missing");
    require(bytes.startsWith("ARM3"), "ARM3 magic missing");
    std::array<CompiledSvDeviceProfile, m4_three_sv_capacity> profiles{};
    const auto source = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size())};
    const auto result = M4ThreeSvBinaryCodec::decode(source, profiles);
    require(result.success() && result.bytes == source.size(),
            "ARM3 V2 full decode/CRC rejected");
    return profiles;
}

void test_real_scd_compiles_exact_mus() {
    const auto fixture = std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/scl/m4-three-9-2le-reference.scd";
    SclProfileModel model;
    require(model.loadFile(QUrl::fromLocalFile(
        QString::fromStdString(fixture.string()))), "SCD parse failed");
    require(!model.m4DraftSupported() && model.m4ConfigurationBinary().isEmpty(),
            "Arbitrary imported SCD must not silently infer 9-2LE");
    require(model.confirmCounterModulus(4000), "explicit shared modulus");
    for (int i = 0; i < 3; ++i) {
        model.selectStream(i);
        require(model.selectProfileFamily(QStringLiteral("9-2le")),
                "operator-selected IEC 61850 9-2LE profile family");
    }
    require(model.m4DraftSupported(), "3 exact canonical SCD profiles required");
    const auto baseline = model.m4ConfigurationBinary();
    const auto hash = model.m4ConfigurationHash();
    require(hash.size() == 16, "firmware-compatible 64-bit identity hash");
    const auto profiles = decode(baseline);
    const auto source = std::span<const std::uint8_t>{
        reinterpret_cast<const std::uint8_t*>(baseline.constData()),
        static_cast<std::size_t>(baseline.size())};
    const auto expectedHash = QString::number(
        static_cast<qulonglong>(sv_binary_profile_identity_hash(source)), 16)
        .rightJustified(16, QLatin1Char('0')).toUpper();
    require(hash == expectedHash, "firmware identity must hash entire envelope");
    const auto roster = model.m4RosterRows();
    require(roster.size() == 3, "one scenario page per source-backed MU");
    for (std::size_t i = 0; i < 3; ++i) {
        const auto& profile = profiles[i];
        require(profile.app_id == 0x4000U, "SCD repeated APPID changed");
        require(profile.destination_mac ==
            (std::array<std::uint8_t, 6>{0x01, 0x0C, 0xCD, 0x04, 0x00,
                                         static_cast<std::uint8_t>(i + 1U)}),
                "MU multicast destination changed");
        require(profile.frame_rate_hz == 4000U &&
                profile.sample_counter_modulus == 4000U &&
                profile.payload_size_bytes == 64U,
                "shared grid/canonical 64B layout changed");
        const auto expectedSv = "MU01_SV" + std::to_string(i + 1U);
        require(std::string(profile.sv_id.data(), profile.sv_id_length) == expectedSv,
                "SCD svID changed");
        require(profile.vlan_present && profile.vlan_id == 0U &&
                profile.vlan_priority == 4U,
                "SCD priority tag must survive compilation");
        const auto shown = roster[static_cast<qsizetype>(i)].toMap();
        require(shown.value(QStringLiteral("svId")).toString() ==
                QString::fromStdString(expectedSv), "UI and binary identity differ");
    }

    // Changing MU2's wire VLAN recompiles the WHOLE atomic V2 envelope;
    // sibling MU1/MU3 profiles remain byte-identical (not reindexed/recreated).
    model.selectStream(1);
    require(model.configureSelectedVlan(true, 100, 5), "MU2 VLAN override");
    require(model.m4DraftSupported(), "valid independent VLAN override");
    const auto changed = decode(model.m4ConfigurationBinary());
    require(model.m4ConfigurationHash() != hash, "wire change must change hash");
    require(changed[0] == profiles[0] && changed[2] == profiles[2],
            "MU2 wire override leaked into siblings");
    require(changed[1].vlan_present && changed[1].vlan_id == 100U &&
            changed[1].vlan_priority == 5U, "MU2 exact VLAN not compiled");

    require(model.configureSelectedVlan(true, 0, 4), "restore MU2 SCD tag");
    require(model.m4ConfigurationBinary() == baseline &&
            model.m4ConfigurationHash() == hash,
            "restored SCD identities must encode deterministically");
    require(model.selectProfileFamily(QStringLiteral("unspecified")),
            "unselect profile family");
    require(!model.m4DraftSupported() && model.m4ConfigurationBinary().isEmpty(),
            "invalid profile must invalidate cached deployment");
    require(model.selectProfileFamily(QStringLiteral("9-2le")),
            "reselect valid family");
    require(model.m4ConfigurationBinary() == baseline,
            "recompiled envelope must be identical");
    model.clear();
    require(!model.m4DraftSupported() && model.m4ConfigurationBinary().isEmpty() &&
            model.m4ConfigurationHash().isEmpty(),
            "source clear must retire stale deploy image/readback identity");
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        test_real_scd_compiles_exact_mus();
        qInfo("PASS P0E2A Studio: imported 3-MU SCD => ARM3 V2, CRC, exact "
              "wire IDs, shared 4000 grid, isolation, invalidation");
        return 0;
    } catch (const std::exception& e) {
        qCritical("FAIL P0E2A Studio: %s", e.what());
        return 1;
    }
}
