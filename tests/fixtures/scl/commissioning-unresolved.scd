<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Header id="AR_COMMISSIONING_UNRESOLVED" version="1" revision="A" />
  <IED name="IED_BAD" manufacturer="AR" type="CommissioningNegative" configVersion="1">
    <AccessPoint name="P1">
      <Server>
        <LDevice inst="LD0">
          <LN0 lnClass="LLN0" lnType="LLN0Type">
            <GSEControl name="GCB_MISSING" datSet="missingDS" appID="missing-goose" confRev="1" type="GOOSE" />
            <ReportControl name="URCB_MISSING" datSet="missingDS" rptID="IED_BADLD0/LLN0$RP$URCB_MISSING" confRev="1" buffered="false" />
          </LN0>
          <LN lnClass="XCBR" inst="1" lnType="XCBRType" />
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="LLN0Type" lnClass="LLN0" />
    <LNodeType id="XCBRType" lnClass="XCBR">
      <DO name="Pos" type="PosType" />
    </LNodeType>
    <DOType id="PosType" cdc="DPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="q" bType="Quality" fc="ST" />
      <DA name="t" bType="Timestamp" fc="ST" />
    </DOType>
  </DataTypeTemplates>
</SCL>
