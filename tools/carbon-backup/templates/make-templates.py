#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Generate song templates from default-1.38.json:
#   pop-sketch-1.38.json  Pop sketch, 110 BPM, C major, six scenes with content.
#   blank-tpl.json        Same track setup; only a four-on-the-floor kick.
#   (pop-sketch-1.38.json is a local sketch and is not published.)
#
# Tracks (both): 1 Drums (DRUM), 2 Bias A, 3 Bass (bias T2), 4 Bias B,
#                5 Chords (bias T4), 6 Lead (bias T4).
# Outputs (both): A = USB device, B = DIN1, same channel; bias tracks silent.
#   Drums ch 10, Bass ch 2, Chords ch 3, Lead ch 4. Reassign A or B to CV/gate
#   on the panel as needed.
# Bias notes are relative to C4 = 60 (60 = no shift).
import copy, json, os

here = os.path.dirname(os.path.abspath(__file__))
BASE = json.load(open(os.path.join(here, "default-1.38.json")))
USB_DEV, DIN1, OFF = 4, 0, -1
CHANNEL = {1: 10, 3: 2, 5: 3, 6: 4}          # one-based MIDI channels
BIAS = {3: 2, 5: 4, 6: 4}                     # follower -> source track (one-based)
STEP = {16: 4, 8: 7, 1: 16}                   # 1/16, 1/8, 1/1 step length indexes
STEP_LEN = {1: 16, 2: 1, 3: 8, 4: 1, 5: 8, 6: 8}
Q = 96                                        # ticks per quarter note

def setup(title, description, tempo):
    d = copy.deepcopy(BASE)
    S = d["settings"]
    d["metadata"]["title"] = title
    d["metadata"]["description"] = description
    S["tempo"] = tempo
    for t in range(1, 7):
        ch = CHANNEL.get(t)
        for out, port in ((1, USB_DEV), (2, DIN1)):
            S[f"track{t}.output{out}.port"] = port if ch else OFF
            S[f"track{t}.output{out}.channel_zero_based"] = (ch or 1) - 1
        S[f"track{t}.type"] = 1 if t == 1 else 0
    return d

def scenes(d, plan, win):
    """plan: scene -> track -> window key or None (muted)."""
    S = d["settings"]
    for sc in range(1, 7):
        S[f"scene{sc}.root"] = 0  # C
        for t in range(1, 7):
            p = f"scene{sc}.track{t}."
            key = plan[sc][t]
            start, length = win[t][key or next(iter(win[t]))]
            S[p + "motion_start_zero_based"] = start
            S[p + "motion_length"] = length
            S[p + "mute"] = 0 if key else 1
            S[p + "step_length_index"] = STEP[STEP_LEN[t]]
            S[p + "scale_index"] = 1 if t in (3, 5, 6) else 0  # Major / Chromatic
            S[p + "bias_source_zero_based"] = BIAS[t] - 1 if t in BIAS else -1

def put(d, t, step, kind, **params):
    for ev in d["shared_tracks"][t - 1]["steps"][step - 1]["events"]:
        if ev["kind"] == "Empty":
            ev["kind"] = kind
            if kind == "Note":
                params.setdefault("delay", {"kind": "inherit"})
            ev["parameters"] = params
            return
    raise SystemExit(f"track {t} step {step} full")

def note(d, t, step, pitch, vel=100, ticks=20):
    put(d, t, step, "Note", pitch=pitch, velocity=vel, length_ticks=ticks)

def write(d, name):
    json.dump(d, open(os.path.join(here, name), "w"), indent=1)
    print("wrote", name)

# ---------------------------------------------------------------- pop sketch
def pop():
    d = setup("Pop sketch 1.38",
        "Pop, 110 BPM, C major. T1 drums; T2 bias A -> T3 bass; T4 bias B -> T5 chords "
        "and T6 lead. Scenes: 1 Intro 2 Verse 3 Pre 4 Chorus 5 Bridge 6 Outro. "
        "Outputs A = USB device, B = DIN1 (drums 10, bass 2, chords 3, lead 4).", 110.0)
    win = {
        1: {"v": (0, 16), "c": (16, 16)},
        2: {"v": (0, 4), "p": (4, 4), "c": (8, 4), "b": (12, 4)},
        3: {"v": (0, 32), "c": (32, 32)},
        4: {"v": (0, 4), "p": (4, 4), "c": (8, 4), "b": (12, 4)},
        5: {"v": (0, 32), "c": (32, 32)},
        6: {"v": (0, 32), "c": (32, 32)},
    }
    plan = {
        1: {1: "v", 2: "v", 3: None, 4: "v", 5: "v", 6: None},
        2: {1: "v", 2: "v", 3: "v", 4: "v", 5: "v", 6: "v"},
        3: {1: "v", 2: "p", 3: "v", 4: "p", 5: "v", 6: "v"},
        4: {1: "c", 2: "c", 3: "c", 4: "c", 5: "c", 6: "c"},
        5: {1: None, 2: "b", 3: "v", 4: "b", 5: "v", 6: "v"},
        6: {1: "c", 2: "c", 3: None, 4: "c", 5: "v", 6: "c"},
    }
    scenes(d, plan, win)

    # Drums (1/16): pop kick on 1 and 3, snare 2 and 4, 8th hats.
    # Verse window 1-16, chorus window 17-32 (extra kick, open hats, crash).
    for base, chorus in ((0, False), (16, True)):
        for s in range(1, 17):
            st = base + s
            if s in (1, 9) or (chorus and s in (8, 11)): note(d, 1, st, 36, 110)
            if s in (5, 13): note(d, 1, st, 38, 105)
            if s % 2 == 1:
                hat = 46 if (chorus and s in (3, 7, 11, 15)) else 42
                put(d, 1, st, "Scn", scenes=[2, 3, 4, 5, 6])  # no hats in the Intro
                note(d, 1, st, hat, 95 if s in (1, 5, 9, 13) else 75)
            if chorus and st == 17:
                put(d, 1, st, "Scn", scenes=[4]); note(d, 1, st, 49, 110)  # crash: Chorus only

    # Bias (1/1, one bar per step). Regions: verse 1-4, pre 5-8, chorus 9-12,
    # bridge 13-16. T4 (chords, lead): I-vi-IV-V | IV-V-IV-V | IV-V-vi-I | vi-IV-I-V.
    chords = [60, 57, 65, 67,  65, 67, 65, 67,  65, 67, 57, 60,  57, 65, 60, 67]
    # Inv per bar on T4: closest voice leading for the C4-E4-G4 chord on T5
    # (computed with voicing_transform + Major quantize; lead is one note, so
    # Inv leaves it unchanged).
    inv = [0, 1, 0, 0,  0, 0, 0, 0,  0, 0, 2, 1,  2, 0, 1, 0]
    # T2 (bass) differs on purpose: pre holds IV then V (pedal build-up);
    # bridge plays A A C G, so IV sounds over A (F/A).
    bass = [60, 57, 65, 67,  65, 65, 67, 67,  65, 67, 57, 60,  57, 57, 60, 67]
    for i in range(16):
        note(d, 2, i + 1, bass[i], 100, 4 * Q)
        note(d, 4, i + 1, chords[i], 100, 4 * Q)
        put(d, 4, i + 1, "Inv", rotations=inv[i], compact=False)

    # Bass (1/8), root C3 = 48: verse sparse, chorus driving 8ths.
    for bar in range(4):
        for s in (1, 4, 7): note(d, 3, bar * 8 + s, 48, 105, 40)
        for s in range(1, 9): note(d, 3, 32 + bar * 8 + s, 48, 110 if s % 2 else 90, 36)

    # Chords (1/8), C4-E4-G4: verse held, chorus on quarters.
    for bar in range(4):
        for p in (60, 64, 67): note(d, 5, bar * 8 + 1, p, 85, 4 * Q - 24)
        for s in (1, 3, 5, 7):
            for p in (60, 64, 67): note(d, 5, 32 + bar * 8 + s, p, 95, 40)

    # Lead (1/8): chord-tone motif, follows the T4 bias. Verse 1-32, chorus 33-64.
    verse = [[(1, 67, 90), (4, 64, 40), (5, 64, 90)]] * 3 + [[(1, 62, 90), (3, 64, 180)]]
    chorus = [[(1, 72, 40), (2, 72, 40), (3, 67, 40), (5, 69, 90)]] * 3 + \
             [[(1, 67, 90), (3, 64, 90), (5, 72, 180)]]
    for base, part, vel in ((0, verse, 90), (32, chorus, 100)):
        for bar, notes in enumerate(part):
            for s, p, ticks in notes:
                note(d, 6, base + bar * 8 + s, p, vel, ticks)

    order = [(1, 16), (2, 32), (3, 16), (4, 32), (2, 32), (3, 16), (4, 32), (5, 16), (4, 32), (6, 16)]
    for i, (sc, beats) in enumerate(order):
        d["song_list"][i].update(scene_code=sc - 1, length_beats=beats)
    write(d, "pop-sketch-1.38.json")

# ---------------------------------------------------------------- blank
def blank():
    d = setup("Blank 1.38",
        "Empty song with the pop track setup: T1 drums; T2 bias A -> T3 bass; T4 bias B -> "
        "T5 chords and T6 lead. Only a four-on-the-floor kick. C major, 120 BPM. "
        "Outputs A = USB device, B = DIN1 (drums 10, bass 2, chords 3, lead 4).", 120.0)
    win = {1: {"a": (0, 16)}, 2: {"a": (0, 4)}, 3: {"a": (0, 32)},
           4: {"a": (0, 4)}, 5: {"a": (0, 32)}, 6: {"a": (0, 32)}}
    plan = {sc: {t: "a" for t in range(1, 7)} for sc in range(1, 7)}
    scenes(d, plan, win)
    for s in (1, 5, 9, 13): note(d, 1, s, 36, 110)
    write(d, "blank-tpl.json")

if __name__ == "__main__":
    import sys
    if "--pop" in sys.argv: pop()   # local sketch only
    blank()
