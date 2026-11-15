# One-level song undo - local 1.37-jwk-und1 candidate

Not part of published 1.37 RC1. No song format change: schema4/1.37 storage
remains unchanged. Firmware hardware acceptance is pending.

## Controls

Hold Clock FIRST, then press Run/Stop to undo. While running, restoration
waits for the next transport-aligned 4/4 bar; stopped restoration is immediate.
Repeat the chord before the bar to cancel. Run/Stop alone still controls
transport; pressing it before Clock keeps its normal action. Either release
order works. Feedback says No undo, Undo next bar, Undo cancelled or Undo
restored. Recording/arming and active panel song I/O refuse undo.

## What creates the undo point?

- Clear current song, selected track regions, a step or one event slot.
- Copy current scene onto another scene (the existing overwrite operation;
  there is no separate clipboard paste command in this firmware).
- Magic on selected regions, once before its first changed step.
- Step Edit event-type changes, note/value/duration/delay/ratchet edits and
  Scn mask toggles. Each dispatched encoder change is one operation, not an
  entire knob-turn gesture. A no-op/bounded edit can still replace the point.
- Step Edit keyboard chord replacement: one capture before the first note
  of a chord; incoming CC edits capture separately when no chord is held.
- Song Edit arrangement insertion/removal and parameter edits.
- Step or realtime recording: once before the session's first song write,
  retaining that point across repeated loop/overdub writes and final flush.
  Arming alone or an empty realtime take does not replace it.

Ordinary performance parameter gestures, incoming Remote parameter edits,
transient CCi/Hrm/Inv playback and global preset-pattern editing do not create
undo points. This is not an undo stack for every operation or a DAW-style
transaction history. No support for undoing host flash uploads is implied.

## Scope and lifetime

Undo stores the whole RAM song plus current musical controls, independently
of the manual snapshot. A newer undoable operation replaces the old point.
Repeated undo restores the same point, not redo. Song load (even the same
slot) invalidates it; power-off loses it. User Clear is itself undoable.
Saving retains the undo point but cancels a queued restore. Scene changes
retain the point. Stop/Reset cancel a queued undo without erasing it. Starting
recording cancels queued undo. Requesting snapshot recall supersedes queued
undo and vice versa; the two stored buffers remain separate.

Restoration uses old-route sequence/live/arp/note/pedal cleanup, cancels
pending attacks and performance edits, restores controls, exits editing and
restarts tracks. Conditional history/cycle restart; probability streams are
not rewound. Song mode restarts its captured entry rather than its beat.
External patches, arbitrary synth CC state and global preset patterns are not
captured. Transport position is not reset; clock-source changes follow normal
clock policy, not a seamless-switching promise.

Undo restores RAM only: no flash save or Program Change resend is performed.
Any edits made after capture, including ordinary parameter edits that do not
create a point, are also rolled back. To keep the result, save explicitly.
The manual snapshot retains its established lifetime (including invalidation
when clearing/loading a song); undo never stores into the manual buffer.

## Verification

Host tests exercise snapshot/undo independence, repeat/cancel, record/I/O
guards, pending-note and output cleanup, PRNG continuity, production clock
next-bar application, multi-track destructive-operation grouping, recording
first-write/recycle/final-flush grouping, song load/save/clear lifetime and
Clock/Run edge/release order. These are not hardware timing measurements.

Use backed-up/disposable songs for the first hardware check: Clear then undo,
Magic then undo, scene copy, Step Edit and recording overdub, repeated undo,
manual snapshot independence, both release orders, and busy-pattern recall.
No firmware flashing, song writing or publication is performed by this document.
