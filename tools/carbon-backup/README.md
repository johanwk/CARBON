# CARBON song backup and controlled restore

Native Rust CLI, tested on Apple Silicon macOS/CoreMIDI with hardened firmware
0623fee. Dependencies pinned by Cargo.lock; executable needs no Python or Cargo
to run. Other platforms remain untested.

## Build and verification

```sh
cargo test --locked --manifest-path tools/carbon-backup/Cargo.toml
cargo build --release --locked --manifest-path tools/carbon-backup/Cargo.toml
tools/carbon-backup/target/release/carbon-backup ports
```

C layout assertions in tests/exchange_layout.c.in cross-check key offsets
against production song.c. Rust tests exercise archive integrity, bounds,
source validation, plans, fake-device restore, each325-message failure
position, readback mismatch and deadline/ACK behavior. Hardware feedback
confirms bank/song backups and song19 restored into30 with matching full
readback and successful panel load/playback. This is not exhaustive fault
recovery, DMA timing or platform coverage.

## Read-only backup

Stop transport, save wanted edits yourself, and leave the panel undisturbed.
Use exact USB Port1 endpoints. Commands below assume repository root:

```sh
tools/carbon-backup/target/release/carbon-backup backup-song \
  19 build/song19.backup "CARBON Port 1" "CARBON Port 1" \
  --stopped --pause-ms 0
tools/carbon-backup/target/release/carbon-backup backup-bank \
  build/carbon-bank.backup "CARBON Port 1" "CARBON Port 1" \
  --stopped --pause-ms 0
tools/carbon-backup/target/release/carbon-backup verify build/song19.backup
```

`--stopped` is user confirmation, not automatic detection/stop. Backup sends
only0x70;64-byte reads serialized with3-second timeout, no retries. Two full
captures must match. `--pause-ms` allows0..100 (default5);0ms demonstrated on
this setup, around3seconds/two-pass song, around99seconds/bank pass.

All raw slots preserved, including blank/invalid records. Archives exclude
firmware, system configuration/global preset-pattern definitions and unsaved
RAM. For faithful musical recovery, customized global patterns need separate
backup support later. Outputs exclusively created; disk failure can leave an
incomplete file, rejected by verify. Sync/reopen verification precedes success.
Copy valuable archives outside build directory.

## Archive formats

Bank: ASCII8-byte `CARBNBK1`, little-endian uint32 JSON length, UTF-8 JSON,
exactly1310720 raw bank bytes (panel slots1..64), no trailing bytes. Manifest
includes schema/timestamp/ports/two passes, slot address/size/hash/magic status
and bank SHA256. Song: same envelope with `CARBNSG1`,20480raw bytes, original
slot/address/hash/magic status. Pacing and timings may be included. Hashes
detect corruption, not malicious modification. Neither archive is a raw .bin.

## Offline preview

```sh
tools/carbon-backup/target/release/carbon-backup plan-restore \
  build/song19.backup build/song30-before-test.backup 30 replace
```

`replace` plans five complete sectors; `patch` plans changed sectors and is
currently restricted to shared-event-region changes. Both archives verify,
base slot must equal explicit target. Plans show raw event/field diffs, hashes,
sectors and token bound to target/mode/base/result. Preview never opens MIDI.

Stored1.34 fields/events are validated conservatively; inactive payload,
padding and reserved bytes preserved. Legacy target backups retain raw
version-specific extension bytes: comparisons currently display those raw
bytes, NOT effective migrated values. Legacy SOURCE recovery is not yet
supported (source must be1.34), though legacy slots can be backed up exactly.

## Controlled hardware restore (initial scope: slot30 only)

```text
carbon-backup restore-song SOURCE TARGET_BACKUP 30 RECOVERY_FILE \
  "CARBON Port 1" --stopped --firmware-0623fee APPROVAL_TOKEN
```

**This command WRITES flash.** Explicitly approve the preview before invocation.
Initial CLI limited to replacement into disposable slot30 and exact Port1.
Firmware flag asserts known installation; no capability query exists. Never
use stock/older broken0x72/73 firmware. Song source19 is not written.

Two fresh reads must equal the reviewed base. A NEW recovery archive is
created/synced/reverified before first write. Each sector sends offset0 first,
then63other64-byte stages with ACKs; commit waits for completion ACK (30-second
deadline). All five sectors then receive full-song readback comparison.
Errors abort without retries; after error stop and retain backups. A timeout
may mean write occurred: do not blindly retry. Whole-song replacement is
non-atomic and power loss can damage target. Do not save/load/edit panel during
transfer. On success LOAD target from panel; don't save stale RAM over it.

Modules: song.rs stored fields; restore.rs plans/validated executor; transfer.rs
bounded adapter; midi.rs hardware Link. Backup CLI still sends only reads;
write commands exist solely on explicitly invoked restore path. Org/MIDI import
and general patch CLI/hardware tests remain future work.

## JSON song document v1 prototype (offline)

```sh
tools/carbon-backup/target/release/carbon-backup export-json build/song19.backup build/song19.json
tools/carbon-backup/target/release/carbon-backup import-json build/song19.json build/song19-compiled.backup
```

Both commands are offline/exclusive-create. Complete schema1 documents require
all6tracks/64steps/6orderedslots, named stored settings,64song-list entries,
metadata and raw provenance. settings uses scoped keys like
`scene1.track1.step_length_index`; shared steps hold delay/ratchet separately.
Events use tagged kinds and named parameters (Note pitch/velocity/length_ticks,
CC/CCi controller/value, etc.). Numeric MIDI60=C4,96PPQ ticks. Hrm special
choice values are currently raw enum indices.

Original20480bytes are embedded as raw_hex with SHA256 to preserve inactive
payloads/padding/reserved bytes; edited fields overlay that base. Unchanged
song19 round-trips byte-identically. This initial format requires a valid1.34
base; no legacy migration or base-free authored defaults yet.

Missing/unknown structured fields and duplicate/out-of-order slot identities
are rejected. Future independent note delay is explicitly rejected at lowering,
not silently discarded. Arbitrary metadata belongs in metadata.extensions;
Org-only material need not be represented. Dependencies array must currently
be empty: global pattern capture/install not implemented.

Compiled archives are marked origin=json_compilation and capture_passes0,
never claimed to be device captures. Metadata retained in compiled manifest;
keep JSON source as authoritative authoring document. No upload is performed.
Duplicate JSON object keys are rejected at every nesting level, including
escaped spellings of the same key. Metadata/extensions survive compilation
and re-export. This schema is a prototype, not
a frozen public interchange contract or JSON Schema specification.

### What raw_hex contains

provenance.raw_hex is the ORIGINAL entire20480-byte song image encoded as
40960hex characters, two per byte. It is not MIDI/SysEx, not another track,
and not an editable musical field. The compiler verifies raw_sha256, uses
these bytes as its base, then overlays explicit JSON settings/events. Thus
JSON musical edits take precedence; raw_hex/raw_sha256 remain the original
provenance and should not be manually edited after each change.

This retains unused NULL payload, padding and reserved bytes for exact binary
round-trips. It duplicates decoded content intentionally and adds clutter. A
future hash-checked sidecar backup reference could keep authoring files smaller;
embedded provenance currently keeps each document self-contained. Do not remove
it in schema1. Raw SHA is the base hash, not compiled-result hash.

## Offline terminal view

```sh
tools/carbon-backup/target/release/carbon-backup view build/song19-C4-test.json --track 1 --steps 1
tools/carbon-backup/target/release/carbon-backup view build/song19.backup --track 6 --steps 1-8
```

Accepts schema1 JSON or verified single-song archive (1.34 only). No MIDI
initialization/device access. Omit --track for all6, --steps for all64.
Selectors are one-based, strict, reject unknown/repeated options. Renderer
and Options are separate from file loading/CLI for future formats/filters.
All6ordered slots shown including Empty, note MIDI60=C4, velocity and tick
length. CC/PC/CCi/gates/Hrm/Inv render explicitly; raw enum choices labelled.
Shared step delay/ratchet shown; scene motion/pattern/routing not applied to
this stored-event view. It is NOT a playback simulation or new source format.

### Terminal grid

```sh
tools/carbon-backup/target/release/carbon-backup view build/song19-C4-test.json \
  --track 1 --scene 1 --layout grid
```

8x8 row-major stored-event occupancy with scene motion window shading.
`--color auto|always|never` (default auto, respects NO_COLOR and TTY),
`--ascii` for portable symbols, `--steps N|A-B` keeps grid geometry while
blanking excluded cells. Default detail view unchanged. All active event
types count as material. Stored scene settings printed; no pattern/Euclidean
mask, gate/mute or transient runtime simulation. Global editable preset masks
are absent; no default-ROM assumption is silently applied.
