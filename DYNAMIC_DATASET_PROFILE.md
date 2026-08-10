# Dynamic DataSet Profile

## Scope

This profile ports the ARIEC61850 dynamic DataSet lifecycle to the C++ stack without coupling it to IEC 61850 control sequencing.

Implemented MMS services:

- `DefineNamedVariableList` — service choice `[11]`
- `DeleteNamedVariableList` — service choice `[13]`
- `GetNamedVariableListAttributes` verification through the existing DataSet-directory codec

The codec is symmetric: request and response encode/decode are available so the same wire model can be reused by a future MMS server dispatcher.

## Reference and member rules

Dynamic DataSets use domain-specific IEC 61850 references:

`LD/LN.DataSetName`

The operational runtime also accepts `LD/DataSetName` and normalizes it to `LD/LLN0.DataSetName`, matching the C# oracle behavior.

Members are ordered domain-specific MMS named variables. Duplicate members are rejected. Member ordering is treated as semantic because IEC 61850 report values are mapped to DataSet members by index.

## Safe create lifecycle

`MmsDynamicDataSetRuntime::create()` requires an already associated `MmsAssociationRuntime` and performs:

1. validate the DataSet identity and bounded member list;
2. send exactly one `DefineNamedVariableList` request;
3. require a matching successful confirmed response;
4. by default, issue `GetNamedVariableListAttributes` for the new DataSet;
5. require exact member count, content, and ordering;
6. record local ownership only after the create path succeeds.

If verification fails after a successful Define, the runtime attempts one best-effort Delete rollback and then propagates the original failure. It does not retry creation automatically.

The operational default is 64 members. Callers can raise the limit explicitly up to the bounded codec maximum after validating target-IED payload and resource limits.

## Safe delete lifecycle

Deletion is default-deny for DataSets the runtime did not create.

`owned_only` is the default policy. `explicit_override` must be selected deliberately to delete an unowned DataSet. This prevents a reporting client from accidentally deleting a static or another client's DataSet merely because the reference is known.

No network operation is performed from a destructor. Cleanup is always explicit so shutdown and association-loss behavior remain observable.

## C# parity evidence

The dedicated C++ test target includes the ARIEC61850 C# oracle vectors for:

- successful `DefineNamedVariableList` response: `A10502010A8B00`;
- successful `DeleteNamedVariableList` matched/deleted response: `A10B02010BAD06800101810101`;
- Define request service/member shape;
- Delete request service/list-name shape.

The tests also round-trip both request types, reject empty/duplicate/invalid definitions, verify invoke-ID correlation, and require an active association before any mutation.

## Acceptance boundary

Hosted CI can establish deterministic wire parity, parser/encoder safety, portability, and integration with the existing association runtime. It cannot establish physical IED mutation interoperability.

Before claiming live Dynamic DataSet interoperability, retain evidence from a non-operational lab IED showing:

1. `DefineNamedVariableList` accepted for a small DataSet;
2. immediate `GetNamedVariableListAttributes` returns the exact requested ordered members;
3. an available RCB can bind to that DataSet and report values map by the verified indexes;
4. `DeleteNamedVariableList` reports the expected matched/deleted counts after RCB disable/unbind;
5. reconnect and failure cases do not cause an unrelated/static DataSet to be deleted.

Smart Control is intentionally outside this profile and may progress independently.
