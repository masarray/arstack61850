from pathlib import Path
import re


def read(path: str) -> str:
    return Path(path).read_text(encoding="utf-8")


def write(path: str, text: str) -> None:
    Path(path).write_text(text, encoding="utf-8")


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one match, got {count}: {old[:160]!r}")
    write(path, text.replace(old, new, 1))


def regex_once(path: str, pattern: str, replacement: str) -> None:
    text = read(path)
    updated, count = re.subn(pattern, lambda _: replacement, text, count=1, flags=re.S)
    if count != 1:
        raise RuntimeError(f"{path}: regex expected one match, got {count}: {pattern!r}")
    write(path, updated)


# A single TCP receive may contain multiple complete TPKT frames. Always drain
# frames already buffered in TpktStreamDecoder before performing another
# blocking transport receive. Otherwise an unsolicited InformationReport can be
# returned first while its following confirmed response remains buffered until a
# network read that may never arrive, producing a false timeout during cleanup.
runtime = "src/mms/association_runtime.cpp"
regex_once(
    runtime,
    r"std::vector<std::uint8_t> MmsAssociationRuntime::receive_application_payload\(\n"
    r"    const Deadline deadline,\n"
    r"    const std::stop_token stop_token\) \{\n"
    r".*?\n"
    r"    return reassembler\.complete\(\);\n"
    r"\}",
    '''std::vector<std::uint8_t> MmsAssociationRuntime::receive_application_payload(
    const Deadline deadline,
    const std::stop_token stop_token) {
    osi::CotpDataReassembler reassembler;
    std::size_t receive_chunks = 0U;

    const auto consume_buffered_frames = [&]() {
        osi::TpktFrame frame;
        while (tpkt_decoder_.try_pop(frame)) {
            const auto tpdu = osi::CotpFrameCodec::decode(frame.payload);
            if (tpdu.kind == osi::CotpTpduKind::disconnect_request) {
                throw MmsAssociationRuntimeError("Remote endpoint requested COTP disconnect.");
            }
            if (tpdu.kind == osi::CotpTpduKind::error) {
                throw MmsAssociationRuntimeError("Remote endpoint returned a COTP error TPDU.");
            }
            if (tpdu.kind != osi::CotpTpduKind::data) {
                throw MmsAssociationRuntimeError(
                    "Expected COTP Data TPDU while receiving an application payload.");
            }
            reassembler.append(tpdu);
            if (reassembler.is_complete()) {
                return;
            }
        }
    };

    while (!reassembler.is_complete()) {
        require_not_cancelled(stop_token);

        // The previous operation may have completed after consuming the first
        // frame from a TCP chunk while one or more complete TPKTs remained in
        // the stream decoder. Those bytes are already received; consume them
        // before asking the transport for more network data.
        consume_buffered_frames();
        if (reassembler.is_complete()) {
            break;
        }

        if (receive_chunks >= options_.maximum_receive_chunks_per_operation) {
            throw MmsAssociationRuntimeError(
                "MMS receive chunk limit exceeded before a complete COTP payload arrived.");
        }
        auto bytes = transport_.receive(deadline, stop_token);
        ++receive_chunks;
        if (bytes.empty()) {
            throw MmsAssociationRuntimeError("MMS transport returned an empty receive chunk.");
        }
        tpkt_decoder_.append(bytes);
    }
    return reassembler.complete();
}''',
)

# Align this independent runtime fixture with the corrected IEC report ordering:
# inclusion -> optional data-reference list -> values -> optional reasons.
test = "tests/test_mms_runtime.cpp"
replace_once(
    test,
    '    add(mms::MmsDataValue::bit_string(7U, inclusion));\n'
    '    add(mms::MmsDataValue::boolean(true));\n'
    '    add(mms::MmsDataValue::visible_string("LD0/PTOC1.Str.stVal"));\n'
    '    add(mms::MmsDataValue::bit_string(2U, reason));\n',
    '    add(mms::MmsDataValue::bit_string(7U, inclusion));\n'
    '    add(mms::MmsDataValue::visible_string("LD0/PTOC1.Str.stVal"));\n'
    '    add(mms::MmsDataValue::boolean(true));\n'
    '    add(mms::MmsDataValue::bit_string(2U, reason));\n',
)

coalesced_test = r'''
void association_drains_coalesced_report_before_confirmed_response() {
    ScriptedTransport transport;
    queue_handshake(transport);
    mms::MmsAssociationRuntime runtime{transport};
    runtime.connect({"127.0.0.1", 102U});

    const auto invoke_id = runtime.next_invoke_id();
    mms::MmsReadRequest request;
    request.invoke_id = invoke_id;
    request.variables.push_back(
        mms::MmsObjectName::domain_specific("LD0", "LLN0$ST$Mod$stVal"));
    const auto request_bytes = mms::MmsServiceCodec::encode_read_request_p_data(request);

    mms::MmsReadResponse response;
    response.invoke_id = invoke_id;
    response.results.push_back({mms::MmsDataValue::boolean(true), std::nullopt});

    const auto report_frame = wrap_application(
        mms::MmsInformationReportCodec::encode_p_data(make_report()));
    const auto response_frame = wrap_application(
        mms::MmsServiceCodec::encode_read_response_p_data(response));
    ByteVector coalesced;
    coalesced.reserve(report_frame.size() + response_frame.size());
    coalesced.insert(coalesced.end(), report_frame.begin(), report_frame.end());
    coalesced.insert(coalesced.end(), response_frame.begin(), response_frame.end());

    // Deliberately provide only one receive chunk. The confirmed response is
    // already buffered behind the report and no second network read is valid.
    transport.push_receive(std::move(coalesced));

    const auto exchange = runtime.exchange_confirmed(request_bytes, invoke_id);
    CHECK(exchange.envelope.kind == mms::MmsPduKind::confirmed_response);
    CHECK(runtime.queued_information_report_count() == 1U);

    ByteVector queued_report;
    CHECK(runtime.try_pop_information_report(queued_report));
    CHECK(mms::MmsInformationReportCodec::is_information_report(queued_report));
    CHECK(!runtime.try_pop_information_report(queued_report));
}

'''
replace_once(
    test,
    "void association_retries_legacy_profile_after_balanced_rejection() {\n",
    coalesced_test + "void association_retries_legacy_profile_after_balanced_rejection() {\n",
)
replace_once(
    test,
    '        {"association lifecycle and routing", association_lifecycle_routes_reports_and_confirmed_results},\n',
    '        {"association lifecycle and routing", association_lifecycle_routes_reports_and_confirmed_results},\n'
    '        {"association drains coalesced report and confirmed response", association_drains_coalesced_report_before_confirmed_response},\n',
)

print("P2 coalesced TPKT transport fix applied")
