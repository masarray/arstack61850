# Built-in TCP, Live Read-Only Discovery, and Live-Model Parity Profile

## Scope

Phase 4C provides a cross-platform TCP implementation of `MmsByteTransport` and bounded live
MMS discovery. Phase 4C.1 maps that evidence into the same `live-ied-model-v1` engineering
shape exported by the C# behavioral oracle and adds repeatable physical-lab evidence tooling.

## TCP transport

`TcpMmsByteTransport` resolves IPv4/IPv6 endpoints, uses non-blocking Winsock/POSIX sockets,
handles partial sends and arbitrary receive chunks, and observes absolute deadlines plus
`std::stop_token` cancellation. It performs no operation until explicitly used by an
application runtime.

## Read-only discovery

`MmsLiveDiscoveryClient` performs only:

1. GetNameList for VMD domains;
2. GetNameList for domain variables;
3. GetNameList for named-variable lists;
4. optional GetVariableAccessAttributes;
5. optional GetNamedVariableListAttributes; and
6. optional Read of discovered RCB attributes.

It does not construct Write, RCB enable/reservation, GI, control, dynamic DataSet mutation, or
file-service requests. Regression tests decode every discovery request and reject service tag 5.

## Live-model parity

The C++ mapper normalizes `LN$FC$DO$DA...` into LD/LN/DO/DA hierarchy, preserves FC and MMS
references, resolves direct or nested TypeSpecification evidence, infers CDC with explicit
confidence, includes DataSet and RCB evidence, and produces a deterministic manifest and
fingerprint. `ariec61850_live_discover --model-json` emits `live-ied-model-v1` JSON.

`scripts/compare-live-model-parity.py` compares C# and C++ documents for identity, attributes,
FC/type, DataSets, and report controls.

## Physical interoperability evidence

`scripts/run-live-readonly-interop.py` reconnects and performs fresh discovery for each cycle,
retains per-cycle JSON, verifies stable fingerprints and warning policy, optionally runs C#
parity, and writes `interop-summary.json`. PowerShell and Bash wrappers are included.

Automated loopback and scripted tests do not replace physical IED evidence. Merge acceptance
still requires controlled runs against a reachable IED or vendor simulator, Windows/MSVC CI,
large-model pagination when available, timeout/reconnect evidence, and a reviewed C# parity
capture against the same IED configuration.
