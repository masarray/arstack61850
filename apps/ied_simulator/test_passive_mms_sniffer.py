#!/usr/bin/env python3
"""P6D passive MMS provenance/ownership regression contract."""
from pathlib import Path
import sys

if len(sys.argv) != 7:
    raise SystemExit("usage: test_passive_mms_sniffer.py decoder receiver controller qml sniffer_qml cmake")

decoder, receiver, controller, qml, sniffer, cmake = [
    Path(path).read_text(encoding="utf-8") for path in sys.argv[1:]
]

required = {
    "decoder": (decoder, (
        "PassiveMmsDecoder::ingest", "stream.tpkt.try_pop", "CotpFrameCodec::decode",
        "SessionCodec::try_decode_data_transfer", "MmsPduCodec::decode_envelope",
        "decoded_reports", "out_of_order", "retransmissions",
        "maximum_pending_segments", "maximum_flows", "maximum_events",
        "confirmed_request", "confirmed_response", "invoke_id"
    )),
    "receiver": (receiver, (
        "RawMmsPacketReceiver", "AF_PACKET", "LoadLibraryA(\"wpcap.dll\")",
        "pcap_open_live", "tcp port 102"
    )),
    "controller": (controller, (
        "PcapReader::read_all", "kMaxPcapBytes", "kMaxPcapPackets",
        "workerPool_.start", "receiver_.receive", "decoder_.ingest",
        "QNetworkInterface::allInterfaces"
    )),
    "qml": (qml, (
        "Open PCAP", "Capture live", "sniffer.events", "sniffer.selectedEvent",
        "Reports", "Control", "Association", "Malformed"
    )),
    "sniffer": (sniffer, (
        "GooseWorkspace {", "MmsSnifferWorkspace {",
        "MmsPassiveSnifferController {"
    )),
    "cmake": (cmake, (
        "MmsPassiveSnifferController.cpp", "qml/MmsSnifferWorkspace.qml"
    ))
}
for label,(source,tokens) in required.items():
    for token in tokens:
        if token not in source:
            raise SystemExit(f"P6D_PASSIVE_MMS_FAIL {label} missing {token}")

for name,source in (("decoder",decoder),("controller",controller),("receiver",receiver),("qml",qml),("sniffer",sniffer)):
    for forbidden in ("connectToIed(", "enableSelected(", "startSimulation(", "sendto(", "pcap_sendpacket"):
        if forbidden in source:
            raise SystemExit(f"P6D_PASSIVE_MMS_FAIL {name} active operation {forbidden}")

if "RawEthernetSubscriber" in receiver or "goose_ethertype" in receiver:
    raise SystemExit("P6D_PASSIVE_MMS_FAIL reused GOOSE capture filter")

print("P6D_PASSIVE_MMS_PASS passive=true live_tcp102=true pcap_replay=true "
      "bounded=true no_client_association=true no_packet_injection=true")
