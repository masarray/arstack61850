# MMS Association, TCP, and Read-Only Discovery Runtime Profile

## Association runtime

`MmsAssociationRuntime` owns TPKT/COTP, Session, Presentation, ACSE/MMS Initiate, invoke
routing, InformationReport queuing, deadline/cancellation events, disconnect, and explicit
reconnect. It receives an `MmsByteTransport` and does not create hidden background work.

## Built-in TCP transport

Phase 4C adds `TcpMmsByteTransport` for Winsock and POSIX sockets. It is non-blocking, resolves
IPv4/IPv6 endpoints, handles partial sends and receive chunks, and observes absolute deadlines
and `std::stop_token` cancellation. It performs no protocol operation by itself.

## Read-only discovery runtime

`MmsTcpLiveDiscoverySession` combines the TCP transport, association runtime, and
`MmsLiveDiscoveryClient`. The discovery client is restricted to GetNameList,
GetVariableAccessAttributes, GetNamedVariableListAttributes, and Read.

Phase 4C.1 maps the resulting evidence to `live-ied-model-v1`, computes a canonical fingerprint,
and provides repeated reconnect/discovery evidence runners. This read-only workflow never
constructs Write, control, GI, RCB reservation/enable, dynamic DataSet mutation, or file-service
requests.

## Report subscription runtime

`MmsReportSubscriptionRuntime` remains a separate explicit stateful surface. It can probe,
reserve/enable supported URCBs, receive reports, stop, and perform ownership-aware cleanup. It
is not invoked by live discovery or the Phase 4C.1 evidence runner.

## Acceptance

Automated tests cover scripted association, routing, report interleaving, cancellation/timeout,
TCP loopback, read-only service allowlisting, and live-model parity behavior. Physical IED
acceptance requires the Phase 4C.1 lab runner, same-target C# parity, reconnect/soak evidence,
and reviewed packet-capture references.
