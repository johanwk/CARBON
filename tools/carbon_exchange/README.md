# CARBON exchange prototype

Run from the repository root with Python3; no extra dependencies for offline use.

```sh
python3 -m tools.carbon_exchange inspect build/song19-readonly-20261014-220549.bin
python3 -m unittest discover -s tests -p test_exchange.py -v
```

`inspect` shows the six slots of a selected shared track/step (use `--track`
and `--step`). `diff BEFORE AFTER` lists event changes, changed byte offsets,
opaque/unmodelled changes and affected sectors.

`patch-event IMAGE NEW_FILE --base-sha256 HASH --track 1 --step 1 --slot 2
--data1 97` creates a **local file only**, never overwrites an existing file,
and prints the diff. Track/step/slot numbering is one-based; event data values
are raw firmware values. Note numbers use MIDI60=C4, lengths are96-PPQ ticks.
Missing patch fields are retained; explicit type0 clears an event without
compacting slots or zeroing its stale payload. No independent per-note delay.

## Scope and separation

- `model.py`: event fields/type validation and one-based addressing.
- `codec.py`: lossless raw image envelope, event decoding/patching, diffs and
  offline sector plans. Globals and scene/track parameters are still opaque;
  this is NOT yet a complete declarative song compiler. Unknown payloads and
  reserved bytes are retained. Authoring is restricted to numeric1.34.
- `transport.py`: pure packet encoders/response decoder and callback-based
  read-only capture. No installed MIDI backend, device selection or uploader
  CLI yet. Its write packet generation is offline, NOT write execution.
- Org parser and MIDI importer are not implemented; they should consume the
  shared validated model rather than interpret transport/binary bytes directly.

`sector_plan(before, after, slot=..., mode=...)` is offline. `before` MUST be
a fresh backup of the explicit target slot for eventual upload. `replace`
plans all five sectors; `patch` plans only changed sectors, always full4096-byte
read-modify-write images. No plan itself proves that the device base is fresh.

## Firmware protocol requirements

Write packets require hardened candidate0623fee (`1.34-johanwk-sx1`, label
truncated on screen). Do not send them to stock/older firmware. No capability
query currently identifies protocol revisions, so installation must be verified
explicitly. Send offset0 first to reset staging coverage, wait for each stage
ACK, then commit and wait for the final ACK. Completion is not readback
verification. Never retry a timed-out commit blindly.

Future executor needs explicit authorization/target spare slot, stopped
transport, two matching backups, base-hash freshness, approved semantic diff,
serialized panel/config I/O, and full readback comparison. Whole-song writes
are non-atomic; power loss can corrupt the selected slot. Panel reload is
required. This prototype does not authorize or perform hardware writes.
