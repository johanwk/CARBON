# CARBON song tools

Rust-only MIDI analysis/import, JSON song editing, backup and controlled
single-song restore. No Python runtime or helpers required. macOS/CoreMIDI
has hardware evidence; other platforms untested. Dependencies pinned by lockfile.

## Quick start

From repository root:

```sh
cargo build --release --locked --manifest-path tools/carbon-backup/Cargo.toml
./carbon --help
./carbon analyse zelda.mid
./carbon plan zelda.mid --bars 16 --step-length 96
./carbon convert zelda.mid template-1.35.json zelda.json --bars 16
./carbon validate zelda.json
./carbon view zelda.json --layout grid --track 1 --scene 1
./carbon compile zelda.json zelda.backup
```

The executable wrapper resolves the binary relative to its own location; input
paths remain relative to your current directory. It never builds or accesses
hardware implicitly. Convert defaults to **quarter-note steps (96 ticks)** and
**USB-device output (port4)**. Explicit --step-length/--output-port override.
No arguments, -h or --help print brief instructions and exit successfully.
Unknown commands/invalid arguments fail nonzero on stderr. Full CLI:

```sh
./carbon tool --help
# Equivalent underlying executable:
tools/carbon-backup/target/release/carbon-backup --help
```

Offline commands and help run before MIDI initialization. Outputs are exclusively
created; existing files are not overwritten. No automatic upload from conversion.
Standalone JSON Schema deferred; validate-json is the authoritative document check.

## MIDI workflow and limitations

analyse-midi FILE [--json] reports bar count, named/numbered tracks, channels,
pitches, polyphony, tempo/meter maps, note counts and fixed-grid fit estimates.
Track counts are not inferred musical voices. SMF0/1 positive PPQ supported;
format2, SMPTE, nonstandard envelopes/truncated data rejected. Input limit8MiB,
1024tracks,500000events (event limit after parser allocation). midly0.5.3,
Unlicense, strict/std features; no parallel/Rayon dependency. Invalid SMPTE
headers rejected before parser to avoid a known malformed-header debug overflow.

plan-midi FILE [--start-bar N] [--bars N] [--tracks NUMBERS_OR_NAMES]
[--step-length auto|TICKS] produces JSON plan, not song JSON. Selectors are
comma-separated exact unambiguous names or one-based track numbers, including
metadata tracks. Omitted tracks selects note-bearing tracks; omitted bars selects
remainder from start (default1). Invalid/ambiguous selections are errors.

Bar1starts at tick0, defaults4/4/120BPM. Mid-bar meter changes start a new flagged
bar, incomplete final bars count; pickups are not inferred. Metadata ties use
last track/event order. Note releases pair FIFO by source track/channel/pitch;
velocity-zero attacks are releases. Missing releases assume track end/minimum
one source tick with diagnostics. Sustain/controllers/bend/pressure counted but
not applied. Original event counts can include velocity-zero note_on releases.

import-midi MIDI TEMPLATE_JSON OUTPUT_JSON --output-port N [selection options]
compiles a valid1.35/schema2template. No implicit legacy migration. **Replaces
ALL six shared event tracks and neutralizes ALL six scenes**, clears arrangement,
disables programs/output2, sets chromatic/root-off/gate100percent/ratchet1/
As Recorded/density100/no Bias/arp. Unused tracks muted; calibration and other
unrelated template settings retained. Events are shared across scenes, not six
independent scene event banks. This is not a merge operation.

Port codes:0 DIN1,1 DIN2,3 USB-host,4 USB-device; CV unsupported by importer.
Source channels preserved per lane. Programs disabled: arrange receiving synth
patches yourself; a General MIDI file does not configure your modular/synth.
Drum notes need a suitable receiver (commonly channel10). Dense sources may use
spare tracks sharing a channel; shared-output same-pitch overlap checks span lanes.

Best approximation proceeds for valid supported input even if lossy. Auto ranks
dropped notes, excluded span, onset/release error, longer steps. Currently first
six selected sources get lanes, spare lanes split slot overflow; excess notes
are dropped in deterministic source/onset/event order, not intelligent voice
reduction. Six tracks/64steps/six slots are hard limits. No time compression:
material beyond64chosen steps is excluded/reported. Short excerpts often sound
better than full-length coarse grids. At quarter steps,64beats fit (16bars4/4).

Nearest onset searches containing/adjacent steps using absolute0..96ticks and
relative1/8,1/4,1/3,1/2,2/3,3/4,7/8choices; ties earlier/lower code. Durations
round to1..65535ticks and selection/capacity clipping. Minimum-tick notes may
extend a fractional end. Channel conflicts and ambiguous same-pitch overlaps
are dropped, not reallocated. Queued-voice sweep includes step scheduling and
loop tails,16voices/lane. This is not complete firmware/synth simulation.
Planner uses floating-point beat coordinates; rational endpoint auditing remains.

Tempo fixed at selection-start value clamped30..300, swing50/internal clock;
later tempo changes ignored. Report gives original integrated duration,
converted content/loop duration and difference, not per-note wall-time error.
metadata.extensions.midi_import embeds original plan, source hash/selections,
accepted compiled timeline and compiler-stage losses. Inspect **both** loss lists.
Independent binary event decode checks accepted pitch/velocity/onset/duration.

Hardware examples: BWV599317notes/no reported timing loss played successfully
in30; Zelda opening16bars334notes/no losses accepted in34. Division quarter
excerpt106notes accepted in32. Dense Aria/Mario/Starwars experiments did not
establish musical fidelity. Nokia13notes uploaded33; all captures remain local.
Full source files are not necessarily preserved: excerpts, omissions, ignored
patches/expression and tempo changes can strongly alter the musical result.

## JSON validation, editing and views

validate-json FILE: strict syntax/duplicate-key rejection, exact structured
fields/cardinality/ranges, provenance hash and full binary compilation in memory.
Exit0prints schema/profile,20480-byte size and binary hash; invalid/read/usage
errors exit nonzero. No output file or MIDI access. Schema1/1.34still valid for
offline codecs, **not accepted as a hardware restore source**. Validation is not
upload permission or musical correctness/playback simulation.

export-json SONG_BACKUP JSON exports editable complete documents; import-json
JSON SONG_BACKUP compiles a single-song archive with origin=json_compilation,
matching_capture_passes0. Metadata survives re-export. Schema2requires profile
carbon-1.35-note-delay. Note delay overrides shared delay,255=inherit,0=explicit
Off; schema2uses typed delay objects (ticks/relative/inherit), not raw codes.
Step inheritance invalid. Relative delays round nearest tick, ties up.

Raw provenance embeds original20480bytes as40960hex characters+SHA256, used as
an overlay base to retain inactive/reserved payload. Musical JSON fields override
known bytes; do not hand-edit raw_hex/hash. Host metadata goes in extensions;
dependencies must currently be empty. No general legacy migration or authored
base-free defaults. JSON schema remains a prototype, not a frozen contract.

view FILE accepts JSON or verified1.34/1.35song archive. --track1..6,
--steps N|A-B; detail lists all six slots. --layout grid --scene N shows8x8
stored occupancy and motion window, panel-style Euclidean label. --ascii and
--color auto|always|never; NO_COLOR/TTY respected. Pattern/density/gate masks
are not simulated. Global editable pattern banks are absent from backups;
no silent ROM-default assumption. MIDI60=C4,96PPQ.

## Read-only device backup

Stop transport and avoid panel edits/save/load throughout transfers. Flags
assert user actions; no automatic transport/firmware detection exists.

```sh
./carbon tool ports
./carbon tool backup-song 34 before34.backup 'CARBON Port 1' 'CARBON Port 1' --stopped --pause-ms 0
./carbon tool backup-bank bank.backup 'CARBON Port 1' 'CARBON Port 1' --stopped --pause-ms 0
./carbon tool verify before34.backup
./carbon tool export-json before34.backup template-1.35.json
```

Only0x70reads; serialized64-byte requests/3s timeout/no retries, two full matching
captures. Pause0..100ms/default5;0ms demonstrated. Save desired RAM edits yourself
before backup: archives capture saved flash only, not config/global patterns,
firmware or unsaved RAM. Blank/invalid raw slots preserved. Incomplete disk output
is rejected by verify; copy valuable archives outside build directory.

## Upload ONE song (WRITES flash)

Explicit targets1..64supported, matching verified target-backup identity required.
Current source must be1.35; --firmware-9c91ab0 asserts installed hardened note-delay
checkpoint1.35-jwk-dly1. Never use old broken stock staging firmware. No capability
query; old --firmware-0623fee flag rejected. Legacy destination can be backed up
raw, but current uploader does not promise legacy-source recovery.

```sh
./carbon tool plan-restore new-song.backup before34.backup 34 replace
# Review the complete diff and exact token before issuing the write command:
./carbon tool restore-song new-song.backup before34.backup 34 recovery34.backup 'CARBON Port 1' --stopped --firmware-9c91ab0 APPROVAL_TOKEN
```

Only that song slot is written, not all64. Replacement rewrites five complete
4096-byte sectors; **not atomic**. Two fresh reads must match reviewed base; new
recovery archive created/synced/reverified before writes. Each64-byte stage awaits
ACK, offset0first; commit waits up to30s for completion; full-song readback
must match. Errors abort/no retries; timeout can mean a write occurred. Retain
backups, do not blindly retry or edit/save/load during transfer. On success
**LOAD target on panel, do not save stale RAM**. Offline patch plans event-only;
hardware CLI remains whole-song replacement. No automatic deployment wrapper.

## Archive format and tests

CARBNSG1/CARBNBK1:8-byte magic, LEu32JSON manifest length, JSON,20480song bytes
or1310720bank bytes, no trailing data. Hashes per song/bank; exclusive file
creation/sync/reopen verification. These are not raw .bin files; only binary
song payload is uploaded. Hashes detect corruption, not malicious replacement.

```sh
cargo test --locked --manifest-path tools/carbon-backup/Cargo.toml
sh tests/run-host-tests.sh
```

Generated Rust fixtures test parser/plan/compiler/validator and transfer failure
positions; fake device tests are not hardware fault-injection. Optional downloaded
music checks (not redistributed): set CARBON_MIDI_TEST_ROOT and run cargo test
--locked --manifest-path tools/carbon-backup/Cargo.toml local_bach_acceptance
-- --ignored --nocapture. Hardware single-song backup/restore/readback and selected
MIDI playback demonstrated; external clock jitter, arbitrary lossy arrangements
and physical failure recovery are not exhaustively tested. Source modules:
smf.rs, midi_plan.rs, midi_compile.rs, document.rs/json.rs, song.rs,
restore.rs/transfer.rs/midi.rs. Briefing holds local experiment history.
