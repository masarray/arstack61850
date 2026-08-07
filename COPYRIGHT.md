# Copyright and provenance

Copyright (C) 2026 Ari Sulistiono and contributors.

The C++ implementation is an incremental clean-room migration guided by public standards,
wire-format evidence, and the ARIEC61850 C# repository as the behavioral oracle. Phase 4C.1
adds a C++ engineering-model representation compatible with the C# `live-ied-model-v1` export
shape so deterministic cross-language comparison can be performed.

No generated physical IED evidence is embedded in the source repository. Lab captures,
endpoint addresses, vendor configuration exports, and C#↔C++ parity artifacts should be kept
in controlled evidence storage and reviewed before publication.

See `LICENSE`, `NOTICE`, `THIRD_PARTY_NOTICES.md`, and
`PHASE_4C1_LIVE_MODEL_INTEROP.md`.
