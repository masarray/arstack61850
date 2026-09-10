# AGENTS.md — arstack61850 Production Engineering Contract

These rules apply to every AI/code agent working in this repository. arstack61850 is protocol/embedded engineering software. Correctness, deterministic timing, bounded resource use, interoperability, and failure containment are product requirements from the first implementation.

## 1. Prime directive

Do not begin with a disposable, intentionally naive, prototype-only implementation when the production architecture is knowable.

Choose the smallest production-quality design that satisfies the requirement without speculative complexity.

Priority order:
1. protocol correctness and data integrity;
2. deterministic/fail-safe behavior;
3. regression/interoperability compatibility;
4. bounded latency, CPU, memory, and queue depth;
5. maintainability and testability.

Never trade a working IEC 61850 capability for a shortcut that only makes a local demo pass.

## 2. Mandatory workflow

For non-trivial changes:

RECONNAISSANCE -> REPRODUCE/BASELINE -> ROOT CAUSE -> INVARIANTS -> ARCHITECTURE IMPACT -> IMPLEMENT -> REGRESSION TEST -> NEGATIVE/FAILURE TEST -> PERFORMANCE/TIMING CHECK -> BUILD/CI -> INTEROP VALIDATION

Before editing:
- locate the current protocol path, state machine, buffer ownership, callback/lifecycle owner, and all consumers;
- identify wire-format, timing, sequence, dataset, control-block, and public API invariants;
- determine root cause before patching symptoms;
- avoid creating a second parser/state machine/cache/source of truth for behavior that already exists;
- preserve portable core behavior unless a platform-specific boundary is explicitly required.

If one approach fails, stop and reassess assumptions. Do not stack workaround upon workaround.

## 3. Architecture boundaries

Keep the protocol core independent from UI, shell, packaging, and platform glue.

Preferred conceptual direction:

Protocol/Core models and codecs
-> Session/state machines and application services
-> Platform adapters (socket, timer, filesystem, NIC, OS)
-> GUI/CLI/embedded integration

Rules:
- wire encoding/decoding must not depend on UI code;
- ESP32, Windows/Linux, and test transports should adapt to shared protocol semantics instead of forking them;
- avoid global mutable protocol state;
- explicit ownership is required for sessions, buffers, timers, sockets, datasets, reports, control blocks, and callbacks;
- do not introduce abstraction layers with no current use.

## 4. Defensive protocol parsing

Treat every received frame, file, configuration, and remote endpoint as fallible.

Before accessing fields:
- validate minimum header length;
- validate declared payload length against available bytes;
- validate offsets, counts, indexes, and alignment assumptions;
- validate enum/tag/type values;
- validate integer arithmetic for overflow/underflow;
- validate string/BER/MMS lengths before allocation or copy;
- reject or safely isolate malformed/truncated inputs.

Never index or dereference based only on untrusted wire metadata.

In C/C++ use explicit bounds checks, RAII where available, scoped ownership, `std::optional`/typed result/error patterns where appropriate, and safe integer conversions.

Do not hide errors with broad catch-all handling. Fail at a meaningful boundary and preserve process/session availability when isolation is possible.

A malformed packet must not crash the analyzer, GUI, service, or embedded target.

## 5. State machines must be explicit

IEC 61850 association, report, GOOSE, Sampled Values, control, file transfer, discovery, reconnect, and embedded generation logic must use explicit states/transitions where state exists.

Never repair a state-machine race with arbitrary sleeps or retries.

Every retry policy must be bounded and have:
- maximum attempt or timeout behavior;
- cancellation/shutdown behavior;
- clear transition after terminal failure;
- no unbounded reconnect or busy loop.

Unexpected or out-of-order traffic must not corrupt session state.

## 6. Zero blocking on latency-sensitive paths

Do not perform blocking filesystem, DNS/network setup, logging, allocation-heavy formatting, GUI work, or unrelated computation inside high-frequency packet callbacks, transmit loops, ISR-adjacent paths, or timing-critical workers.

On desktop, expensive work belongs on bounded background execution rather than the UI thread.

On embedded targets, timing-critical work must remain deterministic and bounded. Avoid dynamic behavior whose worst-case cost is unknown.

Never spawn one thread/task per packet or event.

## 7. Bounded queues and backpressure

All producer/consumer paths must have an explicit overload policy.

Avoid unbounded queues for:
- captured frames;
- MMS responses;
- GOOSE events;
- SV samples;
- logs;
- GUI updates;
- transmit requests;
- device telemetry.

Use bounded ring buffers, batching, coalescing, sampling, drop policies, or backpressure according to semantics.

When loss is acceptable for presentation/telemetry, prefer dropping/coalescing stale intermediate data over growing memory without bound.

When loss is not acceptable, apply backpressure or fail explicitly rather than silently corrupting order/data.

## 8. Sampled Values / GOOSE / generator timing

For periodic transmit and sample generation:
- avoid heap allocation in the steady-state hot loop;
- precompute invariant frame sections when practical;
- reuse packet buffers;
- bound per-cycle work;
- keep logging/diagnostics off the timing-critical path;
- separate configuration changes from the real-time send loop using safe handoff;
- measure jitter, missed deadlines, CPU load, queue depth, and sustained-rate behavior where applicable.

Never claim real-time behavior from average timing alone. Consider worst-case and sustained load.

## 9. Memory discipline

Embedded and high-rate desktop paths must avoid unnecessary allocation churn.

Prefer:
- preallocated/reused packet buffers;
- fixed-capacity or bounded containers when maximums are known;
- spans/views instead of copies;
- incremental parsing instead of duplicated whole-message structures;
- deterministic cleanup through RAII/scoped ownership.

Use object pools only when the lifetime model is safe and profiling/measurement shows value.

Never keep raw pointers to buffers whose ownership/lifetime can end asynchronously.

## 10. Files, SCL, captures, and large data

Do not eagerly duplicate large captures/SCL datasets into multiple complete representations.

Prefer streaming/chunked parsing and indexed access when practical.

Validate malformed XML/SCL, missing references, inconsistent datasets, invalid addressing, unsupported editions/features, and oversized declarations.

Do not invent engineering values to compensate for missing source data. Preserve unknown/invalid states explicitly.

## 11. Thread safety and shutdown

Every worker, socket, timer, queue, callback registration, file handle, mapping, and platform handle must have a clear owner and shutdown path.

Shutdown/reconfigure must:
- signal cancellation/stop;
- prevent new work from entering;
- drain/drop according to documented semantics;
- join/retire workers safely;
- release sockets/handles/buffers;
- prevent callbacks into destroyed objects.

Do not detach threads merely to avoid lifecycle work.

## 12. Interoperability over imitation

Do not cosmetically alter protocol behavior to imitate a specific vendor trace/tool.

Wire behavior must follow the implemented IEC 61850 contract, supported edition/profile, and documented project compatibility requirements.

When matching another implementation, distinguish clearly between:
- required standard behavior;
- accepted de-facto interoperability behavior;
- vendor-specific behavior;
- unsupported behavior.

Add deterministic fixtures for every compatibility quirk that is intentionally supported.

## 13. Performance and timing contracts

Performance-sensitive changes should identify relevant measurements, such as:
- messages/s or samples/s;
- sustained CPU load;
- allocation rate/heap high-water mark;
- queue depth/drop count;
- transmit jitter/deadline misses;
- parse throughput;
- reconnect/association latency;
- firmware task stack usage;
- desktop UI latency when applicable.

Do not claim optimization without evidence.

Prefer simpler algorithms and bounded data structures over speculative micro-optimization.

## 14. ESP32 / embedded addendum

For ESP32 targets:
- no blocking network/file/console operations in timing-critical tasks;
- avoid uncontrolled heap allocation after steady state begins;
- size task stacks intentionally and monitor high-water marks;
- use bounded FreeRTOS queues/ring buffers;
- avoid priority inversion and long critical sections;
- keep ISR work minimal and defer processing safely;
- watchdog behavior must expose actual deadlock/stall problems, not be disabled as a workaround;
- reconnect/network failures must not wedge the main application loop;
- firmware loading/update paths must validate image metadata, size, target compatibility, integrity where available, and failure recovery before replacing working firmware.

## 15. Regression prevention

Every protocol bug fix should add/update a deterministic regression test whenever technically practical.

Test the exact failure mode: malformed lengths, disconnect timing, sequence/order issue, timeout, wraparound, unsupported data, buffer boundary, state transition, or platform-specific failure.

Before changing public structs/APIs, binary/wire formats, config semantics, timing defaults, or supported behavior, evaluate compatibility with existing consumers and tests.

## 16. Change discipline

Prefer the smallest coherent root-cause fix.

Do not:
- mix unrelated refactoring into a focused bug fix;
- duplicate parsers/state machines/services;
- rewrite a working subsystem because a local patch is difficult to understand;
- add dependencies without evaluating size, portability, security, maintenance, and runtime impact;
- add background workers/caches merely as generic performance patterns.

## 17. Definition of done

A task is not complete because it compiles.

Validate as applicable:
BUILD
+ STATIC ANALYSIS
+ UNIT/DETERMINISTIC FIXTURE TESTS
+ REGRESSION TEST
+ MALFORMED/NEGATIVE TESTS
+ TIMING/PERFORMANCE CHECK
+ RESOURCE/LIFECYCLE CHECK
+ DESKTOP/EMBEDDED TARGET BUILD
+ SIMULATOR/LOOPBACK
+ AUTHORIZED INTEROP/LAB VALIDATION

Never claim a validation step was run when it was not.

## 18. Agent completion report

Report:
- Changed;
- Root cause;
- Architecture decision;
- Invariants preserved;
- Regression protection;
- Timing/performance impact;
- exact validation executed;
- remaining genuine limitations.

## Final rule

Think like the engineer responsible for a protocol stack and embedded device under sustained field load for years, not like a prototype generator trying to make one packet or screenshot pass.

Understand first. Bound every hot path. Make states explicit. Preserve protocol semantics. Validate malformed inputs. Measure timing. Prevent regressions.
