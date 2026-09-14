# RCB Reporting — Start Here

For any ARStack61850 work involving URCB, BRCB, DataSet binding, `RptEna`, `InformationReport`, `EntryID`, reconnect, replay, or report bit fields, read:

**[`docs/RCB_REPORTING_REFERENCE.md`](docs/RCB_REPORTING_REFERENCE.md)**

Thirty-second contract:

```text
Static URCB  : Read -> Resv -> RptEna -> readback -> async reports
Static BRCB  : Read -> RptEna -> readback -> async reports
Dynamic URCB : Define+verify DataSet -> Read -> Resv -> configure -> RptEna last -> readback
Dynamic BRCB : Define+verify DataSet -> Read -> configure -> RptEna last -> readback
```

Do not forget:

- reports are unsolicited MMS traffic and can interleave with confirmed Read/Write responses;
- DataSet member order is semantic;
- every grouped Write result must be inspected;
- effective RCB readback is authoritative;
- `TrgOps` and `ReasonForInclusion` have a reserved leading significant bit;
- `ReasonForInclusion` payload `0x40` is data-change and `0x08` is integrity;
- BRCB `EntryID` is the durable buffered-record recovery identity; `SqNum` may reset after re-enable;
- BRCB replay may burst immediately and may contain duplicates when no resume cursor is supplied;
- capture-specific values are evidence, never universal constants.

Current implementation correction points and the complete regression checklist are maintained in the detailed reference. Do not use existing report-reason constants as an oracle without checking that document.
