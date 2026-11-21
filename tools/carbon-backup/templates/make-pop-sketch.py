#!/usr/bin/env python3
# Generate pop-sketch-1.38.json from default-1.38.json.
# Tracks: 1 Drums, 2 Bias A (bass), 3 Bass, 4 Bias B (chords+lead), 5 Chords, 6 Lead.
# Scenes: 1 Intro, 2 Verse, 3 Pre-chorus, 4 Chorus, 5 Bridge, 6 Outro.
import json, os
here = os.path.dirname(os.path.abspath(__file__))
d = json.load(open(os.path.join(here, "default-1.38.json")))
S = d["settings"]
d["metadata"]["title"] = "Pop sketch 1.38"
d["metadata"]["description"] = ("T1 drums, T2 bias A -> T3 bass, T4 bias B -> T5 chords, T6 lead. "
    "Scenes: 1 Intro 2 Verse 3 Pre 4 Chorus 5 Bridge 6 Outro. C major, bias notes relative to C4=60.")
S["tempo"] = 110.0

# Outputs: drums/bass/chords to USB device (port 4), lead to DIN1 (port 0). Bias tracks silent.
for t, port, ch in [(1, 4, 9), (2, -1, 1), (3, 4, 1), (4, -1, 3), (5, 4, 2), (6, -1, 0)]:  # lead: set CV/gate on the panel
    S[f"track{t}.output1.port"] = port
    S[f"track{t}.output1.channel_zero_based"] = ch
S["track1.type"] = 1  # drum: no transpose/scale

STEP = {16: 4, 8: 7, 1: 16}  # 1/16, 1/8, 1/1 step length indexes
step_len = {1: 16, 2: 1, 3: 8, 4: 1, 5: 8, 6: 8}
bias = {3: 1, 5: 3, 6: 3}  # zero-based source
# (start_zero_based, length) per track per scene 1..6
V, C = "v", "c"
win = {
    1: {"v": (0, 16), "c": (16, 16)},
    2: {"v": (0, 4), "p": (4, 4), "c": (8, 4), "b": (12, 4)},
    3: {"v": (0, 32), "c": (32, 32)},
    4: {"v": (0, 4), "p": (4, 4), "c": (8, 4), "b": (12, 4)},
    5: {"v": (0, 32), "c": (32, 32)},
    6: {"v": (0, 32), "c": (32, 32)},
}
plan = {  # scene -> per-track window key, or None = muted
    1: {1: "v", 2: "v", 3: None, 4: "v", 5: "v", 6: None},
    2: {1: "v", 2: "v", 3: "v", 4: "v", 5: "v", 6: "v"},
    3: {1: "v", 2: "p", 3: "v", 4: "p", 5: "v", 6: "v"},
    4: {1: "c", 2: "c", 3: "c", 4: "c", 5: "c", 6: "c"},
    5: {1: None, 2: "b", 3: "v", 4: "b", 5: "v", 6: "v"},
    6: {1: "c", 2: "c", 3: None, 4: "c", 5: "v", 6: "c"},
}
for sc in range(1, 7):
    S[f"scene{sc}.root"] = 0  # C
    for t in range(1, 7):
        p = f"scene{sc}.track{t}."
        key = plan[sc][t]
        start, length = win[t][key or ("v" if "v" in win[t] else "c")]
        S[p + "motion_start_zero_based"] = start
        S[p + "motion_length"] = length
        S[p + "mute"] = 0 if key else 1
        S[p + "step_length_index"] = STEP[step_len[t]]
        S[p + "scale_index"] = 1 if t in (3, 5, 6) else 0  # Major
        S[p + "bias_source_zero_based"] = bias.get(t, -1)

tracks = d["shared_tracks"]
def put(t, step, kind, **params):
    for ev in tracks[t - 1]["steps"][step - 1]["events"]:
        if ev["kind"] == "Empty":
            ev["kind"] = kind
            if kind == "Note":
                params.setdefault("delay", {"kind": "inherit"})
            ev["parameters"] = params
            return
    raise SystemExit(f"track {t} step {step} full")
def note(t, step, pitch, vel=100, ticks=20):
    put(t, step, "Note", pitch=pitch, velocity=vel, length_ticks=ticks)

# Drums (1/16). Verse window 1-16, chorus window 17-32.
for base, chorus in [(0, False), (16, True)]:
    for s in range(1, 17):
        st = base + s
        if s in (1, 5, 9, 13) or (chorus and s == 15): note(1, st, 36, 110)
        if s in (5, 13): note(1, st, 38, 105)
        hat = 46 if (chorus and s in (3, 7, 11, 15)) else 42
        if chorus or s % 2 == 1:
            put(1, st, "Scn", scenes=[2, 3, 4, 5, 6])  # hats silent in Intro
            note(1, st, hat, 80 if s % 4 != 1 else 95)
        if chorus and st == 17:
            put(1, st, "Scn", scenes=[4]); note(1, st, 49, 110)  # crash only in Chorus

# Bias tracks (whole notes). I-vi-IV-V / IV-V-IV-V / IV-V-vi-I / vi-IV-I-V
prog = [60, 57, 65, 67,  65, 67, 65, 67,  65, 67, 57, 60,  57, 65, 60, 67]
for t in (2, 4):
    for i, p in enumerate(prog):
        note(t, i + 1, p, 100, 384)

# Bass (1/8), root C3=48. Verse sparse, chorus driving eighths.
for bar in range(4):
    for s in (1, 4, 7): note(3, bar * 8 + s, 48, 105, 40)
    for s in range(1, 9): note(3, 32 + bar * 8 + s, 48, 110 if s % 2 else 90, 36)

# Chords (1/8), C triad C4-E4-G4. Verse sustained, chorus on quarters.
for bar in range(4):
    for p in (60, 64, 67): note(5, bar * 8 + 1, p, 85, 360)
    for s in (1, 3, 5, 7):
        for p in (60, 64, 67): note(5, 32 + bar * 8 + s, p, 95, 40)
# Lead (track 6) left empty: record verse in steps 1-32, chorus in 33-64.

# Song list: scene_code is zero-based scene; -1 = end.
order = [(1, 16), (2, 32), (3, 16), (4, 32), (2, 32), (3, 16), (4, 32), (5, 16), (4, 32), (6, 16)]
for i, (sc, beats) in enumerate(order):
    d["song_list"][i].update(scene_code=sc - 1, length_beats=beats)

json.dump(d, open(os.path.join(here, "pop-sketch-1.38.json"), "w"), indent=1)
print("wrote pop-sketch-1.38.json")
