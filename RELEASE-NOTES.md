# CARBON community-1.37-rc1 prerelease

Unofficial CARBON firmware, not a Kilpatrick Audio release.
System identity: `1.37-jwk-rc1`; numeric song version: 1.37.
Use the accompanying build record for exact source, checksum and toolchain.

## New since community 1.36 RC1

- Scn gates: later slots on a step execute only in selected active scenes.
  No probability draw or contribution to Pre history.
- Scn sits between PC and Prb in the event-type cycle. New gates pass all scenes.
- Selecting one Scn slot takes over buttons 1--6 and their LEDs for mask editing.
  Three paired-bar glyphs show the mask; the title spells out scene membership.
  Buttons do not select tracks, mute, select/copy scenes; captured releases
  remain consumed when leaving the editor.
- JSON schema4/profile `carbon-1.37-scene-gates`, readable `scenes: [1,3,6]`,
  validation/view/restore support and a blank 1.37 MIDI-import template.
  Simple upload names the required 1.36/1.37 firmware from the source; advanced
  1.37 restore uses `--firmware-community-1.37`. No automatic firmware detection.

See [SCENE-GATES.md](SCENE-GATES.md) and the [host guide](tools/carbon-backup/README.md).
All earlier community features remain included. This RC freezes the feature set.

## Evidence and limitations

The user reports Scn gates work as intended on 1.37-jwk-scn1 and no failures in
current practical use. This RC changes its display identity, not musical
behavior. Host tests cover all64 masks across six scenes, editor/LED/key
ownership, persistence, JSON round-trips and simulated transfer protections.

Practical feedback is not exhaustive validation. Busy-load CPU and stack
high-water are unmeasured. The new 1.37 host transfer has fake-device coverage,
not a hardware upload result. Optional remote LCD clients need glyph support.
MIDI portamento remains synth-dependent; grid preview is not prediction;
RAM snapshots are not persistent backups. See RELEASE-CHECKLIST.md.

Back up before installing or saving migrated songs. Older firmware, including
1.36 RC1, cannot interpret Scn; faithful downgrade is not supported. Host song
replacement is non-atomic across sectors and requires panel LOAD afterward,
not saving stale RAM. All target/approval/recovery/readback safeguards remain.

Use the README's dfu-util procedure at **0x08020000** to preserve the factory
bootloader. Never use stock dfu.sh, 0x08000000 or mass erase for this image.

## Contributing and previous releases

Report issues at https://github.com/johanwk/CARBON/issues with identity,
reproduction steps, clock source, routes and synth setup. Share only files you
have permission to publish. Successful setup reports are welcome too.

The 1.36 release/tag and upstream PR #10/#11/#12 branches are unchanged.
1.37 is a separate cumulative prerelease; it does not depend on upstream
review or merger. Corresponding source and build instructions accompany it.
Existing license and attribution notices are preserved.
