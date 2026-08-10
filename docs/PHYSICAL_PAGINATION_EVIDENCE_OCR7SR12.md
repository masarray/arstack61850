# Physical MMS pagination evidence — OCR7SR12

This document records the physical `GetNameList` continuation evidence observed
on the OCR7SR12 laboratory target after the Phase 4C read-only interoperability
work. It closes the previously pending requirement to demonstrate pagination on
a response that actually requires multiple pages.

## Environment

- target: `OCR7SR12`
- endpoint used in the lab: `192.168.1.10:102`
- association profile selected by arstack61850: `BalancedApTitle`
- operation class: read-only MMS discovery
- executable: `ariec61850_pagination_probe`
- command limits: `--timeout-ms 30000 --max-pages 256 --max-names 65536 --max-domains 4096 --require-pagination`

The Release build completed before the physical probe and the complete local CTest
suite reported 21/21 tests passing.

## Accepted probe summary

The physical run reported:

```text
Read-only GetNameList pagination probe:
endpoint=192.168.1.10:102,
associationProfile=BalancedApTitle,
associationAttempts=1,
queries=9,
paginatedQueries=4,
continuationRequests=88,
accepted=true.
```

Observed domain enumeration:

| Scope | Pages | Names | Continuations | Final state |
| --- | ---: | ---: | ---: | --- |
| VMD Domain | 1 | 4 | 0 | `moreFollows=false` |
| `OCR7SR12CTRL` NamedVariable | 27 | 2686 | 26 | final page 86 names |
| `OCR7SR12DR` NamedVariable | 3 | 276 | 2 | final page 76 names |
| `OCR7SR12MEAS` NamedVariable | 14 | 1347 | 13 | final page 47 names |
| `OCR7SR12PROT` NamedVariable | 48 | 4758 | 47 | final page 58 names |

`NamedVariableList` was also queried in each domain. The PROT domain exposed one
list named `LLN0$DataSet`; CTRL, DR and MEAS returned zero lists in this bounded
probe.

## Continuation behavior

For every multi-page variable query, the continuation request used the final
name returned by the previous page as `continueAfter`. The relay then returned
the next bounded page and retained `moreFollows=true` until the final page.

The largest observed sequence was `OCR7SR12PROT`:

- 48 pages;
- 4,758 names;
- 47 continuation requests;
- normally 100 names/page;
- final page 58 names;
- final response `moreFollows=false`.

This is materially stronger than a synthetic continuation test because the
client completed a long real-device sequence without repeating a page, losing
the continuation marker, exceeding the configured bounds, or terminating before
the relay cleared `moreFollows`.

## Acceptance conclusion

The Phase 4C.1 physical pagination continuation gate is **accepted for this
OCR7SR12 observation window**.

The evidence proves that arstack61850 can execute bounded real-device MMS
`GetNameList` continuation across multiple logical-device domains, including a
48-page sequence, on one fresh association. It does not by itself prove
multi-vendor pagination interoperability or every server-specific continuation
edge case.

## Remaining physical process-bus gates

The next active laboratory work is separate from this read-only MMS evidence:

- physical IEC 61850 SV transmit/capture interoperability;
- sustained ESP32-P4 SV timing/heap/TX-error evidence;
- later VLAN/priority validation subject to the selected Ethernet driver path;
- physical GOOSE transmit/subscriber interoperability.
