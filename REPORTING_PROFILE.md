# Offline MMS Reporting Profile

Phase 4A adds deterministic, read-only reporting analysis above the MMS service codecs.
It does not establish an association or transmit a request.

## Inventory

`MmsReportInventoryBuilder` consumes paged `GetNameList` evidence that has already been
collected by a caller. Named-variable-list names become DataSet candidates. Variables
containing `$BR$` or `$RP$` are grouped into buffered and unbuffered report-control
candidates, including the discovered attribute names.

## DataSet directory

`MmsDataSetDirectoryCodec` implements service tag 12,
GetNamedVariableListAttributes. Domain-specific member names are normalized from MMS
form, for example `LD0/GGIO1$ST$Ind1$stVal`, to
`LD0/GGIO1.Ind1.stVal`, while retaining the original ObjectName and functional
constraint.

## InformationReport order

The exact decoder follows `OptFlds` order:

1. `RptID`
2. `OptFlds`
3. optional `SqNum`
4. optional `TimeOfEntry`
5. optional `DatSet`
6. optional `BufOvfl`
7. optional `EntryID`
8. optional `ConfRev`
9. optional `SubSqNum` and `MoreSegmentsFollow`
10. inclusion bit string
11. included values
12. optional data-reference list
13. optional reason-for-inclusion list

Unknown trailing access results, missing mandatory fields, invalid bit strings, and
DataSet indexes outside the supplied directory are rejected.

## Monitoring

`MmsReportSequenceTracker` records sequence gaps, duplicates, wraps, resets,
configuration revision changes, DataSet changes, buffer overflow, and segmented-report
continuity. `MmsOfflineReportMonitor` bounds both the number of streams and retained
frames per stream and evicts the least-recent stream deterministically.

## Safety boundary

All APIs operate on supplied byte arrays and supplied discovery/read evidence. No TCP
socket, IED association, live RCB enable, reservation, GI trigger, read, write, or
control operation is enabled by this phase.
