<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <!-- Acceptance fixture: indexed ReportControl expansion + URCB/BRCB GI/report interoperability; non-indexed legacy coverage is separate. -->
  <Header id="AR_INTEROP_INDEXED_REPORTS" version="1" revision="A" />
  <IED name="INTEROP01" manufacturer="AR" type="ProtectionIED" configVersion="1">
    <AccessPoint name="P1">
      <Server>
        <LDevice inst="LD0">
          <LN0 lnClass="LLN0" lnType="LLN0Type">
            <DataSet name="dsGO">
              <FCDA ldInst="LD0" lnClass="XCBR" lnInst="1" doName="Pos" daName="stVal" fc="ST" />
              <FCDA ldInst="LD0" lnClass="XCBR" lnInst="1" doName="Pos" daName="q" fc="ST" />
              <FCDA ldInst="LD0" lnClass="XCBR" lnInst="1" doName="Pos" daName="t" fc="ST" />
            </DataSet>
            <ReportControl name="Unbuffer" datSet="dsGO" rptID="INTEROP01LD0/LLN0$RP$Unbuffer" confRev="1" buffered="false" indexed="true" bufTime="100" intgPd="1000">
              <RptEnabled max="2" />
            </ReportControl>
            <ReportControl name="Buffer" datSet="dsGO" rptID="INTEROP01LD0/LLN0$BR$Buffer" confRev="2" buffered="true" indexed="true" bufTime="75" intgPd="1000">
              <RptEnabled max="2" />
            </ReportControl>
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
