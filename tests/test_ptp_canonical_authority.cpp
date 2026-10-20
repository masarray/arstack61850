// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/time_sync/ptp_canonical_authority.hpp"
#include <cstdio>
#include <cstdlib>

using ar::iec61850::time_sync::PtpAuthorityRole;
using ar::iec61850::time_sync::PtpCanonicalAuthorityEvidence;
using ar::iec61850::time_sync::canonical_ptp_authority;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x); std::abort(); } } while(false)

int main() {
    PtpCanonicalAuthorityEvidence e{};
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.role = PtpAuthorityRole::source;
    e.runtime_ready = true;
    e.clock_usable_claim = true;
    e.globally_traceable_claim = true;
    e.generation = 0U;
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.generation = 17U;
    auto source = canonical_ptp_authority(e);
    CHECK(source.clock_usable && source.generation == 17U);
    CHECK(!source.globally_traceable && !source.sv_phase_qualified);
    CHECK(!source.selected_source);
    e.runtime_ready = false;
    CHECK(!canonical_ptp_authority(e).clock_usable);
    // Physical SOURCE task may remain RUNNING when its timestamp freshness
    // expires. UI runtime is true, but the canonical timing lease must
    // remain fail-closed until fresh evidence restores the clock.
    e.runtime_ready = true;
    e.generation = 0U;
    e.clock_usable_claim = false;
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.generation = 18U; // even a stale nonzero generation is insufficient
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.clock_usable_claim = true;
    CHECK(canonical_ptp_authority(e).clock_usable);

    e.role = PtpAuthorityRole::receiver;
    e.runtime_ready = true;
    e.source_clock_identity = {10U, 20U, 30U, 40U, 50U, 60U, 70U, 80U};
    e.source_port = 3U;
    // Stale fields left by a prior worker may not certify an active clock.
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.selected_source = true;
    e.measured_discipline_locked = false;
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.measured_discipline_locked = true;
    auto receiver = canonical_ptp_authority(e);
    CHECK(receiver.clock_usable && receiver.globally_traceable);
    CHECK(receiver.selected_source && receiver.source_port == 3U);
    CHECK(receiver.source_clock_identity[7] == 80U);
    CHECK(!receiver.sv_phase_qualified);
    e.clock_usable_claim = false;
    CHECK(!canonical_ptp_authority(e).globally_traceable);
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.clock_usable_claim = true;
    e.globally_traceable_claim = false;
    CHECK(canonical_ptp_authority(e).clock_usable);
    CHECK(!canonical_ptp_authority(e).globally_traceable);
    e.runtime_ready = false;
    receiver = canonical_ptp_authority(e);
    CHECK(!receiver.clock_usable && !receiver.selected_source);
    CHECK(receiver.source_port == 0U);
    e.runtime_ready = true;
    e.selected_source = false;
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.selected_source = true;
    e.generation = 0U;
    CHECK(!canonical_ptp_authority(e).clock_usable);
    e.generation = 18U;
    e.role = PtpAuthorityRole::monitor;
    CHECK(!canonical_ptp_authority(e).clock_usable);
    CHECK(!canonical_ptp_authority(e).globally_traceable);
    e.role = static_cast<PtpAuthorityRole>(99U);
    CHECK(!canonical_ptp_authority(e).clock_usable);
    std::puts("Canonical PTP timing authority: PASS");
}
