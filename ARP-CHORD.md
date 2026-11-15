# Arp Speed: Chord - first local prototype

Identity: 1.38-jwk-chd1. Built on the tested undo/Scn development work, not
part of a published RC. Hardware musical acceptance is pending.

## Try it

Enable Arp on a sequenced track. Select its Type, then turn Arp Speed past
1/1 to Chord. A step's gate-eligible positive-velocity Notes supply the input.
Start with one note and compare Up 1, Up 4 and Random 4, then try a few input
notes. Polyphonic MIDI/Bitwig is the clearest reference; mono CV cannot play
a chord. Existing poly CV allocation has its usual hardware voice limits.

This is a pragmatic compact chord generator, not harmonic analysis.

## First-pass musical rule

- Run the selected, existing arp program from its start against the current
  step's eligible input notes, collecting generated notes instead of sending.
- Sample at most 16 ordinary arp advances (including rests); stop once four
  distinct compact pitches are collected. Each advance retains the existing
  100-instruction bound. This is not a promise to enumerate a complete cycle.
- Fold generated pitches by octaves into the lowest input note through one
  octave above it. Retain the first four distinct pitches in generation order.
  Exact duplicates disappear; upper octave doubles collapse to one upper
  octave. Unlike Inv Compact, octave-doubling spacing is NOT preserved here.
- Send the resulting group on the same sequencer tick, using the first
  eligible input note's velocity. MIDI messages are serialized on the wire.
- Use one common duration: actual step interval times track Gate Time, minimum
  one tick. Ignore input note lengths/delays/ratchets and the Arp Gate setting.
  A new chord cuts off any previous chord before retriggering.

C4 through Up 4 generates octave material which compacts to C4+C5, not
C4+C5+C6+C7. C4 through Up 1 remains one note. No thirds/fifths are invented
if the arp does not generate them. Wider input collections compress; random
arps sample their existing random program, so successive results can vary.
Normal output root/scale/transpose processing still follows generation and
may merge pitches or suppress out-of-range notes.

## Scope and limitations

Chord is triggered by sequencer steps only. Live-only held keyboard input
does not free-run or generate chords in this first prototype. Empty or
fully gated steps produce no new chord; an existing chord can finish its
gate tail. Local Gli is ignored as on other arp input; persistent CCi34/35
still reaches generated attacks, with the existing shared-output limitations.
Chord mode is not added to grid prediction or Inv Bias processing.

Stop/track cleanup, disabling arp, type changes and crossing into/out of Chord
release generated voices. Straight performance timing includes Chord as its
endpoint after 1/1, with next-bar switching; its menu shows Chord >bar pending.
CCi/Remote CC26 intentionally keeps the original 17-rate scaling; it cannot
select Chord. A latched CCi speed retains precedence over the stored choice.

The additional stored Arp Speed index is 17; all old indices stay unchanged.
Track step index17 still means 1/3 bar, not Chord. Numeric song version1.38
marks the new capability; six-byte events and 20480-byte songs stay unchanged.
Back up before saving: older firmware cannot interpret the Chord speed.
Current host JSON/upload tools support through1.37 and reject1.38; raw saved
song/bank backup remains available. No implicit profile edits or downgrade.

Tests exercise all37 built-in types with one through six notes, <=4 outputs,
compact range/deduplication, common gate time and note cleanup, production
step batching, speed/type/disable transitions, UI label distinction, Straight
endpoint and song save/reload. Existing suites pass. Bounded work is not a
measured worst-case interrupt budget: busy six-track hardware timing still
needs checking. This prototype adds no full-song buffer.
