<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Header id="AR_IEDSIM_FULL_MODEL" version="1" revision="A" />
  <IED name="MU01" manufacturer="AR" type="Simulator" configVersion="1">
    <AccessPoint name="P1">
      <Server>
        <LDevice inst="LD0">
          <LN0 lnClass="LLN0" lnType="LLN0Type">
            <DataSet name="dsSV">
              <FCDA ldInst="LD0" lnClass="TCTR" lnInst="1" doName="Amp" daName="instMag.i" fc="MX" />
            </DataSet>
            <DataSet name="dsBuffered">
              <FCDA ldInst="LD0" lnClass="XCBR" lnInst="1" doName="SimCfg" daName="setVal" fc="SP" />
            </DataSet>
            <ReportControl name="urcb01" datSet="dsSV" rptID="MU01_LD0_URCB01" buffered="false" bufTime="20" intgPd="1000" confRev="7" indexed="false">
              <TrgOps period="true" gi="true" />
              <OptFields seqNum="true" timeStamp="true" reasonCode="true" dataSet="true" dataRef="true" configRef="true" />
            </ReportControl>
            <ReportControl name="brcb01" datSet="dsBuffered" rptID="MU01_LD0_BRCB01" buffered="true" bufTime="20" intgPd="0" confRev="8" indexed="false">
              <TrgOps dchg="true" />
              <OptFields seqNum="true" timeStamp="true" reasonCode="true" dataSet="true" dataRef="true" bufOvfl="true" entryID="true" configRef="true" />
            </ReportControl>
          </LN0>
          <LN lnClass="TCTR" inst="1" lnType="TCTRType" />
          <LN lnClass="XCBR" inst="1" lnType="XCBRType" />
          <LN lnClass="GGIO" inst="1" lnType="GGIOControlType" />
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="LLN0Type" lnClass="LLN0" />
    <LNodeType id="TCTRType" lnClass="TCTR">
      <DO name="Amp" type="AmpType" />
    </LNodeType>
    <DOType id="AmpType" cdc="SAV">
      <DA name="instMag" bType="Struct" type="AnalogueValue" fc="MX" />
      <DA name="q" bType="Quality" fc="MX" />
    </DOType>
    <DAType id="AnalogueValue">
      <BDA name="i" bType="INT16" />
      <BDA name="range" bType="INT8U" />
    </DAType>
    <LNodeType id="GGIOControlType" lnClass="GGIO">
      <DO name="SPCSO1" type="SPCSOType1" />
      <DO name="SPCSO2" type="SPCSOType2" />
      <DO name="SPCSO3" type="SPCSOType3" />
      <DO name="SPCSO4" type="SPCSOType4" />
    </LNodeType>
    <DOType id="SPCSOType1" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="ctlModel" bType="INT8U" fc="CF"><Val>1</Val></DA>
      <DA name="operTimeout" bType="INT32U" fc="CF"><Val>1000</Val></DA>
    </DOType>
    <DOType id="SPCSOType2" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="ctlModel" bType="INT8U" fc="CF"><Val>2</Val></DA>
      <DA name="sboTimeout" bType="INT32U" fc="CF"><Val>1500</Val></DA>
      <DA name="operTimeout" bType="INT32U" fc="CF"><Val>1000</Val></DA>
    </DOType>
    <DOType id="SPCSOType3" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="ctlModel" bType="INT8U" fc="CF"><Val>3</Val></DA>
      <DA name="operTimeout" bType="INT32U" fc="CF"><Val>1000</Val></DA>
    </DOType>
    <DOType id="SPCSOType4" cdc="SPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="ctlModel" bType="INT8U" fc="CF"><Val>4</Val></DA>
      <DA name="sboTimeout" bType="INT32U" fc="CF"><Val>1500</Val></DA>
      <DA name="operTimeout" bType="INT32U" fc="CF"><Val>1000</Val></DA>
    </DOType>
    <LNodeType id="XCBRType" lnClass="XCBR">
      <DO name="Pos" type="PosType" />
      <DO name="Health" type="HealthType" />
      <DO name="SimCfg" type="SimCfgType" />
    </LNodeType>
    <DOType id="PosType" cdc="DPC">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="q" bType="Quality" fc="ST" />
      <DA name="t" bType="Timestamp" fc="ST" />
    </DOType>
    <DOType id="HealthType" cdc="SPS">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="q" bType="Quality" fc="ST" />
    </DOType>
    <DOType id="SimCfgType" cdc="SPG">
      <DA name="setVal" bType="BOOLEAN" fc="SP" />
    </DOType>
  </DataTypeTemplates>
</SCL>
