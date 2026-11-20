# Conditional-gate host tests

From the repository root:

```sh
sh tests/run-host-tests.sh
```

The `.c.in` extension keeps this host-only main out of makegen's recursive
firmware source scan. This test exercises the production inline evaluator,
not a duplicate implementation. It covers probability endpoints, skipped
gates, fresh visits, OR history, history preservation, independent streams
and reproducibility. The engine harness includes the production engine source, MIDI encoders,
sequence utilities and internal-CC conversion helpers. Song/control/hardware
services are fixtures. It exercises preparation, playback, note management,
track resets and the production tick loop with address/undefined-behavior
sanitizers: mixed Note/CC/PC output, skipped bias/CCi, muted/Live sources,
masked visits, note-offs, Stop/Continue and tick-zero reset. Both track-order
directions are tested for CCi making targets newly due or unmasked.

Link-time dead stripping removes unused firmware services; no production
conditional compilation or special test-only engine behavior is introduced.
This is not hardware testing or flash-backed song-storage/preview coverage.

The Chapter 9 prototype uses event code 2 for Probability, data0=0..100.
Shift + Transpose cycles Note -> CC -> CCi -> PC -> Probability -> Note.
Probability defaults to 50%; Transpose adjusts its first value. Preview
ignores the gate and auditions ordinary notes/CC/PC; CCi remains playback-only.

Song-record size and event offsets are unchanged. The local firmware/song version
is 1.30 and uses a display-only `johanwk-ch9-dev` identity; this is not an
official release-number allocation. Do not downgrade a
song containing gates: older firmware cannot interpret them and may suppress
later events. Back up songs before testing this development firmware.

## All four gate types

Event codes 2/3/4/5 are Probability/Euclidean/Pre/Non-Pre. The type cycle is
Note -> CC -> CCi -> PC -> Prb -> Euc -> Pre -> !Pr -> Note. Euclidean
defaults to 1:2, rotation 0. Transpose selects its pair (28 entries through
7:8); velocity edits rotation. Pair changes normalize rotation modulo B.
Pre and !Pr have no numeric fields. All gate lengths are unused.

Euclidean positions start at zero, advance after a boundary step, and
advance once for an effective direction reversal. Same-direction assignments
do nothing. One-step and full 64-step windows count correctly; out-of-window
corrections do not count. Position is stored modulo 840 (LCM of 2..8), so
wrap preserves every supported denominator's phase.

Pre reads prior contributing-visit OR history, committed after the visit.
Non-Pre negates that history but never contributes. Skipped gates do not
contribute; no-gate visits preserve history. Scene position resets preserve
state; transport and track resets restore zero cycle and false history.

Tests cover every pair/rotation's hit count, cycle modulo wrap, one-step and
64-step windows in both directions, actual reversals versus reassignment,
window corrections, reset overriding direction restoration, Pre snapshot
timing, OR history and non-Pre non-contribution. Hardware verification,
preview/editor gesture tests and flash-backed persistence remain open.

## Clock, dependency and ordering coverage

The fifth suite links the production MIDI clock timer to the production
engine through host callbacks matching seq_ctrl's routing. It verifies
Start reset, Stop/Continue preservation, requested Reset, and probability
stream progression during 80% swing. Callback routing is supplied by the
harness; real MIDI port reception and physical timing are not covered.

CCi regression tests include backward eligibility chains, cyclic Bias Track
mappings, and reversal of an already prepared Euclidean track. Preparation
is ordered by track number with bounded rescans of unprepared tracks. Each
prepared visit stays frozen for that tick; later CCi changes affect cached
parameters/cycle state immediately, but do not reevaluate its gates.

All sanitized suites fail on undefined-behavior reports. On Apple clang,
`__weak` is explicitly rebound to the C weak-function attribute for the
clock callbacks; its built-in Objective-C meaning is not suitable here.

## Revised probability initialization

Probability streams are initialized once at engine startup from the STM32
hardware random source, with a bounded timing fallback. Musical resets
clear history and loop position, but preserve probability state. Song loads
also preserve probability state. Fixed initialization in host fixtures makes
tests reproducible; it is not the device's startup seed policy. Practical
hardware testing confirmed different random events after reboot.

Chapter 10 adds root/output, pitch integration, song-layout and root-UI suites.
The integration suite runs production engine, arp and output processing together
with song/hardware fixtures. It covers bipolar CCi17, keyboard/Bias offsets,
scene activation, delayed starts, ratchets, voice stealing, live pressure,
repeated attacks and damper cleanup. Layout assertions compare all persistent
field offsets with the c7c4a02 layout. UI tests exercise shared label buffers
for both LCD types; they do not replace a physical LCD audition.

Harmony changes apply to subsequent note attacks; already sounding voices keep
their pitch until release. Legacy Root Off transpose-only revoicing is retained.

Hrm (local 1.32) appends a ninth editor type: Scale plus Keep/Off/C--B shared
Root. Frozen eligible slots resolve after CCi rescans and before attacks; the
lowest source track wins Root, last explicit slot wins within that source.
Scales apply to source and Bias targets in source/slot order (last wins).
Integration tests cover gated/clashing requests, rescan priority, transient
reset/scene lifecycle, held/live/arp/drum output, editor preview and persistence.
No per-track roots or CCi30. Older firmware cannot interpret Hrm songs.

## Note-delay regression coverage (local 1.35)

The engine/editor/song fixtures cover inherited versus explicit Off, relative
choices, triplet onsets, delayed durations, all-onset inversion, old padding
migration and stale-slot creation. Clock integration drives full engine passes
through 54/55-tick bar intervals and the 0/32/64 triplet example. Known ticks
are injected through the production callback; this does not measure clock jitter.

Ratchet tests check a delayed whole train, one release per attack, minimum
one-tick subdivisions/gates, pending cancellation and repeated-pitch output
integration. Scene/reset cancels pending attacks but keeps legacy sounding
tails; route stop and song load clear sequenced voices. GUI tests check shared
status-buffer boundaries/dirty suppression, not physical LCD pixels. Hardware
audition, stack high-water, external-clock jitter and timing-preserving live
recording are not claimed verified by these host tests.

## Humanize

`humanize_test.c.in` uses production engine/arp/output fixtures with dedicated
deterministic RNG state. Tests cover signed bounds/rounding, late probability
endpoints and all delay amounts, shared timing, frozen pending values, bypasses,
reversed same-input releases, long/full slots, cancellations and checkpoint
preferences. Clock tests compare control/conditional/step schedules with
Humanize Off/On, including swing and cross-step mixed on-time/late attacks.
Root UI tests exercise all seven controls, multiselect and limits on both LCD
buffer configurations. See HUMANIZE.org for limits and hardware gaps.

Arp Humanize coverage adds sync/freerun shortened gates and omitted hits,
release-wins-ties, shared simultaneous timing, frozen per-note velocity,
full-slot handling and pending cancellation at transport/mode/route boundaries.
All37arp types retain Off-equivalent traces; long-delay cleanup is exercised.
Generated Chord/input lifetimes remain unchanged; gate variation is ignored.

## Clock help

Snapshot-key tests cover the800ms hold, release suppression and shortcut/tap
priority. Panel tests cover scroll-only encoder interception and exit/release
ownership. Root UI tests exercise all21shared menu rows/help bounds on both
LCD layouts, live queued/CCi and Snapshot status, dirty-only rendering and
restoration without clearing grid overlays. Physical LCD/button audition and
realtime workload measurements remain separate; see CLOCK-HELP.org.

## Editable menu overview extension

Root UI fixtures now cover all60Clock/Arp/MIDI/Tonality rows on both layouts,
shared per-item editor dispatch, MIDI scene Program identities, multiselect
Humanize, retained underlying menu state, full-value detail and action guards.
Key tests cover deferred Arp/MIDI/Tonality taps and800ms holds, shifted bypass,
cancel/repeat/release cases; panel tests verify Value edits and matching-button
close without toggling Arp. CLOCK-HELP.org documents the revised controls.

Step Edit group-transpose regression: mixed Note/control steps preserve all
non-Note records byte-for-byte, including Prb66percent, in both directions and
with/without Shift. Control-only groups remain unchanged; an individually
selected Prb still edits its probability. Reproduced failure before the fix.

## Read-only track overview (1.38-jwk-trk1)

Root UI tests exercise the production shared overview renderer for all tracks
and scenes on both LCD rectangles, all4096start/length windows, scale/root
combinations including song inheritance and Off, all rate labels, selected-scene
highlights, stored-versus-live/queued rates, read-only guards, dirty-row caching
and overlay-preserving close. Panel tests exercise the production track-hold
observer and input guards:799/800ms, all buttons, encoder cancellation, repeated
press/release, multi-button chords in both orders, Shift/Scene/Scn exclusions
and inactive encoders/Clock close. Existing menu/editor/playback suites remain.
These fixtures do not establish physical hold feel, LCD legibility or realtime
headroom. No song schema changes or additional Rust tests are required.

## Scene overview (1.38-jwk-scn1)

The same production renderer/hold observer now covers Scene-first numbered
chords: all six destinations,799/800ms, repeats, either release order, early
release, other-button/encoder cancellation, wrong order, Shift-copy and Scn
exclusions. Scene overview tests check all tracks/scenes on both LCDs, root
inheritance/Off, scale/type, selected-track highlights, pinned scene despite
engine/panel changes, no-op edit/scroll, cached refresh and grid restoration.
The immediate scene-request dispatch is unchanged; hardware scene-sync timing
and physical chord feel still need confirmation.

## Screenshot renderer smoke test

The final step builds `tools/screenshots/render.sh` and renders both LCD
layouts from prepared example data with the production graphics/GUI code.
It checks that rendering completes, not pixel content. See
`tools/screenshots/README.org` for generating documentation images.
