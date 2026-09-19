<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Header id="AR_CLIENT_WORKBENCH_INCOMPATIBLE" version="1" revision="A" />
  <Communication>
    <SubNetwork name="StationBus" type="8-MMS">
      <ConnectedAP iedName="OTHER" apName="P1">
        <Address>
          <P type="IP">127.0.0.1</P>
          <P type="OSI-AP-Title">1,3,9999,23</P>
          <P type="OSI-AE-Qualifier">23</P>
          <P type="OSI-PSEL">00 00 00 23</P>
          <P type="OSI-SSEL">00 23</P>
          <P type="OSI-TSEL">00 23</P>
        </Address>
      </ConnectedAP>
    </SubNetwork>
  </Communication>
  <IED name="OTHER" manufacturer="AR" type="WorkbenchMismatchFixture" configVersion="1">
    <AccessPoint name="P1">
      <Server>
        <LDevice inst="LD0">
          <LN0 lnClass="LLN0" lnType="LLN0Type" />
          <LN lnClass="GGIO" inst="1" lnType="GGIOType">
            <DOI name="SetPoint"><DAI name="setVal"><Val>7</Val></DAI></DOI>
          </LN>
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="LLN0Type" lnClass="LLN0" />
    <LNodeType id="GGIOType" lnClass="GGIO">
      <DO name="SetPoint" type="SetPointType" />
    </LNodeType>
    <DOType id="SetPointType" cdc="ING">
      <DA name="setVal" bType="INT32" fc="SP" />
    </DOType>
  </DataTypeTemplates>
</SCL>