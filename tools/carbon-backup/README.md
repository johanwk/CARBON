# CARBON read-only song-bank backup

Build on macOS with Rust stable and Apple command-line tools:

```sh
cargo test --locked --manifest-path tools/carbon-backup/Cargo.toml
cargo build --release --locked --manifest-path tools/carbon-backup/Cargo.toml
tools/carbon-backup/target/release/carbon-backup ports
```

Cargo.lock pins dependencies. The built executable requires neither Cargo nor
Python to run. macOS uses CoreMIDI through midir. Other platforms are untested.

Stop CARBON transport, save any desired RAM edits yourself, then leave the
panel alone throughout capture. With exact USB port names:

```sh
tools/carbon-backup/target/release/carbon-backup backup-bank \
  build/carbon-bank.backup "CARBON Port 1" "CARBON Port 1" --stopped
tools/carbon-backup/target/release/carbon-backup verify build/carbon-bank.backup
```

`--stopped` is your confirmation, not automatic detection or a transport-stop
command. The executable sends ONLY SysEx0x70 reads; no restore/stage/commit
command exists. Requests are serialized,64 bytes with a configurable extra pause after each
matched reply (default5ms);
3-second response timeout aborts without retry. Both whole-bank captures must
match. Expect several minutes. All64 saved slots are included, even blank or
invalid ones. Firmware, configuration and unsaved RAM are NOT included.

Output is exclusively created, never overwritten. A write/disk failure can
leave an incomplete file; verify rejects it and it is not a valid backup.
File sync and reopen/hash verification occur before reporting success.

## Version1 archive format

One file:8 ASCII bytes `CARBNBK1`, a little-endian uint32 JSON-manifest length,
that many UTF-8 JSON bytes, then exactly1310720 raw song-bank bytes ordered
by panel slots1..64. No trailing bytes. Manifest records schema, timestamp,
ports, two matched capture passes, per-slot address/size/SHA256/magic status
and whole-bank SHA256. Hashes detect corruption, not malicious modification.

Raw payload is suitable for future faithful restoration, including invalid
slots, but restore is NOT implemented or hardware-tested. Future writes need
explicit slot selection/approval, backup, diff and readback verification.

Tests cover bounded read packets, nibble/address validation, archive round-trip,
corruption and truncation. Hardware timeout/disconnect/bank stability and MIDI
backend behavior require real captures; successful port listing alone is not
backup verification. A bank with sustained concurrent writes can fail matching
(or appear stable at capture times); leave device undisturbed.

## Pacing measurements

Optional `--pause-ms N` (integer0..100) follows `--stopped`. Both passes,
strict response matching and abort-without-retry remain unchanged. Timings
are printed per pass and stored in the manifest.1ms/0ms require hardware
verification; default5ms remains the verified conservative setting.

```sh
# New output file; preserve the original backup. Stop transport first.
tools/carbon-backup/target/release/carbon-backup backup-bank \
  build/carbon-bank-1ms.backup "CARBON Port 1" "CARBON Port 1" \
  --stopped --pause-ms 1
```

Compare its verified bank hash with the original (if saved songs are unchanged).
If successful, try a separate `carbon-bank-0ms.backup` with `--pause-ms 0`.
Zero means no artificial sleep, NOT pipelining: every request still waits
for a full matched reply. Any error aborts; do not discard the earlier backup.

## Single-song backup

Panel slot numbers are1..64. Stop transport and leave the panel undisturbed:

```sh
tools/carbon-backup/target/release/carbon-backup backup-song \
  19 build/song19.backup "CARBON Port 1" "CARBON Port 1" \
  --stopped --pause-ms 0
tools/carbon-backup/target/release/carbon-backup verify build/song19.backup
```

This reads only the chosen saved slot twice (320 requests per pass), preserves
all20480 bytes including invalid/blank records, and requires matching captures.
Existing outputs are refused; errors abort without retry. Default extra pause
is still5ms;0ms has been demonstrated with full-bank captures on this setup.

Single-song archive:8 ASCII bytes `CARBNSG1`, little-endian uint32 manifest
length, UTF-8 JSON manifest, then exactly20480 raw bytes. Manifest includes
original panel slot/address, hash/magic status, ports, timestamp, capture count
and pacing/timing. `verify` auto-detects bank versus song archives. This is
not a raw .bin file; future restore must parse/verify the archive first.
No restore command or write packets are available.
