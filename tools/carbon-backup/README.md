# Using the CARBON song tools

Use `./carbon` to inspect MIDI files, convert them into CARBON songs, check the
results, and upload to a single song slot. You can also back up saved songs.

Run these examples from the CARBON repository directory. MIDI files such as
`zelda.mid` are your own downloads, not files supplied with the tools.

**Converting a file does not upload it.** `upload` (or the advanced
`restore-song` command) writes to CARBON.

## Start here: back up or upload one song

```sh
./carbon backup song 37 song37.json
./carbon upload song 37 song37.json
```

**Backup** reads the saved song from CARBON into a JSON file.
**Upload** reads that JSON file and replaces the chosen song on CARBON.
Both commands take the song number first, then the filename.

The original shorthand still works: `./carbon download 37 song37.json` and
`./carbon upload song37.json 37`. These do exactly the same transfers. Both use `CARBON Port 1` automatically. You do not need to compile
an archive, copy a token, select ports or supply firmware/stopped flags.

Each command asks you to stop CARBON and leave it undisturbed. Type
`download 37` or `replace 37` at the prompt; anything else cancels before
connecting. Upload also states the supported firmware: currently the installed
1.37-jwk-scn1 for 1.37 sources, or 1.36-jwk-rc1 (also 1.36-jwk-snp3) for 1.36 sources. The tool cannot detect
firmware or stop playback automatically. The prompt replaces the old switches;
confirm only if its conditions are true. Piped input cannot authorize a transfer.

Upload validates/compiles JSON automatically, reads the target twice, rechecks
it before writing, saves a verified recovery backup, and checks the full
readback. Recovery files are placed automatically in a uniquely named folder
under `carbon-recovery/` in your current directory; the path is printed. Keep
these files. No other song slots are changed. An interrupted write can damage
the target; do not blindly retry.

After uploading, **LOAD the target song from the panel**, rather than saving
stale RAM over it. Download reads saved flash, not unsaved edits or host-only
JSON annotations. It creates a new file and never overwrites an existing one.
On failure an empty/incomplete output or recovery directory may remain.
JSON download requires a decodable 1.34/1.35/1.36/1.37 song; the advanced raw backup
command remains available for blank, damaged or unsupported images.


## 1. Build the tool

You need Rust/Cargo to build it. Python is not required.

```sh
cargo build --release --locked --manifest-path tools/carbon-backup/Cargo.toml
./carbon --help
```

Running `./carbon` without arguments also shows help. For all commands, use
`./carbon tool --help`. The launcher does not build the program automatically.

## 2. The default template is supplied

You do not need to create a template for normal use. The wrapper uses:

```text
tools/carbon-backup/templates/default-1.37.json
```

It is a blank starting song derived from a verified song36 capture. Its example
notes and arrangement were removed. Outputs are disabled until conversion
sets them; calibration offsets are zero. It is not a backup of your device.

The development default is schema4/profile `carbon-1.37-scene-gates`.
It contains no Notes, Scn, Gli or CCi events. For RC1, supply the retained
`tools/carbon-backup/templates/default-1.36.json` explicitly to `convert`;
templates are not implicitly migrated. Confirm the source-matching firmware
before upload, not merely the song's version number.

A MIDI file does not contain all of CARBON's stored settings, so the converter
needs this starting song internally. The default now supplies it automatically:

```sh
./carbon convert zelda.mid zelda.json --bars 16
```

### Optional: use your own template

The older explicit-template form still works:

```sh
./carbon convert zelda.mid my-template.json zelda.json --bars 16
```

Conversion replaces notes/arrangement and neutralizes all six scenes; it does
not merge the MIDI with the template's melody. Other settings are retained.
The following capture procedure is optional, not required for the default.

### Create your template

1. On CARBON running community firmware 1.37-jwk-scn1, load a song you are willing to save again.
   Back it up first if you need to preserve its old saved version.
2. Save that song using the panel.
3. Stop playback. Do not edit, save or load anything during the backup.
4. List the MIDI ports:

```sh
./carbon tool ports
```

The next commands use **song30 as an example**. Replace `30` with the song you
saved. Use the exact input and output port names listed by the tool. On our
Mac, both are `CARBON Port 1`.

```sh
./carbon backup song 30 template-1.37.json
./carbon validate template-1.37.json
```

Validation should report:

```text
Valid CARBON schema4 / firmware profile carbon-1.37-scene-gates
```

**You have now created `template-1.37.json`.** You can use it with the
explicit-template command above. Keep the JSON somewhere safe.
The backup reads saved flash twice and requires matching results; it does not
change CARBON.

If validation reports an older profile, the saved song is still in the older
format. Back up its original first, save it under community 1.37, then repeat the capture/export with **new filenames**.
The tools refuse to overwrite existing output files.

## 3. Inspect your MIDI file

This needs neither a template nor a connected CARBON:

```sh
./carbon analyse zelda.mid
```

The report shows bars, tempo changes, track names and numbers, MIDI channels,
note counts and how much material could fit. It also shows each track's
polyphony: how many notes sound at once.

Start with a short passage and a few parts. A melody and bass are easier to
fit than a large orchestral arrangement.

## 4. Convert a passage

Using the supplied default:

```sh
./carbon convert zelda.mid zelda.json --bars 16
```

This creates `zelda.json` on your computer. It does not write to CARBON.
The defaults are:

- Start at bar 1.
- Use quarter-note steps.
- Send notes through USB-device output, keeping their source MIDI channels.

CARBON has **64 steps per track**. With quarter-note steps, that means 64 beats,
or 16 bars in 4/4. Other time signatures give different bar counts. If you omit
`--bars`, the converter considers the rest of the file, but notes beyond the
64-step limit are omitted and reported. It does not squeeze them into time.

To choose another passage and specific MIDI tracks:

```sh
./carbon convert zelda.mid zelda-bars5-12.json   --start-bar 5 --bars 8 --tracks 1,2
```

Track numbers come from `analyse`. You can also use exact names, such as
`--tracks 'Lead,Bass'`, if each name identifies only one track.

| Option | Meaning |
|---|---|
| `--start-bar 5` | Start at bar 5 |
| `--bars 8` | Consider eight bars |
| `--tracks 1,2` | Choose source MIDI tracks 1 and 2 |
| `--step-length 96` | Quarter-note steps; the default |
| `--step-length 48` | Eighth-note steps; a shorter total span |
| `--step-length 192` | Half-note steps; a longer span, possibly worse timing |
| `--step-length auto` | Let the tool choose a step length |
| `--output-port 4` | USB device; the default |

Other output ports: `0` = DIN1, `1` = DIN2, `3` = USB host.
The MIDI importer does not currently support CV output.

### Understand the compromises

Conversion tries to produce an approximation even when the music does not fit.
It cannot guarantee that every approximation will sound good.

- Long passages can lose their ending.
- Dense passages can lose notes. Each step has six event slots; spare tracks
  may be used to hold extra notes.
- With more than six source parts, the tool prioritizes the first six selected
  parts. It does not intelligently choose the melody and bass.
- Attacks can move to fit available delays. Durations can be rounded or cut
  short. Some overlapping notes of the same pitch are dropped.
- The song keeps one tempo: the tempo at the passage's start, limited to
  30--300 BPM. Later tempo changes are not reproduced.
- Instrument program changes, sustain, pitch bend and other expression are
  not reproduced. Set up your receiving instruments yourself. Drum notes need
  a drum instrument, usually on channel10.

Short passages with quarter-note steps often sound better than whole pieces
with coarse steps. Recorded short-excerpt hardware tests worked well with this approach.

To see a detailed plan without creating a song:

```sh
./carbon plan zelda.mid --bars 16 --step-length 96
```

This prints a technical JSON report. The converted song also includes its
report under `metadata.extensions.midi_import`. Look at `chosen.losses` and
`compiler_losses` for changes and dropped notes. `compiled_timeline` lists the
notes actually included. There is not yet a short, friendly loss-summary command.

## 5. Check and compile the result

```sh
./carbon validate zelda.json
./carbon view zelda.json --track 1
./carbon view zelda.json --layout grid --track 1 --scene 1
./carbon compile zelda.json zelda.backup
```

- **Validate** checks JSON syntax, required data, value ranges and binary
  compilation. It creates no files. Valid does not mean musically faithful.
- **View** shows stored notes or a step grid. It does not simulate playback.
- **Compile** optionally creates a single-song `.backup` archive for advanced
  workflows. The simple `upload` command accepts JSON and compiles it for you.

You can edit the JSON and validate again. Do not edit `raw_hex` or `raw_sha256`:
these preserve the original template data. The Rust validator does the checks;
there is no separate JSON Schema file yet.

## 6. Upload the JSON directly

Keep CARBON stopped and connected, then run:

```sh
./carbon upload song 34 zelda.json
```

This overwrites song34 only after you confirm by typing `replace 34`.
The tool handles compilation, backup and verification. Then LOAD song34 on
CARBON. To save an editable copy later:

```sh
./carbon backup song 34 zelda-downloaded.json
```

### Advanced archive workflow (optional)

The older archive commands are still available for scripts and detailed diff
review. They are not required for everyday JSON transfers.

```sh
./carbon tool plan-restore zelda.backup before34.backup 34 replace
# Add --json for the complete diff.
./carbon tool restore-song zelda.backup before34.backup 34 recovery34.backup \
  'CARBON Port 1' --stopped --firmware-community-1.37 --confirm
```

Here `before34.backup` must be a previously captured backup of slot34; the
recovery filename must be new. The advanced API retains its explicit flags.

## Other useful commands

```sh
./carbon tool ports
./carbon tool verify before34.backup
./carbon tool export-json before34.backup saved-song.json
./carbon view zelda.json --track 1 --steps 1-8 --color never

# Read-only backup of all saved songs; keep CARBON stopped:
./carbon tool backup-bank bank.backup   'CARBON Port 1' 'CARBON Port 1' --stopped --pause-ms 0
```

Backups contain **saved songs only**, not firmware, unsaved edits, system
configuration or the global editable pattern bank. Keep valuable backups outside
the build directory and preferably on another drive. The simple uploader accepts 1.36 and 1.37
song sources with source-specific firmware assertions; an older raw backup is not necessarily uploadable with it.

## Troubleshooting

- **Template not found:** normal conversion uses the supplied default. Check
  that the templates directory is present. For a custom template, check its path.
- **Template is an older version:** back up the original, save under community 1.37 and capture/export it again,
  using new filenames.
- **Output already exists:** choose a new filename. Overwriting output files
  is deliberately prevented.
- **Music sounds wrong:** check the selected parts, reported losses, tempo,
  excerpt and receiving instruments. Longer steps are not always better.
- **Nothing plays after upload:** LOAD the target song and check the receiving
  MIDI port and channels. Instrument programs are not set automatically.
- **Executable missing:** run the build command in step 1.

## Technical notes and tests

The tool is Rust; the launcher is a small shell script. No Python is needed.
Help and offline commands do not open MIDI ports. Input/output paths are relative
to your current directory; the launcher finds its binary relative to itself.
Hardware transfers have been tested on macOS/CoreMIDI; other platforms are untested.

MIDI types 0/1 with positive PPQ timing are supported. Type2, SMPTE timing and
nonstandard file wrappers are not. Limits: 8 MiB, 1,024 tracks, 500,000 decoded
events. The parser dependency is midly 0.5.3. Missing note releases are repaired
with diagnostics. Bar1starts at time0; pickups are not inferred. A mid-bar meter
change starts a new numbered bar. MIDI60is C4; CARBON uses96ticks per quarter.
The planner still uses floating-point timing coordinates.

Recommended development JSON uses schema4/profile `carbon-1.37-scene-gates`.
Schema3/1.36 remains supported including simple upload. Offline tools also
read schemas1/2; simple upload rejects those older sources.
The JSON format is a prototype. Each song archive contains a manifest and a
20,480-byte image; only that binary image is uploaded. Hashes detect changed
data, not malicious tampering.

```sh
cargo test --locked --manifest-path tools/carbon-backup/Cargo.toml
sh tests/run-host-tests.sh
```

Tests cover parsing, conversion, validation, binary note timelines and simulated
transfer failures. They do not prove every musical result or hardware failure
case. For optional local Bach tests, set `CARBON_MIDI_TEST_ROOT` to your MIDI
directory and run the Rust test named `local_bach_acceptance` with `--ignored`.
Use the public release checklist for candidate verification status.

## Supported firmware policy

| Source | JSON profile | Simple upload prompt / advanced assertion |
|---|---|---|
| 1.37 | schema4 / `carbon-1.37-scene-gates` | 1.37-jwk-scn1 / `--firmware-community-1.37` |
| 1.36 | schema3 / `carbon-1.36-glide` | 1.36-jwk-rc1 or snp3 / `--firmware-community-1.36` |
| 1.35 | schema2 / `carbon-1.35-note-delay` | advanced only: 1.35-jwk-dly1 / `--firmware-9c91ab0` |
| 1.34 | schema1 / `carbon-1.34-shared-events` | offline editing only |

Legacy `--firmware-1.36-gli3` remains scoped to 1.36-jwk-gli3. Each advanced
assertion requires matching source format. Simple upload chooses its prompt
from the validated source: this does NOT detect installed firmware. Confirm
that the named compatible firmware is installed. Unsupported or mismatched
profiles fail before MIDI access. No automatic migration/downgrade is performed.

MIDI import defaults to the blank 1.37 template in this development branch;
explicit 1.35/1.36 templates remain supported. Conversion replaces the shared
event bank, including any Scn events in a custom template. It does not invent
scene-specific arrangements. RC1's published branch/tools remain unchanged.

## Scene gates in JSON

Schema4 adds an ordered `Scn` event with a readable list of one-based scenes:

```json
{
  "slot": 2,
  "kind": "Scn",
  "parameters": { "scenes": [1, 3, 6] }
}
```

This passes later event slots on the same step in scenes 1, 3 and 6.
`[]` blocks all scenes; `[1,2,3,4,5,6]` passes all. Order in the scene list
does not matter; export sorts it. Duplicate/non-integer/out-of-range scene
numbers, extra fields and Scn in older profiles are rejected. The six-bit mask
is compiled into event data0, with data1 reserved zero. Unused bytes remain
unchanged, preserving byte-exact unedited round-trips. Do not hand-edit raw
provenance or its hash. View shows included scene numbers; the terminal grid
still shows stored occupancy, not gate evaluation.

Backup/export, validation, compilation, view, restore planning and simple upload
support 1.37. All explicit-target, approval, durable recovery backup, fresh-base
verification, bounded writes and full readback protections remain unchanged.
Host upload does not reload the active song: LOAD afterward, never SAVE stale
RAM. Replacement across five sectors is not atomic. Raw backup can archive
unsupported/blank slots, without promising those sources are uploadable.

The user reports Scn gates work as intended on hardware. The new host 1.37
transfer path has offline/fake-device tests, not a new hardware upload result.
Earlier backup and restore hardware evidence applies to earlier tool versions.
