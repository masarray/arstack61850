<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Header id="ARSTACK_SV_4800_STRUCTURED" version="1" revision="A"/>
  <Communication>
    <SubNetwork name="ProcessBus" type="8-MMS">
      <ConnectedAP iedName="MU_A" apName="AP1">
        <SMV ldInst="MU01" cbName="MSVCB">
          <Address>
            <P type="VLAN-ID">0</P>
            <P type="VLAN-PRIORITY">4</P>
            <P type="MAC-Address">01-0C-CD-04-00-00</P>
            <P type="APPID">4001</P>
          </Address>
        </SMV>
      </ConnectedAP>
    </SubNetwork>
  </Communication>
  <IED name="MU_A" type="GENERIC_MU" configVersion="1">
    <AccessPoint name="AP1">
      <Server>
        <LDevice inst="MU01">
          <LN0 lnClass="LLN0" inst="" lnType="LLN0_GENERIC">
            <DataSet name="MSV">
              <FCDA ldInst="MU01" prefix="I01A" lnClass="TCTR" lnInst="1" doName="Amp" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01B" lnClass="TCTR" lnInst="2" doName="Amp" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01C" lnClass="TCTR" lnInst="3" doName="Amp" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01N" lnClass="TCTR" lnInst="4" doName="Amp" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01A" lnClass="TVTR" lnInst="1" doName="Vol" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01B" lnClass="TVTR" lnInst="2" doName="Vol" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01C" lnClass="TVTR" lnInst="3" doName="Vol" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01N" lnClass="TVTR" lnInst="4" doName="Vol" fc="MX"/>
            </DataSet>
            <SampledValueControl multicast="true" name="MSVCB" datSet="MSV" confRev="100"
                smvID="MU_A/MU01/LLN0.MSVCB" smpRate="4800" nofASDU="1" smpMod="SmpPerSec">
              <SmvOpts/>
            </SampledValueControl>
          </LN0>
          <LN prefix="I01A" lnClass="TCTR" inst="1" lnType="TCTR_GENERIC"/>
          <LN prefix="I01B" lnClass="TCTR" inst="2" lnType="TCTR_GENERIC"/>
          <LN prefix="I01C" lnClass="TCTR" inst="3" lnType="TCTR_GENERIC"/>
          <LN prefix="I01N" lnClass="TCTR" inst="4" lnType="TCTR_GENERIC"/>
          <LN prefix="U01A" lnClass="TVTR" inst="1" lnType="TVTR_GENERIC"/>
          <LN prefix="U01B" lnClass="TVTR" inst="2" lnType="TVTR_GENERIC"/>
          <LN prefix="U01C" lnClass="TVTR" inst="3" lnType="TVTR_GENERIC"/>
          <LN prefix="U01N" lnClass="TVTR" inst="4" lnType="TVTR_GENERIC"/>
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="LLN0_GENERIC" lnClass="LLN0"/>
    <LNodeType id="TCTR_GENERIC" lnClass="TCTR">
      <DO name="Amp" type="SAV_FIXED_INT32"/>
    </LNodeType>
    <LNodeType id="TVTR_GENERIC" lnClass="TVTR">
      <DO name="Vol" type="SAV_FIXED_INT32"/>
    </LNodeType>
    <DOType id="SAV_FIXED_INT32" cdc="SAV">
      <DA name="instMag" bType="Struct" type="ANALOGUE_INT32" fc="MX"/>
      <DA name="q" bType="Quality" fc="MX"/>
      <DA name="sVC" bType="Struct" type="SCALED_VALUE_CONFIG" fc="CF"/>
    </DOType>
    <DAType id="ANALOGUE_INT32">
      <BDA name="i" bType="INT32"/>
    </DAType>
    <DAType id="SCALED_VALUE_CONFIG">
      <BDA name="scaleFactor" bType="FLOAT32"/>
      <BDA name="offset" bType="FLOAT32"/>
    </DAType>
  </DataTypeTemplates>
</SCL>
