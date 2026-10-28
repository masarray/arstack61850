# M4-P0E3B2 — Boot-scoped selected-roster reconnect guard

A verified ESP32-P4 advertising MULTISV-TX-LAB-PREVIEW now enters a bounded read-only `MULTISV SHOW` probe **before** the Studio supervisor can automatically synchronize a V1 profile. The mode is PENDING until canonical STATUS + TXMODE + the three STREAM identities arrive. Mode SELECTED prohibits any automatic V1 PROFILE/START; the device instead initiates an existing three-MU read-only bank inspection, leaving GUI drafts untouched and respecting ongoing autonomous RUNNING state. A 5 s timeout or malformed response yields UNKNOWN and blocks automatic V1 writes (never guesses LEGACY). A TXMODE-selected=0 board is LEGACY only after valid STATUS, preserving V1 behavior. No new serial writer/workflow or TX hot-path cost.

This gate does **not** prove SCD/roster identity match for safe group START, and does not resume or start physical Ethernet output. Those validations remain P0E3B3/P0E3C, followed by physical P0F.
