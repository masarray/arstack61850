#!/usr/bin/env python3
"""One-shot namespace correction for the generated MMS parity smoke block."""

from pathlib import Path

path = Path("embedded/mms_connection_runtime_hard_profile_smoke.cpp")
text = path.read_text(encoding="utf-8")

replacements = {
    "const auto file_directory_request = MmsPduSpanCodec::encode_confirmed_request_into(":
        "const auto file_directory_request = mms::MmsPduSpanCodec::encode_confirmed_request_into(",
    "static_cast<std::int32_t>(MmsWireConfirmedService::file_directory)":
        "static_cast<std::int32_t>(mms::MmsWireConfirmedService::file_directory)",
    "    MmsConfirmedPduView file_directory_response;":
        "    mms::MmsConfirmedPduView file_directory_response;",
    "result.application_service != MmsWireConfirmedService::file_directory":
        "result.application_service != mms::MmsWireConfirmedService::file_directory",
    "runtime.state() != MmsStaticConnectionState::established":
        "runtime.state() != mms::MmsStaticConnectionState::established",
    "!MmsPduSpanCodec::try_decode_confirmed_response_view(":
        "!mms::MmsPduSpanCodec::try_decode_confirmed_response_view(",
    "file_directory_response.service() != MmsWireConfirmedService::file_directory":
        "file_directory_response.service() != mms::MmsWireConfirmedService::file_directory",
}

for old, new in replacements.items():
    count = text.count(old)
    # MmsStaticConnectionState appears elsewhere in the existing test, so only
    # replace the one unqualified occurrence introduced by the parity block.
    if old == "runtime.state() != MmsStaticConnectionState::established":
        if count != 1:
            raise SystemExit(f"namespace fix {old!r}: expected 1, found {count}")
    elif count != 1:
        raise SystemExit(f"namespace fix {old!r}: expected 1, found {count}")
    text = text.replace(old, new, 1)

path.write_text(text, encoding="utf-8")
print("Qualified generated MMS parity smoke symbols.")
