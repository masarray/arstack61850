<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Header id="AR_CLIENT_WORKBENCH" version="1" revision="A" />
  <IED name="CLIENT" manufacturer="AR" type="WorkbenchFixture" configVersion="1">
    <AccessPoint name="P1">
      <Server>
        <LDevice inst="LD0">
          <LN0 lnClass="LLN0" lnType="LLN0Type" />
          <LN lnClass="GGIO" inst="1" lnType="GGIOType">
            <DOI name="SetPoint"><DAI name="setVal"><Val>7</Val></DAI></DOI>
            <DOI name="Status"><DAI name="stVal"><Val>false</Val></DAI></DOI>
          </LN>
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="LLN0Type" lnClass="LLN0" />
    <LNodeType id="GGIOType" lnClass="GGIO">
      <DO name="SetPoint" type="SetPointType" />
      <DO name="Status" type="StatusType" />
    </LNodeType>
    <DOType id="SetPointType" cdc="ING">
      <DA name="setVal" bType="INT32" fc="SP" />
    </DOType>
    <DOType id="StatusType" cdc="SPS">
      <DA name="stVal" bType="BOOLEAN" fc="ST" />
      <DA name="q" bType="Quality" fc="ST" />
      <DA name="t" bType="Timestamp" fc="ST" />
    </DOType>
  </DataTypeTemplates>
</SCL>
