# MMS file transfer client

## Status and scope

The C++20 core implements the read/download side of the ISO 9506 MMS file
service used by IEC 61850 engineering clients:

- `FileDirectory` confirmed service `[77]`;
- `FileOpen` confirmed service `[72]`;
- `FileRead` confirmed service `[73]`;
- `FileClose` confirmed service `[74]`;
- bounded pagination with `continueAfter`;
- the full signed `Integer32` FRSM identifier range;
- one precise rooted-backslash interoperability fallback; and
- caller-owned streaming sinks with best-effort remote cleanup.

Upload, FileDelete, FileRename, and every other remote file mutation are out of
scope. The implementation is not a claim of full IEC 61850 conformance or
multi-vendor interoperability.

## Architecture

`include/ariec61850/mms/file_service.hpp` and
`src/mms/file_service.cpp` contain the portable layer. They reuse the existing
BER, MMS PDU, Presentation, invoke-routing, and association runtime. They do not
open sockets, create threads, or access a filesystem.

The portable boundaries are:

- `MmsFileServiceCodec` for request/response wire models;
- `MmsFileServiceChannel` for a confirmed-exchange provider;
- `MmsAssociationFileServiceChannel` for the existing association runtime;
- `MmsFileSink` for application-owned flash, SD, RAM, or other storage; and
- `MmsFileTransferRuntime` for directory and download lifecycle policy.

The host-only `ariec61850_file_transfer` executable supplies the native TCP
transport and a filesystem-backed sink. It is not part of `embedded_core`.

## Wire and interoperability behavior

Normal `FileOpen` encodes a normalized path as a sequence of GraphicString path
segments. Backslashes received from the caller are normalized to `/`; empty,
traversal (`.` or `..`), control-character, null-character, drive/scheme-like,
and overlong paths are rejected before an exchange.

Some deployed relays list a root file normally but require FileOpen to use one
GraphicString containing a leading backslash. Adaptive download first sends the
normal segmented request. It retries exactly once with a rooted path only when:

1. FileOpen returned Confirmed-Error `errorClass [11]`, value `7`
   (`file/file-non-existent`);
2. no FileRead operation ran and no byte was accepted; and
3. the caller-provided sink resets successfully.

Access denied, malformed responses, transport faults, cancellation, any error
after FileRead, or any other service-error value never triggers the fallback.
Primary and fallback diagnostics are retained separately.

FRSM identifiers are signed 32-bit values. Negative values and both Integer32
endpoints are accepted from FileOpen and echoed unchanged by FileRead/FileClose.
A FileOpen size attribute of zero is treated as unavailable, matching the C#
oracle and deployed relay behavior; a positive advertised size remains an
enforced upper bound.

## Bounded lifecycle and failure rules

Callers configure explicit bounds for directory pages, entries, read
operations, total bytes, block bytes, diagnostic entries, and diagnostic text.
The runtime also rejects:

- repeated/no-progress directory pages;
- an empty FileRead block with `moreFollows=true`;
- arithmetic or configured byte-limit overflow;
- bytes beyond a positive advertised file size; and
- declared-size mismatch when strict matching is requested.

After FileOpen succeeds, FileClose is attempted even after read validation,
sink failure, cancellation observed between operations, or a normal pipeline
failure. Cleanup uses a fresh stop token so caller cancellation does not skip it.
If the underlying association has already faulted or timed out, the result
records that cleanup could not be sent. A FileClose failure never replaces an
earlier primary failure. `remote_file_closed` makes the cleanup outcome explicit.

## Host CLI

Build with `ARIEC61850_BUILD_TOOLS=ON`, then list the root directory:

```text
ariec61850_file_transfer <host> [port] list --json-output evidence.json
```

Download requires both an explicit remote path and destination:

```text
ariec61850_file_transfer <host> [port] download \
  --remote REMOTE_PATH --output LOCAL_FILE \
  --max-bytes 1048576 --json-output evidence.json
```

Exit codes are stable:

- `0`: requested operation completed;
- `2`: argument/configuration error;
- `3`: association, transport, protocol, decode, or limit failure;
- `4`: local sink failure; and
- `5`: cleanup was the terminal failure.

The default command is FileDirectory. The CLI never uploads, deletes, or
renames a remote object. JSON evidence may contain endpoint/file metadata and
should remain in an ignored local evidence directory unless deliberately
sanitized.

## Validation evidence

Offline regression coverage includes golden request/response shapes, nested and
root directory paths, pagination/continuation, attributes, malformed/truncated/
trailing BER, invoke mismatch, all signed FRSM boundaries, multi-block reads,
empty-progress blocks, configured and advertised size bounds, sink failures,
cancellation/timeout evidence, cleanup precedence, and adaptive fallback policy.
A dedicated mutation target and corpus exercise all four response decoders.

The current controlled lab acceptance proved:

- one bounded root FileDirectory operation;
- a small read-only file downloaded in one FileRead;
- the deployed rooted-backslash FileOpen fallback;
- zero-size FileOpen attribute interoperability;
- successful FileClose after both success and forced block-limit failure; and
- exact local byte count plus SHA-256 evidence.

Exact endpoint, remote filename, file content/hash, and JSON traces are retained
locally in the ignored evidence directory and are intentionally not published.
Pagination was not observed on the current one-page directory and remains a
multi-page live acceptance item. ESP32 compilation/device execution and
multi-vendor physical acceptance also remain pending.
