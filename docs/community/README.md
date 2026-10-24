# Community IEC 61850 experiments

Real experiences from independent engineering labs help other users learn how to experiment with SV and PTP.

**[Browse submitted experiments](https://github.com/masarray/arstack61850/issues?q=is%3Aissue+%22Community+test%3A%22)** · **[Share your experiment](https://github.com/masarray/arstack61850/issues/new?template=interoperability.yml)** · [First-run tutorial](../GETTING_STARTED.md)

## What you can share

A simple diagram of your test network, a screenshot, a sanitized SV/PTP capture, or a short explanation of the behavior you observed is enough to start. You may use ARStack with analyzers, simulated IEC 61850 subscribers, protection relays or bay controllers.

Try telling other engineers:

1. **Setup:** ARStack Studio version and firmware, plus receiver/analyzer model and revision if known.
2. **Configuration:** SV APPID, svID, 4I+4V sample profile, PTP mode and Ethernet test topology.
3. **Observation:** What happened? What worked well? What would you like to see improved?
4. **Evidence (optional):** Screenshot, decoded frame summary or sanitized packet capture.

## How results are described

A report contributed by an engineer is credited as a **community observation** for that particular setup. When reproduced independently, the record can be updated to distinguish the verified result.

Only actual reports are listed in the [GitHub community test tracker](https://github.com/masarray/arstack61850/issues?q=is%3Aissue+%22Community+test%3A%22); this page does not manufacture compatibility claims or imply endorsement by manufacturers.

## Keep the lab isolated

Never connect an unsolicited SV publisher or simulated PTP grandmaster to a live substation process bus. Remove station identifiers, addresses, credentials and proprietary configuration data from any files you share.
