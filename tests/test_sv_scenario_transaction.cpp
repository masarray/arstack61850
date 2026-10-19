// SPDX-License-Identifier: GPL-3.0-or-later
#include "ariec61850/sampled_values/live_signal_transaction.hpp"
#include <cstdio>
#include <cstdint>

using namespace ar::iec61850::sampled_values;

static bool populate(SvScenarioTransaction& tx, const std::uint64_t token,
                     const SvLiveSignalBank& bank, const std::uint32_t now) {
    if (!tx.begin(token, bank.snapshot(), now)) return false;
    auto& s = tx.draft();
    s.frequency_millihz = 50000U;
    tx.mark_frequency();
    for (std::size_t i = 0; i < 8; ++i) {
        s.channels[i].rms_counts = static_cast<std::int32_t>(100 + i * 300);
        s.channels[i].phase_millidegrees = static_cast<std::int32_t>(i * 13750) - 90000;
        s.channels[i].quality = static_cast<std::uint32_t>(i + 10);
        s.channels[i].enabled = i != 3U && i != 7U;
        tx.mark_set(i);
        tx.mark_enabled(i);
    }
    s.current_shape = {true, -150, 240, 3, 850};
    tx.mark_shape();
    return true;
}

int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* name) {
        std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    {
        SvLiveSignalBank bank;
        SvScenarioTransaction tx;
        const auto before = bank.snapshot();
        check(populate(tx, 101, bank, 100), "begin valid transaction");
        const auto hash = sv_scenario_hash(tx.draft());
        check(bank.snapshot() == before, "staging never changes active publisher");
        check(tx.commit(bank, 101, hash, 105) == SvScenarioTransaction::Result::accepted,
              "complete 4I4V atomic commit");
        check(bank.snapshot().generation == before.generation + 1,
              "one generation for entire scenario");
        check(!tx.can_start(bank, 101, hash, 105), "no START before explicit readback");
        check(tx.confirm_readback(bank, 101, 105), "firmware applied-state readback");
        check(tx.readback_matches(bank, 101, 105) && tx.can_start(bank, 101, hash, 106),
              "ACK/readback and token/hash match");
        check(!tx.can_start(bank, 102, hash, 106) && !tx.can_start(bank, 101, hash ^ 1, 106),
              "stale token and altered hash refuse START");
        tx.consume_start();
        check(!tx.can_start(bank, 101, hash, 106), "token cannot replay after START");
    }
    {
        SvLiveSignalBank bank;
        SvScenarioTransaction tx;
        const auto before = bank.snapshot();
        check(tx.begin(201, before, 100), "partial transaction begin");
        tx.draft().channels[0].rms_counts = 200;
        tx.mark_set(0);
        check(tx.commit(bank, 201, sv_scenario_hash(tx.draft()), 101) ==
              SvScenarioTransaction::Result::incomplete, "partial SET/ENABLE rejected");
        check(bank.snapshot() == before, "partial commit leaves bank unchanged");
    }
    {
        SvLiveSignalBank bank;
        SvScenarioTransaction tx;
        check(populate(tx, 301, bank, 100), "checksum fixture");
        check(tx.commit(bank, 301, sv_scenario_hash(tx.draft()) ^ 1U, 110) ==
              SvScenarioTransaction::Result::checksum, "one-bit corrupted transaction rejected");
        check(bank.snapshot().generation == 0, "corrupt transaction no publish");
        check(!tx.can_start(bank, 301, 0, 111), "no START on rejected ACK");
    }
    {
        SvLiveSignalBank bank;
        SvScenarioTransaction tx;
        check(populate(tx, 401, bank, 100), "timeout fixture");
        check(tx.commit(bank, 401, sv_scenario_hash(tx.draft()), 8101) ==
              SvScenarioTransaction::Result::expired, "bounded staging expiry");
        check(populate(tx, 402, bank, 100), "new transaction supersedes stale staging");
        check(tx.commit(bank, 401, sv_scenario_hash(tx.draft()), 105) ==
              SvScenarioTransaction::Result::wrong_token, "old commit cannot apply new draft");
        tx.reject();
        check(tx.commit(bank, 402, sv_scenario_hash(tx.draft()), 105) ==
              SvScenarioTransaction::Result::invalid, "malformed staged command taints transaction");
    }
    {
        SvLiveSignalBank bank;
        SvScenarioTransaction tx;
        check(populate(tx, 501, bank, 100), "invalid values fixture");
        tx.draft().frequency_millihz = 500U;
        check(tx.commit(bank, 501, sv_scenario_hash(tx.draft()), 101) ==
              SvScenarioTransaction::Result::invalid, "invalid frequency rejected");
        check(bank.snapshot().generation == 0, "invalid frequency cannot mutate published state");
    }
    {
        SvLiveSignalBank bank;
        SvScenarioTransaction tx;
        check(populate(tx, 601, bank, 100), "generation fixture");
        const auto hash = sv_scenario_hash(tx.draft());
        check(tx.commit(bank, 601, hash, 105) == SvScenarioTransaction::Result::accepted,
              "generation fixture commit");
        check(tx.confirm_readback(bank, 601, 105), "readback ticket before external edit");
        auto unsolicited = bank.snapshot();
        unsolicited.channels[2].phase_millidegrees += 1;
        check(bank.publish(unsolicited), "external live edit");
        check(!tx.readback_matches(bank, 601, 105), "unexpected bank generation revokes ticket");
        check(!tx.can_start(bank, 601, hash, 105), "unexpected live edit refuses START");
    }
    {
        SvLiveSignalBank bank;
        SvScenarioTransaction tx;
        const auto before = bank.snapshot();
        check(populate(tx, 701, bank, 100), "running snapshot staged");
        // BEGIN on a running publisher carries an explicit live-mode flag.
        check(tx.begin(702, before, 101, true), "RUNNING scenario transaction allowed");
        auto& live = tx.draft();
        live.frequency_millihz = 60000U;
        tx.mark_frequency();
        for (std::size_t i = 0; i < live.channels.size(); ++i) {
            live.channels[i].rms_counts = static_cast<std::int32_t>(300 + i);
            live.channels[i].phase_millidegrees = static_cast<std::int32_t>(i * 12000);
            live.channels[i].enabled = i != 7;
            live.channels[i].quality = static_cast<std::uint32_t>(100 + i);
            tx.mark_set(i);
            tx.mark_enabled(i);
        }
        live.current_shape = {true, 0, 210, 2, 890};
        tx.mark_shape();
        const auto liveHash = sv_scenario_hash(live);
        check(bank.snapshot() == before, "RUNNING stage does not leak partial channel state");
        check(tx.commit(bank, 702, liveHash, 102) == SvScenarioTransaction::Result::accepted,
              "RUNNING complete transaction commits atomically");
        check(bank.snapshot().frequency_millihz == 60000U &&
              bank.snapshot().channels[7].quality == 107U &&
              bank.snapshot().channels[7].enabled == false,
              "all 4I4V live values published in one generation");
        check(tx.confirm_readback(bank, 702, 103), "RUNNING transaction independent readback");
        check(!tx.can_start(bank, 702, liveHash, 103), "RUNNING ticket cannot ever become START ticket");
    }
    return failures == 0 ? 0 : 1;
}
