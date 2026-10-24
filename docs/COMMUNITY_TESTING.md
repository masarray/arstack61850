# Share an IEC 61850 interoperability experiment

Have you tried ARStack with a simulator, analyzer, protection relay or bay controller in your own lab? Your results can help the international IEC 61850 community discover useful configurations and improve ARStack.

**[Share an interoperability report](https://github.com/masarray/arstack61850/issues/new?template=interoperability.yml)** · [Ask for help](../SUPPORT.md)

## What is helpful to share?

- Which **ARStack Studio release** and ESP32-P4 firmware were used?
- What was the lab topology? (For example: ESP32-P4 → isolated switch → analyzer/subscriber.)
- Which SV profile and PTP mode were selected?
- Which device or software was receiving or analyzing the traffic? Make/model/version are helpful but optional.
- What did you observe? Include positive results, unexpected behavior or a capture screenshot.
- Which tests would you like to try next?

A short description and one screenshot are useful. You do not need to write a formal test report.

## Capture and privacy

If you share an SCL/SCD/CID file or packet capture, remove confidential project names, private IP addresses, credentials and other sensitive network information. Only share files you have permission to distribute.

Each external report describes the **specific setup tested**. Reported results are community observations; the project will clearly identify whether a result was independently reproduced.

## Experiment ideas

- Inspect SV `APPID`, `svID`, `smpCnt`, quality and sample counter rollover.
- Compare PTP Announce/Sync/Follow_Up with the received SV stream.
- Test SCL-derived stream selection and publisher configuration.
- Test how a laboratory analyzer or IEC 61850 subscriber interprets the published reference profile.

Keep active SV/PTP experiments on an isolated lab Ethernet segment, never a live substation process bus.
