# Scene gates - 1.37-jwk-scn1 development

Local follow-up to community 1.36 RC1; not part of that published release.
Hardware acceptance is pending. No musical features other than Scn are added.

## Use

Select one event slot in Step Edit with Shift+Motion Start. Shift+Transpose
cycles N, CC, CCi, PC, Scn, Prb, Euc, Pre, !Pr, Hrm, Inv, Gli.
New Scn events pass all six scenes.

With one Scn selected and no menu open, press numbered buttons 1--6 directly
to toggle the corresponding scene. No Scene modifier is needed. The buttons
do not select tracks/scenes, mute or copy scenes, even with Shift/Scene held.
Their LEDs show included scenes instead of ordinary track indications.
Leaving Scn, selecting all slots, opening a menu or exiting Step Edit restores
normal controls/indications; a captured press stays consumed until release.
A press begun before entering Scn is not reinterpreted as a mask toggle.

The three-character field shows pairs 1/2, 3/4, 5/6 as narrow filled bars,
with a gap between adjacent enabled scenes. The title spells out the mask,
e.g. `Scn: 1-3--6 Keys toggle`. All bars off blocks every scene; all on passes
every scene. Encoder values do not edit the mask. Buttons refresh the edit
timeout and do not audition notes or reset transport.

## Playback

Scn tests the active scene when the step visit is prepared, never a queued
scene. Failure skips every later slot on that step; earlier events remain
eligible. Normal note cleanup is independent. Combine Scn followed by Prb for
probability only in selected scenes. Scn draws no randomness and does not
contribute to Pre/!Pr history. Gates still obey normal pattern/motion rules;
muted Bias sources can supply eligible controls as before. A prepared visit
is not re-evaluated because the mask is edited later.

Stopped Step Edit audition ignores gates as before. Grid strokes remain a
current-state potential-note preview, not a scene/probability prediction.

## Compatibility

Event code 9; data0 is a six-bit scene mask (0..63), data1 reserved zero.
Six-byte events and 20480-byte song layout are unchanged. Numeric song version
is 1.37, distinguishing these songs from RC1; identity is 1.37-jwk-scn1.
Back up originals before saving. Older firmware cannot interpret Scn; do not
expect faithful downgrade. No migration of existing event codes is needed.

Current host JSON/edit/import/upload tools support through 1.36 and reject
1.37 sources. Do not override profile/version checks. Raw backup-song/bank
can still archive saved bytes; this is not a supported 1.37 restore workflow.
Host Scn support is a separate follow-up.

The local LCD renderer supports four private ASCII control glyph codes for
the mask, without changing existing font characters. An optional remote-LCD
client would need matching glyph support; it is not validated here.

## Verification

Host tests cover all64 masks x6 scenes, history/random continuity, blocked
Bias CCi, ordinary events before/after gates, Note Off, all-mask save/reload,
editor type cycling/toggles, glyph rows, LED overlay/restoration and key release
ownership including Shift/Scene and menu exclusion. Existing suites also pass.
No physical LED/readability/button or playback results are claimed yet.

Suggested hardware check: disposable song with Scn followed by Note or CCi;
toggle six keys, verify LEDs/three-cell mask/title, switch slots and scenes,
try Shift/Scene-held keys, leave editor while a key is held, save/reload, and
confirm no unwanted scene/track/mute/copy action or stuck notes.
