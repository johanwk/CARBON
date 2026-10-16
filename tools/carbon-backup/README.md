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
bytes, NOT effective migrated values. Legacy SOURCE recovery is not supported by the current hardware CLI
(source must be1.35), though legacy slots can be backed up exactly. Offline
preview/JSON codecs still accept both1.34 and1.35.

## Controlled hardware restore (initial scope: slot30 only)

```text
carbon-backup restore-song SOURCE TARGET_BACKUP 30 RECOVERY_FILE \
  "CARBON Port 1" --stopped --firmware-9c91ab0 APPROVAL_TOKEN
```

**This command WRITES flash.** Explicitly approve the preview before invocation.
Initial CLI limited to replacement into disposable slot30 and exact Port1.
Firmware flag asserts installed1.35 checkpoint9c91ab0; no capability query exists. Never
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

## 1.35 note-delay songs and current upload target

Offline export/import/view and planning also accept 1.35 images, using
`schema_version: 2` and profile `carbon-1.35-note-delay`. Schema1/1.34
remains supported without interpreting its event padding as delay. The
profile must match the raw provenance version; import does not migrate it.

Schema2 steps use `delay` instead of `delay_ticks`; Note parameters also
require `delay`. Choices are tagged objects: `{"kind":"ticks","ticks":0}`
(explicit Off), `{"kind":"relative","fraction":"1/3"}`, or
`{"kind":"inherit"}` (Notes only). Absolute ticks remain 0..96; relative
fractions are 1/8, 1/4, 1/3, 1/2, 2/3, 3/4 and 7/8 of the actual step.
The Note choice replaces rather than adds to shared delay. Relative delays
round to the nearest tick (ties upward), including on variable intervals.

Hardware restore now requires a valid 1.35 source and the explicit
`--firmware-9c91ab0` assertion: the user confirms the installed 1.35-jwk-dly1
note-delay checkpoint. This is NOT automatic firmware detection. The old
`--firmware-0623fee` assertion and 1.34 upload sources are rejected; no legacy
source migration is performed. Exact archival backup and offline schema1
support remain available, but are not a legacy hardware recovery promise.

Slot30-only replacement, fresh two-read target comparison, new recovery
archive before writes, exact approval token, ACK pacing and full readback
remain mandatory. The target backup may contain any old/blank image: it is
preserved exactly for recovery, not interpreted as a compatible upload source.
Previously verified hardware transfers used 1.34; this updated 1.35 uploader
has offline/fake-device tests only until a separately approved hardware test.
Note-delay playback/save-load and sampled old-song playback were confirmed by
the user on the 1.35 candidate; that is not upload-path hardware verification.

MIDI conversion and timing-preserving live recording remain future work.

## Offline MIDI analysis (Rust-only, first import milestone)

```sh
tools/carbon-backup/target/release/carbon-backup analyse-midi bwv599.mid
tools/carbon-backup/target/release/carbon-backup analyse-midi bwv599.mid --json
```

Runs before MIDI-port initialization; no device access, template, conversion,
or upload. Uses pinned midly 0.5.3 (Unlicense), strict parsing, without its
optional parallel/Rayon dependency. No Python runtime or test helper required.
Supports SMF0/1 with positive PPQ. Format2, SMPTE, RIFF wrappers, nonstandard
headers/chunks and invalid/truncated files are explicitly rejected. Limits:
8 MiB input, 1024 tracks, 500000 decoded events (event limit is checked after
parser allocation; byte limit bounds parser input). Missing EndOfTrack and
events after EndOfTrack are rejected.

Human or JSON report includes numbered track names/channels, pitch ranges,
peak note polyphony, event/program counts, tempo/meter maps, bars, duration,
nonintegral 96-PPQ timing, and fixed-step grid estimates. Track numbers include
metadata-only tracks. Musical voices are not inferred. Source hash identifies
the inspected file; downloaded MIDI files are not embedded in the executable.

Bar1 begins at tick0. Default meter/tempo: 4/4 and120BPM. Mid-bar meter changes
start a new numbered bar, flagged in JSON; final partial bars count as bars.
Pickup bars are not guessed. Simultaneous tempo/meter changes use last in
source track/event order, with diagnostics. Notes pair FIFO per source track/
channel/pitch; velocity-zero Note On releases a note. Unmatched releases are
reported, missing releases assume track end (at least one source tick after
attack). Zero-length notes are flagged. Sustain, bend and controllers are
counted, not rendered into note lengths/pitches. Event counts reflect original
message types, so velocity-zero releases still count under note_on.

Grid estimates use each source track as one destination track, assign notes
to their containing step, and inspect firmware delay choices. They do not yet
allocate spare tracks, thin notes, simulate runtime voices or search adjacent
steps. Suggested full-span grid prioritizes fewer overflowing bins, fewer
inexact onsets, lower total onset error, then longer steps. This is a heuristic,
not a guarantee of musical quality or a complete conversion plan. Durations
are checked for tick integrality, not yet compiler length limits. Long trailing
silence contributes to span. Selection by starting bar/name/number and lossy
JSON song conversion are the next implementation milestones, not commands yet.

Generated Rust fixtures cover running status, FIFO note pairing, metadata,
defaults, missing releases, meter changes, grid density and malformed input.
Local examples checked: bwv599.mid=317notes/10bars4/4/40beats; suggested96tick
steps. bwv-988-aria.mid=418notes/32bars3/4/96beats; suggested144tick steps with
reported timing loss and six overfull track-step bins. No hardware action.

## Offline MIDI selection and approximation plan

```sh
tools/carbon-backup/target/release/carbon-backup plan-midi bwv599.mid --start-bar 1 --bars 10 --tracks 2,3,4,5 --step-length auto
tools/carbon-backup/target/release/carbon-backup plan-midi bwv-988-aria.mid --start-bar 9 --bars 4 --tracks 'upper:,lower:1' --step-length 96
```

Produces JSON on stdout, not a song document/archive. No template or device
access. Options are pairs, reject duplicates/unknown values. Tracks use source
numbers (metadata tracks included) or exact unambiguous names; comma separates
selectors. Omitted tracks selects note-bearing tracks; omitted bars selects the
remainder from starting bar, default1. Out-of-file ranges and ambiguous names
are user errors, not musical losses. Commas/numeric-only track names cannot
be selected by name; use numbers. Step length is auto or a supported fixed
CARBON tick count (e.g.96=quarter); variable bar divisions not yet considered.

Plan preserves beat coordinates, clips notes held across selection boundaries,
and reports every adjusted/dropped note by source track/channel/event. It does
not time-compress: selected material after the chosen64-step capacity is dropped
and its excluded span reported. Nearest onset searches adjacent steps; ties
favour earlier attack then lower delay code. Duration rounds to ticks with a
one-tick minimum,65535maximum and selected/capacity-end clipping. Fractional
final boundaries can consequently incur a reported minimum-tick extension.

One lane is reserved per selected source (first six). Spare tracks split full
six-slot bins for that same source; excess notes/sources are dropped. Current
thinning is deterministic selected-track/onset/event order, NOT inferred melody
or bass priority. Auto ranks dropped-note count, truncated span, total absolute
onset/release error, then longer steps. Source-channel routing is not assigned
yet; lanes from one source may include multiple channels requiring later splits.
Tempo maps, controller/sustain/bend/pressure/program expression and runtime
voice capacity/FIFO are not rendered/simulated. These limits are in the report.

Current real-file plans:599auto96ticks retains317notes with zero timing error;
Aria auto144ticks retains418notes using spare tracks, with reported timing
adjustments. This does not yet establish a playable verified CARBON song.
Next: runtime allocation/route checks, schema2template compilation and independent
binary timeline comparison, then separately approved upload. No Python.

## Compile MIDI into song JSON (offline)

Example (use the full executable path as above):

    carbon-backup import-midi bwv599.mid template-1.35.json bwv599-song.json --output-port 4 --start-bar 1 --bars 10 --tracks 2,3,4,5 --step-length 96
    carbon-backup import-json bwv599-song.json bwv599-song.backup

Requires a valid schema2 template exported from a 1.35 archive; older templates
rejected, not migrated. Exclusive output creation. Explicit output port:0 DIN1,
1 DIN2,3 USB-host,4 USB-device. CV unsupported. Preserve source channel per lane;
conflicting channels dropped/reported. Source tracks sharing a channel merge at
physical output; cross-track same-pitch interactions remain a limitation.

REPLACEMENT SCOPE: clears ALL six shared event tracks and arrangement; neutralizes
ALL six scenes to identical timing/motion/chromatic/root-off/gate100percent/
pattern31/density100 settings. No inherited Bias/Inv/conditions/ratchets/arp.
Unused tracks muted; output2/program changes disabled. Program numbers are not
assumed to match connected synths. Calibration and unrelated template fields
retained. Raw-template provenance and existing host metadata preserved.

Constant selection-start tempo clamped30..300; later changes ignored. Original
tempo map retained in report. Swing50; autolive/remote off; internal clock.
metadata.extensions.midi_import embeds plan, compiler losses, accepted timeline,
source hash/selections/mapping/policies. Not a separate report sidecar.

Compiler conservatively drops same-pitch overlaps/loop tails within a lane,
channel conflicts and excessive active/pending runtime demand. Pending occupancy
starts at parent-step visit. Interval check can overestimate concurrency and
currently does not retry spare-lane allocation for compiler-stage drops.
Independent binary slot/delay decoding verifies accepted pitch/velocity/onset/
duration. This is stored-event consistency, not full firmware/output simulation.

Local tests with synthetic1.35template:599317notes compiled without timing loss;
Aria414of418planned notes compiled, four overlap drops plus planner timing losses.
Optional local test: set CARBON_MIDI_TEST_ROOT to the downloaded MIDI directory,
then cargo test --locked --manifest-path tools/carbon-backup/Cargo.toml local_bach_acceptance -- --ignored --nocapture.
Downloaded music remains untracked, not redistributed as fixtures.

No MIDI ports open during compilation; upload approval remains separate.
Remaining: rational timing coordinates, smarter runtime/channel reallocation,
merged-output FIFO checks, tempo-map error metrics and CLI/hardware acceptance.

## Validate a song JSON without creating an archive

    tools/carbon-backup/target/release/carbon-backup validate-json song.json

Strict syntax (including duplicate-key rejection), document/profile structure,
provenance hash, field limits and complete binary compilation are checked in
memory using the same parser/compiler as import-json. Reports schema/profile,
20480-byte binary size and compiled SHA256. Exit0 means valid; nonzero with
ERROR on stderr means invalid input, read failure or incorrect command usage.
No files created/changed, no MIDI initialization/device access. Accepts existing
schema1 and schema2 offline documents; schema1 validation is NOT permission
to upload to the current1.35-only restore path. Firmware/target/backup/token
checks remain separate; this is not playback simulation or musical validation.
Standalone JSON Schema work deferred: Rust validator is authoritative.

### Runtime/output checks follow-up

Compiler now sweeps queued lifetimes over three loop cycles, releases before
new allocation at equal ticks, and enforces16pending/sounding voices per lane.
Same-pitch overlaps are checked across ALL lanes sharing output channel, not
just within one lane. Retained sources sharing a channel can still share synth
patch/expression state; the importer only renders Notes. Compiler-stage losses
are still dropped/reported, not reallocated. These checks replace the earlier
conservative concurrency/cross-track limitation described above.

Compile policy also reports original selection seconds integrated over its
tempo map, constant-tempo converted content seconds, padded loop seconds and
loop-minus-source duration. This is duration reporting, not tempo-map playback
or per-note wall-time error analysis. Synthetic tests plus local Bach tests
pass; no device/template capture or upload is implied.
