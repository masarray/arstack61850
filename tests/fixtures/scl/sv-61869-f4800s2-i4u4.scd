<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL" version="2007" revision="B">
  <Header id="ARSTACK_IEC61869_9_F4800S2_I4U4" version="1" revision="A"/>
  <Communication>
    <SubNetwork name="ProcessBus" type="8-MMS">
      <ConnectedAP iedName="MU_61869" apName="AP1">
        <SMV ldInst="MU01" cbName="MSVCB01">
          <Address>
            <P type="VLAN-ID">0</P>
            <P type="VLAN-PRIORITY">4</P>
            <P type="MAC-Address">01-0C-CD-04-00-10</P>
            <P type="APPID">4010</P>
          </Address>
        </SMV>
      </ConnectedAP>
    </SubNetwork>
  </Communication>
  <IED name="MU_61869" type="GENERIC_IEC61869_9" configVersion="1">
    <AccessPoint name="AP1">
      <Server>
        <LDevice inst="MU01">
          <LN0 lnClass="LLN0" inst="" lnType="LLN0_GENERIC">
            <DataSet name="PhsMeas1">
              <FCDA ldInst="MU01" prefix="I01A" lnClass="TCTR" lnInst="1" doName="AmpSv" daName="instMag.i" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01A" lnClass="TCTR" lnInst="1" doName="AmpSv" daName="q" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01B" lnClass="TCTR" lnInst="2" doName="AmpSv" daName="instMag.i" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01B" lnClass="TCTR" lnInst="2" doName="AmpSv" daName="q" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01C" lnClass="TCTR" lnInst="3" doName="AmpSv" daName="instMag.i" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01C" lnClass="TCTR" lnInst="3" doName="AmpSv" daName="q" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01N" lnClass="TCTR" lnInst="4" doName="AmpSv" daName="instMag.i" fc="MX"/>
              <FCDA ldInst="MU01" prefix="I01N" lnClass="TCTR" lnInst="4" doName="AmpSv" daName="q" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01A" lnClass="TVTR" lnInst="1" doName="VolSv" daName="instMag.i" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01A" lnClass="TVTR" lnInst="1" doName="VolSv" daName="q" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01B" lnClass="TVTR" lnInst="2" doName="VolSv" daName="instMag.i" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01B" lnClass="TVTR" lnInst="2" doName="VolSv" daName="q" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01C" lnClass="TVTR" lnInst="3" doName="VolSv" daName="instMag.i" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01C" lnClass="TVTR" lnInst="3" doName="VolSv" daName="q" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01N" lnClass="TVTR" lnInst="4" doName="VolSv" daName="instMag.i" fc="MX"/>
              <FCDA ldInst="MU01" prefix="U01N" lnClass="TVTR" lnInst="4" doName="VolSv" daName="q" fc="MX"/>
            </DataSet>
            <SampledValueControl multicast="true" name="MSVCB01" datSet="PhsMeas1"
                confRev="1" smvID="MU_61869/MU01/LLN0.MSVCB01"
                smpRate="4800" nofASDU="2" smpMod="SmpPerSec">
              <SmvOpts refreshTime="false" sampleSynchronized="true"
                  sampleRate="false" dataSet="false" security="false"/>
            </SampledValueControl>
          </LN0>
          <LN prefix="I01A" lnClass="TCTR" inst="1" lnType="TCTR_61869"/>
          <LN prefix="I01B" lnClass="TCTR" inst="2" lnType="TCTR_61869"/>
          <LN prefix="I01C" lnClass="TCTR" inst="3" lnType="TCTR_61869"/>
          <LN prefix="I01N" lnClass="TCTR" inst="4" lnType="TCTR_61869"/>
          <LN prefix="U01A" lnClass="TVTR" inst="1" lnType="TVTR_61869"/>
          <LN prefix="U01B" lnClass="TVTR" inst="2" lnType="TVTR_61869"/>
          <LN prefix="U01C" lnClass="TVTR" inst="3" lnType="TVTR_61869"/>
          <LN prefix="U01N" lnClass="TVTR" inst="4" lnType="TVTR_61869"/>
        </LDevice>
      </Server>
    </AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="LLN0_GENERIC" lnClass="LLN0"/>
    <LNodeType id="TCTR_61869" lnClass="TCTR"><DO name="AmpSv" type="SAV_61869"/></LNodeType>
    <LNodeType id="TVTR_61869" lnClass="TVTR"><DO name="VolSv" type="SAV_61869"/></LNodeType>
    <DOType id="SAV_61869" cdc="SAV">
      <DA name="instMag" bType="Struct" type="ANALOGUE_INT32" fc="MX"/>
      <DA name="q" bType="Quality" fc="MX"/>
    </DOType>
    <DAType id="ANALOGUE_INT32"><BDA name="i" bType="INT32"/></DAType>
  </DataTypeTemplates>
</SCL>
