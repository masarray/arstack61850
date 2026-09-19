# external IEC 61850 client Server Golden Profile

Status: external behavioral evidence for ARStack IED Simulator parity work.  
Tracking: Issue #95 and PR #82.

## Capture identity

Golden capture: `external IEC 61850 client_As_Server_Discovery.pcapng`  
SHA-256: `6f90cb4aa87326f07d569ebf40575d15841831259cc52e21f2e847b86d11ab99`

The capture records external vendor external IEC 61850 client operating as the IEC 61850 server for the same reference IEC 61850 model engineering model used by the ARStack simulator parity work. The values below are measured capture evidence, not generic IEC 61850 constants.

## Association/discovery sequence

The capture contains 418 MMS confirmed request/response exchanges with invoke IDs `1..418` in strict increasing order.

Measured service totals:

| Service | Count |
| --- | ---: |
| GetNameList | 140 |
| GetVariableAccessAttributes | 119 |
| Read | 157 |
| GetNamedVariableListAttributes | 2 |

No Write/Control mutation is part of this discovery profile.

## Directory behavior

The initial VMD-domain GetNameList returns 32 MMS domains. Domain order follows the engineering model's LogicalDevice order.

NamedVariable discovery then consists of 105 domain-specific pages containing 8,741 identifiers in total. The server returns at most 100 identifiers per page. Seventy-three continuation requests were observed, and every `continueAfter` value is exactly the final identifier from the preceding page.

The directory is hierarchical, not leaf-only. Representative names include:

```text
LLN0
LLN0$ST
LLN0$ST$Beh
LLN0$ST$Beh$stVal
LLN0$ST$Beh$q
LLN0$ST$Beh$t
LLN0$CF
LLN0$DC
LLN0$SP
LLN0$RP
LLN0$RP$A_URCB01
LLN0$RP$A_URCB01$RptID
...
```

The Application domain similarly advertises `LLN0$BR`, concrete BRCB roots such as `LLN0$BR$Buffer01`, `LLN0$RP`, concrete URCB roots such as `LLN0$RP$Unbuffer01`, and their attributes.

### Required ARStack invariant

A hierarchy name advertised by GetNameList must be readable and type-queryable; a readable/type-queryable hierarchy node must be represented in the same canonical directory. GetNameList, GVAA and Read must therefore project from one canonical node graph rather than three independently synthesized views.

## GVAA behavior

There are 119 GetVariableAccessAttributes requests. Every observed GVAA target is a Logical Node root rather than an individual leaf. The response TypeSpecification is a full nested MMS Structure with named components.

For the Application LLN0, the top-level FC components observed in the TypeSpecification include:

```text
ST, CO, CF, DC, SP, SG, RP, BR, SE, EX, OR
```

The exact FC set depends on the Logical Node. ARStack must derive it from the canonical model and preserve deterministic semantic ordering; it must not hard-code the Application example as a global FC list.

## Read behavior

The capture contains 157 Read requests covering 869 object references.

617 references are FC-root reads. The remaining reads include deeper service/data structures such as concrete RCB roots and metadata objects.

The Read response structure matches the hierarchy/type view used for GVAA. This is evidence for compiling data and type projections from the same immutable hierarchy.

## ReportControl instance behavior

The source SCL contains 32 ReportControl definitions, but the golden server exposes 34 concrete RCB instances. Runtime RCB instance count is therefore distinct from SCL ReportControl definition count.

The difference comes from indexed ReportControl instance expansion using `RptEnabled max`.

Observed examples:

```text
Application / Buffer   -> Buffer01, Buffer02
Application / Unbuffer -> Unbuffer01, Unbuffer02
```

ADD indexed URCBs similarly expose a two-digit instance suffix, including the max=1 case.

### Golden URCB root schema

A concrete URCB root Read returns a 12-component MMS Structure in this order:

```text
RptID
RptEna
Resv
DatSet
ConfRev
OptFlds
BufTm
SqNum
TrgOps
IntgPd
GI
Owner
```

Representative initial state observed for `AA1E1F06R4ADD/LLN0$RP$A_URCB01`:

- RptID: configured report identity
- RptEna: false
- Resv: false
- DatSet: empty for that free/unbound instance
- ConfRev: 1
- BufTm: 0
- SqNum: 0
- IntgPd: 0
- GI: false
- Owner: empty

### Golden BRCB root schema

A concrete BRCB root Read returns a 15-component MMS Structure in this order:

```text
RptID
RptEna
DatSet
ConfRev
OptFlds
BufTm
SqNum
TrgOps
IntgPd
GI
PurgeBuf
EntryID
TimeofEntry
ResvTms
Owner
```

The ARStack compatibility facade must use this order for both synthesized TypeSpecification and Read values.

## Normal/process semantic defaults

The golden server does not represent an unedited simulator as an all-zero data model.

For `LLN0$ST`, the representative normal state contains standard DOs `Beh`, `Health` and `Mod` with:

```text
stVal = 1
q     = Good, test=false
t     = valid current-ish UTC timestamp
```

This is particularly important because a generic Enumeration=`0` fallback can make an engineering client classify the simulated IED as off/test-related instead of normal/process.

ARStack default-value precedence must be:

1. explicit instance value from SCL (`DAI/Val`);
2. standard semantic default derived from LN/DO/DA/CDC/bType/FC plus IED/header/model context;
3. conservative type-safe fallback.

Examples of semantic defaults when no explicit SCL value exists:

- `Mod.stVal` -> normal/on semantic code (`1` for this standard enum);
- `Beh.stVal` -> normal/on semantic code;
- `Health.stVal` -> OK semantic code;
- Quality -> Good with test=false and operatorBlocked=false;
- Timestamp -> simulation start/current UTC, not Unix epoch zero;
- `ctlModel` -> configured SCL value when present; otherwise preserve safe/status-only semantics;
- identity/NamePlate strings -> derive from SCL metadata where the standard object meaning is unambiguous.

Do not implement a global `Enumeration=1` rule. Enumeration semantics differ across standard types such as Dbpos/DPC, control models, health and mode values.

## DataSet behavior

The golden discovery queries DataSet inventory and then GetNamedVariableListAttributes for configured DataSets. Member ordering is the configured SCL order. Discovery does not mutate or dynamically rebuild configured DataSets.

## Transport/segmentation behavior

Large MMS responses are segmented at the COTP layer. The capture includes response TSDUs requiring multiple COTP data segments; the largest observed assembled response is several kilobytes while individual TPKTs remain close to the negotiated TPDU size.

ARStack must retain one logical MMS response and segment only at transport, rather than truncating application data to one TPKT.

## Architecture derived from the evidence

### Canonical immutable model compiler

Compile the selected SCL/CID once into a stable graph:

```text
IED
  -> MMS domain / LogicalDevice
      -> LogicalNode
          -> FC
              -> DataObject / structured component
                  -> DataAttribute / BDA
```

Service objects such as DataSets, RCBs, SGCB and control objects attach to this graph. Intermediate hierarchy nodes are virtual and do not need one concrete `MmsStaticObjectEntry` each.

A flattened immutable directory index is derived once from the graph for GetNameList pagination. GVAA and Read reference the same graph. This prevents inconsistent views and avoids rebuilding trees on every request.

### Memory and lifetime

- concrete leaves/service endpoints retain bounded object storage;
- virtual nodes live in one compiled hierarchy/directory, not duplicated per association;
- TypeSpecifications are compiled once and backing storage remains stable;
- per-association mutable state contains only ownership/reservation/report enable/session state and bounded scratch buffers;
- RAII owns sockets and workers;
- no detached per-RCB worker/thread;
- no per-request unbounded heap graph reconstruction.

### Live updates and coalescing

Keep ARStack's generation/revision checks and bounded live-update history. Coalesce repeated updates to the same point before they become a retained report event.

For report scheduling, coalescing is permitted only while an event is still pending in its BufferTime window. Once a BRCB report entry is retained, later changes must create later retained state rather than rewrite historical evidence.

### Scheduler

Use one bounded scheduler per IED (or equivalent centralized timed-event queue) for BufferTime, integrity and GI work. Do not spawn one timer/thread per RCB. Preserve deterministic ordering for equal-deadline work.

## Acceptance gates

Internal regression must lock the measured behavior without converting model-specific counts into generic protocol constants. The reference IEC 61850 model fixture should prove the exact capture shape where appropriate:

- 32 domains;
- 8,741 discoverable NamedVariable identifiers;
- 105 NamedVariable pages at max 100/page;
- exact continuation token behavior;
- 119 LN-root GVAAs;
- 34 runtime RCB instances from 32 ReportControl definitions;
- exact URCB/BRCB root field order;
- normal LLN0 Mod/Beh/Health defaults;
- valid non-zero timestamps;
- large-response COTP segmentation.

Final closure still requires a real external IEC 61850 client retest against an ARStack RC. Simulator/unit/CI evidence alone is not external interoperability proof.
