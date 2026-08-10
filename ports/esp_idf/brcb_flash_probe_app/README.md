# ESP32-P4 BRCB flash geometry / latency probe

This image validates the physical non-volatile backend used by the bounded BRCB
A/B checkpoint journal. It is intentionally separate from the normal SV trial
firmware because the latency probe performs destructive flash operations.

## Partition policy

The application-defined `brcb_state` partition uses type `0x40`, subtype `0x01`.
The adapter reads the actual `esp_partition_t::erase_size` at boot and requires at
least three erase units:

1. erase unit 0: journal bank A;
2. erase unit 1: journal bank B;
3. erase unit 2: destructive measurement probe.

Only the first two units are exposed through `MmsStaticBrcbStorageBackend`.
`run_latency_probe()` cannot address either journal bank.

Each checkpoint attempt that reaches the storage erase stage erases exactly one
journal erase unit. A/B alternation means one bank sees at most
`ceil(checkpoint_erase_attempts / 2)` erase cycles. No flash endurance rating is
hard-coded; use the actual flash/device rating when converting this counter into
an endurance budget.

The current CSV reserves `0x3000` bytes. On a target reporting a `0x1000` erase
size this maps exactly to three erase units. If the runtime geometry differs, the
adapter fails closed instead of silently changing the journal layout.

Encrypted and read-only partitions are rejected by this first backend. Encrypted
partition write alignment and power-loss behavior require a separate acceptance
slice before they can be enabled.

## Measurement transaction

One probe run performs, only in the dedicated probe unit:

1. full erase;
2. 40-byte header write;
3. 3072-byte synthetic state-payload write;
4. 32-byte footer write at the fixed bank footer position;
5. full read/verify of header, payload and footer;
6. cleanup erase.

A successful run therefore consumes two erase cycles on the **probe** unit and
zero erase cycles on the journal banks.

The UART output uses machine-readable records:

```text
ARBRCB_FLASH {"stage":"geometry",...}
ARBRCB_FLASH {"stage":"latency-probe",...}
```

The second record reports prepare-erase, header/payload/footer write, verify-read
and cleanup-erase latency in microseconds. Timing is measured with
`esp_timer_get_time()`.

## Build and physical run

From an ESP-IDF v6.0.2 environment:

```text
idf.py set-target esp32p4
idf.py build
idf.py -p <PORT> flash monitor
```

Run the probe only when a destructive measurement of its reserved scratch sector
is intended. Do not use repeated probe executions as an endurance test unless a
separate wear-test plan and erase-cycle budget have been approved.

## Evidence boundary

Hosted CI can prove that the adapter, custom partition table and probe firmware
cross-compile for ESP32-P4. It cannot provide physical flash latency, real
power-cut survival or endurance data. Those labels are reserved for captured
board output and controlled hardware tests.
