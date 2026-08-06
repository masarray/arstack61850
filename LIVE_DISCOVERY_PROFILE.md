# Built-in TCP and Live Read-Only Discovery Profile

## Scope

Phase 4C provides a cross-platform TCP implementation of `MmsByteTransport` and a bounded
live discovery workflow above the Phase 4B association runtime. It makes the C++ stack usable
against an explicitly selected MMS endpoint without requiring an application-specific socket
adapter.

## TCP transport

`TcpMmsByteTransport`:

- resolves IPv4 and IPv6 endpoints with `getaddrinfo`;
- uses non-blocking Winsock or POSIX sockets;
- awaits connect, send, and receive readiness in bounded slices;
- observes the caller's absolute deadline and `std::stop_token` during every wait;
- enables TCP_NODELAY and keepalive by default;
- handles partial sends and arbitrary receive chunking;
- reports timeout, cancellation, peer close, and socket failures as typed runtime errors; and
- owns no worker thread and performs no automatic reconnect.

The transport is intentionally serialized: one association operation may use it at a time.
`MmsAssociationRuntime` remains responsible for TPKT/COTP framing, association state, invoke
routing, and reconnect policy.

## Live discovery

`MmsLiveDiscoveryClient` performs only these confirmed MMS services:

1. GetNameList for VMD domains;
2. GetNameList for domain-specific named variables;
3. GetNameList for domain-specific named-variable lists;
4. optional GetVariableAccessAttributes probes;
5. optional GetNamedVariableListAttributes DataSet directory reads; and
6. optional multi-variable Read requests for discovered RCB attributes.

The result contains bounded domain/name evidence, DataSet and BRCB/URCB inventory, variable
TypeSpecification evidence, DataSet members, read-only RCB state, and explicit diagnostics for
optional probes that failed. Pagination must make forward progress and all domain, page, name,
type, DataSet, and RCB counts are caller-bounded.

`MmsTcpLiveDiscoverySession` combines the built-in TCP transport, Phase 4B association runtime,
and the discovery client. The `ariec61850_live_discover` tool exposes this workflow with human
or JSON summaries.

## Read-only boundary

The discovery client never constructs an MMS Write request. It does not enable or reserve an
RCB, issue GI, create/delete a DataSet, operate a control object, or access MMS file services.
Regression tests decode every discovery request and reject service tag 5 (Write).

`TcpMmsByteTransport` itself is a general byte transport. An application that deliberately
combines it with another runtime surface remains responsible for that surface's authorization
and safety policy.

## Acceptance boundary

Automated loopback transport tests and scripted discovery tests do not replace physical IED
interoperability. Controlled laboratory evidence is still required for vendor-specific
association parameters, large model pagination, slow IED timing, disconnect behavior, and
long-running network stability.
