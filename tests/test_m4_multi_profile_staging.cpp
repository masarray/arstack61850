// SPDX-License-Identifier: GPL-3.0-or-later
#include "../embedded/esp32p4_smv_injector/main/multi_profile_staging.hpp"
#include "ariec61850/sampled_values/m4_three_sv_roster.hpp"
#include "ariec61850/sampled_values/m4_three_sv_binary.hpp"
#include "ariec61850/sampled_values/profile_identity.hpp"
#include "ariec61850/scl/parser.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
#define CHECK(condition) do { if (!(condition)) { \
    throw std::runtime_error(std::string{"M4 C1 CHECK failed: "} + #condition + \
                             " at " + std::to_string(__LINE__)); \
} } while (false)

using ar::esp32p4::smv::MultiSvConfigStore;
using namespace ar::iec61850::sampled_values;

std::vector<std::uint8_t> sample_v2_roster() {
    auto document = ar::iec61850::scl::SclParser{}.load(
        std::filesystem::path{ARIEC61850_SOURCE_DIR} /
        "tests/fixtures/scl/m4-three-9-2le-reference.scd");
    SvPublisherProfileCompileContext context;
    context.profile_family = SvProfileFamily::legacy_9_2le;
    context.sample_counter_modulus = 4000U;
    const auto compiled = M4ThreeSvRosterCompiler::compile(document, context);
    CHECK(compiled.ok());
    const auto& s = compiled.roster->streams;
    const MultiSvConfigStore::Profiles profiles{
        s[0].device_profile, s[1].device_profile, s[2].device_profile};
    const auto size = M4ThreeSvBinaryCodec::encoded_size(profiles);
    CHECK(size.has_value());
    CHECK(*size >= MultiSvConfigStore::min_record_bytes);
    CHECK(*size <= MultiSvConfigStore::max_record_bytes);
    std::vector<std::uint8_t> data(*size);
    CHECK(M4ThreeSvBinaryCodec::encode_into(profiles, data).success());
    return data;
}

void send_chunks(MultiSvConfigStore& store, const std::uint32_t transaction,
                 const std::vector<std::uint8_t>& data) {
    std::size_t offset = 0U;
    while (offset < data.size()) {
        const auto count = std::min(
            MultiSvConfigStore::max_chunk_bytes, data.size() - offset);
        CHECK(store.append(transaction, offset,
                           std::span<const std::uint8_t>{data}.subspan(offset, count)));
        offset += count;
    }
}

void three_stream_firmware_config_lifecycle() {
    MultiSvConfigStore store;
    const auto data = sample_v2_roster();
    CHECK(!store.configured());
    CHECK(!store.validate(1U));
    CHECK(!store.commit(1U));
    CHECK(!store.begin(0U, data.size()));
    CHECK(!store.begin(1U, MultiSvConfigStore::max_record_bytes + 1U));
    CHECK(store.begin(1U, data.size()));
    CHECK(!store.begin(2U, data.size()));
    send_chunks(store, 1U, data);
    CHECK(store.complete(1U));
    CHECK(store.validate(1U));
    CHECK(store.validated());
    CHECK(store.commit(1U));
    CHECK(!store.uploading());
    CHECK(store.configured());
    CHECK(store.generation() == 1U);
    CHECK(store.last_transaction() == 1U);
    CHECK(store.committed_bytes() == data.size());
    CHECK(store.committed_hash() ==
          sv_binary_profile_identity_hash(std::span<const std::uint8_t>{data}));
    const auto original = store.committed_profiles();
    CHECK(original[0].app_id == 0x4000U);
    CHECK(original[1].app_id == 0x4000U);
    CHECK(original[2].app_id == 0x4000U);
    CHECK(original[0].destination_mac != original[1].destination_mac);
    CHECK(original[1].destination_mac != original[2].destination_mac);
    CHECK(original[0].frame_rate_hz == 4000U);
    CHECK(original[2].sample_counter_modulus == 4000U);

    // The prior commit is untouched by an aborted, incomplete, or invalid V2.
    CHECK(!store.begin(1U, data.size())); // replay blocked
    CHECK(store.begin(2U, data.size()));
    CHECK(store.append(2U, 0U,
          std::span<const std::uint8_t>{data}.first(48U)));
    CHECK(!store.validate(2U));
    CHECK(!store.commit(2U));
    store.abort();
    CHECK(store.configured() && store.generation() == 1U);
    CHECK(store.committed_profiles() == original);

    CHECK(store.begin(3U, data.size()));
    auto corrupt = data;
    corrupt[corrupt.size() - 1U] ^= 0x01U;
    send_chunks(store, 3U, corrupt);
    CHECK(!store.validate(3U)); // aggregate CRC32
    CHECK(!store.commit(3U));
    CHECK(store.committed_profiles() == original);
    CHECK(store.generation() == 1U);
    store.abort();

    CHECK(store.begin(4U, data.size()));
    CHECK(!store.append(4U, 5U, std::span<const std::uint8_t>{data}.first(10U)));
    CHECK(!store.uploading()); // bad chunk aborts the transaction
    CHECK(store.committed_profiles() == original);

    CHECK(store.begin(5U, data.size()));
    send_chunks(store, 5U, data);
    CHECK(store.validate(5U));
    CHECK(store.commit(5U));
    CHECK(store.generation() == 2U);
    CHECK(store.committed_profiles() == original);
    CHECK(!store.begin(5U, data.size()));
    std::cout << "PASS M4-P0C1: three V2 SV profiles, ordered 48-byte chunks, "
                 "atomic commit/readback, rollback, CRC and replay protection\n";
}
} // namespace

int main() {
    try {
        three_stream_firmware_config_lifecycle();
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }
}
