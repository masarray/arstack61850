# Isolated IEC 61850 Laboratory Checklist

## Before connection

- Use an isolated engineering network with an approved test IED or vendor simulator.
- Record vendor, model, firmware, IP, MMS port, C# commit, and C++ commit.
- Confirm no production breaker/process control is reachable.
- Confirm the intended Phase 4C.1 command uses only the read-only discovery CLI.
- Start packet capture when permitted.

## Read-only request allowlist

Phase 4C.1 discovery may issue only:

- MMS GetNameList;
- MMS GetVariableAccessAttributes;
- MMS GetNamedVariableListAttributes; and
- MMS Read.

Stop the test if Write, GI, RCB reservation/enable, control, dynamic DataSet mutation, or
file-service traffic is observed.

## Minimum acceptance run

1. Build Release with warnings as errors and run repository CTest.
2. Run three reconnect/discovery cycles against the test target.
3. Confirm every cycle produces `live-ied-model-v1` and a non-empty hierarchy.
4. Confirm fingerprints are stable when the IED configuration is unchanged.
5. Export the C# oracle model against the same target/configuration.
6. Run C#↔C++ parity and review every blocking finding.
7. Run ten cycles for the primary target vendor.
8. Exercise a timeout/connection-failure scenario, then confirm a clean reconnect.
9. Record multi-page GetNameList evidence when the target model is large enough.
10. Store `interop-summary.json`, per-cycle models, parity reports, and capture references.

## Review gate

Do not mark Phase 4C.1 physically accepted until:

- the same-target C# parity report has no unexplained blocking finding;
- warning policy is reviewed;
- fingerprint stability is demonstrated;
- reconnect behavior is demonstrated;
- Windows/MSVC and repository CI pass; and
- an engineer signs the completed `LAB_INTEROP_REPORT_TEMPLATE.md`.
