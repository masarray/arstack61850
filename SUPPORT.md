# ARStack support

**[Download the latest version](https://github.com/masarray/arstack61850/releases/latest)** · **[Getting started](docs/GETTING_STARTED.md)** · **[Installation](docs/INSTALL.md)**

## Something not working?

Use the [simple bug-report form](https://github.com/masarray/arstack61850/issues/new?template=bug_report.yml). A short description is enough to start.

Useful details include the Studio release, Windows version, ESP32-P4 firmware build if connected, the exact error shown and the last action you tried.

**SCL import issue?** Follow the [SCL import troubleshooting steps](docs/SCL_TROUBLESHOOTING.md). Share the Studio version, stream selection and a sanitized sample if possible. Never share confidential installation data.

**Can't find the board?** Check the USB data cable and serial device selection; use **Configuration → Firmware** to verify the ESP32-P4 and install matching firmware.

**SMV missing in ARSAS, DigSubAnalyzer or Wireshark?** Follow the [independent Ethernet receiver check](docs/INTEROP_FIRST_PACKET.md): verify SV RUNNING, capture on the actual NIC, and compare priority-tagged VLAN 0 with the optional untagged lab SCL.

**Interested in IEC 61850 subscriber testing?** Share results in the [community interoperability guide](docs/COMMUNITY_TESTING.md).

For a new release, always install the Studio/firmware pairing shipped together. Do not connect active SV/PTP lab output to an operating substation LAN.
