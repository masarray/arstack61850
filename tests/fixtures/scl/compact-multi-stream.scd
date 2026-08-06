<?xml version="1.0" encoding="UTF-8"?>
<SCL xmlns="http://www.iec.ch/61850/2003/SCL">
<Header id="AR_COMPACT_SV" version="1" revision="A"/>
<Communication><SubNetwork name="ProcessBus" type="8-MMS">
<ConnectedAP iedName="MU01_4I_4V_1" apName="LD0"><SMV ldInst="MU01" cbName="MSVCB01"><Address><P type="VLAN-ID">0</P><P type="VLAN-PRIORITY">4</P><P type="MAC-Address">01-0C-CD-04-00-01</P><P type="APPID">4000</P></Address></SMV></ConnectedAP>
<ConnectedAP iedName="MU01_4I_4V_2" apName="LD0"><SMV ldInst="MU01" cbName="MSVCB01"><Address><P type="VLAN-ID">0</P><P type="VLAN-PRIORITY">4</P><P type="MAC-Address">01-0C-CD-04-00-02</P><P type="APPID">4000</P></Address></SMV></ConnectedAP>
<ConnectedAP iedName="MU01_4I_4V_3" apName="LD0"><SMV ldInst="MU01" cbName="MSVCB01"><Address><P type="VLAN-ID">0</P><P type="VLAN-PRIORITY">4</P><P type="MAC-Address">01-0C-CD-04-00-03</P><P type="APPID">4000</P></Address></SMV></ConnectedAP>
</SubNetwork></Communication>
<IED name="MU01_4I_4V_1" manufacturer="AR" type="MergingUnit" configVersion="G"><AccessPoint name="LD0"><Server><LDevice inst="MU01"><LN0 lnClass="LLN0" lnType="MULLN0">
<DataSet name="PhsMeas1">
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="1" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="1" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="2" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="2" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="3" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="3" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="4" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="4" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="1" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="1" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="2" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="2" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="3" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="3" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="4" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="4" doName="VolSv" daName="q" fc="MX"/>
</DataSet>
<SampledValueControl multicast="true" name="MSVCB01" datSet="PhsMeas1" confRev="1" smvID="MU01_SV1" smpRate="4000" smpMod="SmpPerSec" nofASDU="1"/>
</LN0>
<LN lnType="CTR" lnClass="TCTR" inst="1"/>
<LN lnType="CTR" lnClass="TCTR" inst="2"/>
<LN lnType="CTR" lnClass="TCTR" inst="3"/>
<LN lnType="CTR" lnClass="TCTR" inst="4"/>
<LN lnType="VTR" lnClass="TVTR" inst="1"/>
<LN lnType="VTR" lnClass="TVTR" inst="2"/>
<LN lnType="VTR" lnClass="TVTR" inst="3"/>
<LN lnType="VTR" lnClass="TVTR" inst="4"/>
</LDevice></Server></AccessPoint></IED>
<IED name="MU01_4I_4V_2" manufacturer="AR" type="MergingUnit" configVersion="G"><AccessPoint name="LD0"><Server><LDevice inst="MU01"><LN0 lnClass="LLN0" lnType="MULLN0">
<DataSet name="PhsMeas1">
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="1" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="1" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="2" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="2" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="3" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="3" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="4" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="4" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="1" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="1" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="2" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="2" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="3" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="3" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="4" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="4" doName="VolSv" daName="q" fc="MX"/>
</DataSet>
<SampledValueControl multicast="true" name="MSVCB01" datSet="PhsMeas1" confRev="1" smvID="MU01_SV2" smpRate="4000" smpMod="SmpPerSec" nofASDU="1"/>
</LN0>
<LN lnType="CTR" lnClass="TCTR" inst="1"/>
<LN lnType="CTR" lnClass="TCTR" inst="2"/>
<LN lnType="CTR" lnClass="TCTR" inst="3"/>
<LN lnType="CTR" lnClass="TCTR" inst="4"/>
<LN lnType="VTR" lnClass="TVTR" inst="1"/>
<LN lnType="VTR" lnClass="TVTR" inst="2"/>
<LN lnType="VTR" lnClass="TVTR" inst="3"/>
<LN lnType="VTR" lnClass="TVTR" inst="4"/>
</LDevice></Server></AccessPoint></IED>
<IED name="MU01_4I_4V_3" manufacturer="AR" type="MergingUnit" configVersion="G"><AccessPoint name="LD0"><Server><LDevice inst="MU01"><LN0 lnClass="LLN0" lnType="MULLN0">
<DataSet name="PhsMeas1">
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="1" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="1" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="2" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="2" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="3" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="3" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="4" doName="AmpSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TCTR" lnInst="4" doName="AmpSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="1" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="1" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="2" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="2" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="3" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="3" doName="VolSv" daName="q" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="4" doName="VolSv" daName="instMag.i" fc="MX"/>
<FCDA ldInst="MU01" lnClass="TVTR" lnInst="4" doName="VolSv" daName="q" fc="MX"/>
</DataSet>
<SampledValueControl multicast="true" name="MSVCB01" datSet="PhsMeas1" confRev="1" smvID="MU01_SV3" smpRate="4000" smpMod="SmpPerSec" nofASDU="1"/>
</LN0>
<LN lnType="CTR" lnClass="TCTR" inst="1"/>
<LN lnType="CTR" lnClass="TCTR" inst="2"/>
<LN lnType="CTR" lnClass="TCTR" inst="3"/>
<LN lnType="CTR" lnClass="TCTR" inst="4"/>
<LN lnType="VTR" lnClass="TVTR" inst="1"/>
<LN lnType="VTR" lnClass="TVTR" inst="2"/>
<LN lnType="VTR" lnClass="TVTR" inst="3"/>
<LN lnType="VTR" lnClass="TVTR" inst="4"/>
</LDevice></Server></AccessPoint></IED>
<DataTypeTemplates>
<LNodeType id="MULLN0" lnClass="LLN0"/>
<LNodeType id="CTR" lnClass="TCTR"><DO name="AmpSv" type="AmpSAV"/></LNodeType>
<LNodeType id="VTR" lnClass="TVTR"><DO name="VolSv" type="VolSAV"/></LNodeType>
<DOType id="AmpSAV" cdc="SAV"><DA name="instMag" bType="Struct" type="AnalogueValue" fc="MX"/><DA name="q" bType="Quality" fc="MX"/></DOType>
<DOType id="VolSAV" cdc="SAV"><DA name="instMag" bType="Struct" type="AnalogueValue" fc="MX"/><DA name="q" bType="Quality" fc="MX"/></DOType>
<DAType id="AnalogueValue"><BDA name="i" bType="INT32"/></DAType>
</DataTypeTemplates></SCL>
