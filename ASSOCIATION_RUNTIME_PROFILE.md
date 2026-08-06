# MMS Association and Report Subscription Runtime Profile

## Scope

Phase 4B adds the stateful runtime that joins the deterministic Phase 3 association codecs
and Phase 4A reporting codecs. The runtime is transport-injected: an application supplies an
`MmsByteTransport` implementation, while the core owns TPKT/COTP framing, Session,
Presentation, ACSE/MMS association, confirmed-service routing, and RCB subscription state.

## Association lifecycle

`MmsAssociationRuntime` implements:

1. byte-transport connect with deadline and `std::stop_token` cancellation;
2. COTP Connection Request/Confirm negotiation and TPDU-size capture;
3. ISO Session Connect, Presentation CP, ACSE AARQ, and MMS Initiate exchange;
4. accepted/rejected association validation and negotiated MMS limits;
5. segmented COTP send and bounded reassembly on receive;
6. monotonically allocated bounded invoke IDs;
7. confirmed-response/error routing while interleaved InformationReports are queued;
8. explicit timeout, cancellation, fault, and disconnect events; and
9. best-effort COTP Disconnect Request before closing the injected transport.

A failed or timed-out confirmed exchange faults the association because the transport stream
may no longer have a trustworthy request/response boundary. Reconnect is explicit: close or
fault the old runtime state, refill/recreate the transport as required, and call `connect` again.

## Report subscription lifecycle

`MmsReportSubscriptionRuntime` implements a persistent static-DataSet RCB workflow:

1. re-probe the selected RCB immediately before any write;
2. refuse takeover when `RptEna`, reservation, or availability evidence indicates another
   session owns the RCB;
3. optionally reserve an URCB using `Resv=true`;
4. optionally write `DatSet`, `TrgOps`, and `OptFlds`;
5. enable with `RptEna=true`;
6. optionally request GI after enable;
7. receive, decode, map, and monitor InformationReports until stop/cancellation/fault;
8. disable only when this runtime enabled the RCB;
9. release only reservations touched by this runtime; and
10. record `cleanup_required` when the association is lost before cleanup completes.

`retry_cleanup` can be called after a successful reconnect. BRCB `EntryID` resume, purge
policy, buffer-overflow recovery, and dynamic DataSet create/delete remain separate work.

## Safety and ownership rules

- The core does not silently take over an already enabled or reserved RCB.
- Cleanup writes are limited to state touched by the current runtime instance.
- The runtime does not include a default TCP socket implementation; live network access is
  possible only when an application explicitly supplies an `MmsByteTransport`.
- No control service, SBO state machine, file service, dynamic DataSet create/delete, or
  automatic BRCB replay is enabled by this phase.

## Validation

The scripted transport regression covers:

- successful COTP/ACSE/MMS association and negotiated limits;
- cancellation followed by a clean reconnect;
- confirmed request/response routing with an interleaved InformationReport;
- confirmed-exchange timeout and fault transition;
- URCB probe, reserve, enable, GI, report receive, disable, and reservation release;
- cleanup-required state after association loss; and
- refusal to take over an enabled RCB.

The repository has 16 CTest entries through Phase 4B. Local GCC, Clang, and Clang
ASan/UBSan runs pass all 16 tests with warnings treated as errors.
