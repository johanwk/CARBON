#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Annotated SVG: a rendered LCD A image with callouts around it.

Anchor coordinates are LCD A pixels from src/gui/gui.c (GUI_DISP_A_*) and
the 8x10 status font (8 px per character, 12 px per expanded row). The
screenshot is embedded as recompressed PNG with pixelated scaling, so the
SVG is self-contained and scales sharply. Callout text is live SVG text.

Leaders are straight lines. On each side, boxes keep the vertical order of
their targets and targets sit at a common edge, so leaders cannot cross.

Usage: annotate.py DIAGRAM SCREEN.png OUT.svg     (DIAGRAM: main, stepedit)
"""
import base64, html, os, struct, subprocess, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngcrush import recompress

SCALE = 1.25          # LCD pixel -> SVG unit
LCD_W, LCD_H = 320, 480
COL = 360             # callout column width per side
GAP = 70              # space between callouts and screen
TOP = 110
HEAD, BODY, LINE = 21, 17, 22   # font sizes / body line height
WRAP = 34                       # characters per body line
HEAD_WRAP = 24                  # characters per bold heading line (first line beside number)

def row(n): return 349 + 12 * n          # expanded Step Edit row y
def ch(c): return 48 + 8 * c             # status character x


# ---------- explanatory overlays (LCD A pixel coordinates) ----------
# The main grid (x 40..278, y 90..328) carries little information in Step
# Edit, so diagrams may draw over it. The real selected cell stays visible.
FAMILY = {"sound": "#3f9d4a", "cond": "#2f6fc0", "out": "#8a4fc0", "shape": "#c0504d"}
GRID = (40, 90, 238, 238)

HATCH_DEFS = ('<defs><pattern id="hatch" width="4" height="4" patternUnits="userSpaceOnUse" '
              'patternTransform="rotate(45)"><rect width="4" height="4" fill="#3a3a3a"/>'
              '<line x1="0" y1="0" x2="0" y2="4" stroke="#777" stroke-width="1.5"/></pattern>'
              '<marker id="arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="5" '
              'markerHeight="5" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="#ddd"/>'
              '</marker></defs>')

def cell_rect(step):
    return (44 + 29 * (step % 8), 94 + 29 * (step // 8), 27, 27)

def esc(t): return html.escape(t)

def tile(x, y, w, h, label, value, family, state):
    """state: 'run', 'skip' or 'empty'."""
    g = []
    if state == "empty":
        g.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="none" '
                 'stroke="#888" stroke-width="0.8" stroke-dasharray="2 2"/>')
        g.append(f'<text x="{x+w/2}" y="{y+h/2+3}" text-anchor="middle" font-size="7" '
                 'fill="#888">empty</text>')
        return "".join(g)
    col = FAMILY[family]
    if state == "skip":
        g.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="url(#hatch)" '
                 'stroke="#999" stroke-width="0.8"/>')
        tc = "#888"
    else:
        g.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="3" fill="{col}"/>')
        tc = "#fff"
    g.append(f'<text x="{x+w/2}" y="{y+12}" text-anchor="middle" font-size="9" '
             f'font-weight="bold" fill="{tc}">{esc(label)}</text>')
    g.append(f'<text x="{x+w/2}" y="{y+24}" text-anchor="middle" font-size="8.5" '
             f'fill="{tc}">{esc(value)}</text>')
    return "".join(g)

def slot_row(y, states, caption):
    slots = [("N", "C4", "sound"), ("Prb", "50%", "cond"), ("N", "E4", "sound"),
             ("N", "G4", "sound"), None, None]
    g = [f'<text x="49" y="{y-4}" font-size="9.5" font-weight="bold" fill="#fff">'
         f'{esc(caption)}</text>']
    w, gap, x0 = 34, 3, 49
    for k, sl in enumerate(slots):
        x = x0 + k * (w + gap)
        if sl is None: g.append(tile(x, y, w, 30, "", "", "sound", "empty"))
        else: g.append(tile(x, y, w, 30, sl[0], sl[1], sl[2], states[k]))
    return "".join(g)

def anatomy_overlay():
    gx, gy, gw, gh = GRID
    cx, cy, cw, ch_ = cell_rect(11)
    g = [HATCH_DEFS,
         # translucent panel with a hole for the real selected cell
         f'<path fill-rule="evenodd" fill="#101418" fill-opacity="0.97" d="M{gx},{gy} h{gw} v{gh} '
         f'h-{gw} z M{cx-2},{cy-2} v{ch_+4} h{cw+4} v-{ch_+4} z"/>',
         f'<rect x="{cx-2}" y="{cy-2}" width="{cw+4}" height="{ch_+4}" fill="none" '
         'stroke="#fff" stroke-width="1"/>',
         '<text x="49" y="104" font-size="10" font-weight="bold" fill="#fff">'
         'Step 12: six slots, read</text>',
         '<text x="49" y="116" font-size="10" font-weight="bold" fill="#fff">'
         'left to right</text>',
         f'<line x1="49" y1="157" x2="268" y2="157" stroke="#ddd" stroke-width="1.2" '
         'marker-end="url(#arr)"/>',
         slot_row(176, ["run"] * 4, "When Prb triggers:"),
         slot_row(230, ["run", "run", "skip", "skip"], "When Prb does not trigger:"),
         '<text x="49" y="276" font-size="8.5" fill="#ddd">Slots to the right of a</text>',
         '<text x="49" y="287" font-size="8.5" fill="#ddd">condition that does not trigger</text>',
         '<text x="49" y="298" font-size="8.5" fill="#ddd">are skipped on this visit only.</text>',
         f'<rect x="188" y="270" width="8" height="8" rx="1.5" fill="{FAMILY["sound"]}"/>',
         '<text x="199" y="277" font-size="8.5" fill="#ddd">Note</text>',
         f'<rect x="188" y="284" width="8" height="8" rx="1.5" fill="{FAMILY["cond"]}"/>',
         '<text x="199" y="291" font-size="8.5" fill="#ddd">Condition</text>',
         '<rect x="188" y="298" width="8" height="8" rx="1.5" fill="url(#hatch)" '
         'stroke="#999" stroke-width="0.6"/>',
         '<text x="199" y="305" font-size="8.5" fill="#ddd">Skipped</text>',
         '<text x="49" y="320" font-size="7.5" font-style="italic" fill="#aaa">'
         'Explanation drawn over the grid; not shown on CARBON.</text>']
    return "\n".join(g)

STRIPS = {}   # key -> "0101...", produced by conditions.c.in (production evaluator)

def load_strips(path):
    for line in open(path):
        k, v = line.split()
        STRIPS[k] = v

def panel(step, heading):
    gx, gy, gw, gh = GRID
    cx, cy, cw, ch_ = cell_rect(step)
    return [HATCH_DEFS,
            f'<path fill-rule="evenodd" fill="#101418" fill-opacity="0.97" d="M{gx},{gy} h{gw} '
            f'v{gh} h-{gw} z M{cx-2},{cy-2} v{ch_+4} h{cw+4} v-{ch_+4} z"/>',
            f'<rect x="{cx-2}" y="{cy-2}" width="{cw+4}" height="{ch_+4}" fill="none" '
            'stroke="#fff" stroke-width="1"/>',
            f'<text x="49" y="104" font-size="10" font-weight="bold" fill="#fff">{esc(heading)}</text>']

def text(x, y, t, size=8.5, fill="#ddd", bold=False, anchor="start"):
    b = ' font-weight="bold"' if bold else ''
    return (f'<text x="{x}" y="{y}" font-size="{size}" fill="{fill}"{b} '
            f'text-anchor="{anchor}">{esc(t)}</text>')

def strip(x, y, bits, label, cell=22, gap=3, note="C5"):
    """One effect strip: a cell per loop; filled = note plays."""
    g = [text(x, y - 5, label, 9, "#fff", True)]
    for k, b in enumerate(bits):
        cx = x + k * (cell + gap)
        if b == "1":
            g.append(f'<rect x="{cx}" y="{y}" width="{cell}" height="{cell}" rx="3" '
                     f'fill="{FAMILY["sound"]}"/>')
            g.append(text(cx + cell/2, y + cell/2 + 3, note, 7.5, "#fff", True, "middle"))
        else:
            g.append(f'<rect x="{cx}" y="{y}" width="{cell}" height="{cell}" rx="3" '
                     'fill="url(#hatch)" stroke="#999" stroke-width="0.6"/>')
        g.append(text(cx + cell/2, y + cell + 9, str(k + 1), 7, "#aaa", False, "middle"))
    return "".join(g)

def prb_overlay():
    # Step 3 is in the top grid row: keep the text below its cell.
    g = panel(2, "")
    g.append(text(49, 140, "Prb 50%: a coin toss each time", 10, "#fff", True))
    g.append(text(49, 152, "the step is played", 10, "#fff", True))
    g.append(text(49, 170, "Step 3 holds  [Prb 50%] [N C5]. Each visit"))
    g.append(text(49, 181, "draws again; the note plays only when"))
    g.append(text(49, 192, "Prb triggers."))
    g.append(strip(49, 210, STRIPS["prb50"], "One example run, 8 loops:"))
    g.append(text(49, 253, "Visit number. Another run gives a"))
    g.append(text(49, 264, "different pattern; on average half."))
    g.append(text(49, 282, "0% never triggers; 100% always does."))
    g.append(text(49, 293, "Each track has its own random stream;"))
    g.append(text(49, 304, "Reset does not restart it."))
    g.append(text(49, 320, "Explanation drawn over the grid; not shown on CARBON.",
                  7.5, "#aaa"))
    return "\n".join(g)

def euc_overlay():
    g = panel(7, "Euc 3:8: three loops out of every")
    g.append(text(49, 116, "eight, spread evenly", 10, "#fff", True))
    g.append(text(49, 140, "Step 8 holds  [Euc 3:8] [N G4]."))
    g.append(text(49, 151, "Euc counts LOOPS of the pattern,"))
    g.append(text(49, 162, "not steps: one cell = one loop."))
    g.append(strip(49, 180, STRIPS["euc3of8"], "Rotation 0:", note="G4"))
    g.append(strip(49, 228, STRIPS["euc3of8r1"], "Rotation 1 (Length/Value dial):",
                   note="G4"))
    g.append(text(49, 274, "The same rhythm repeats every 8 loops."))
    g.append(text(49, 285, "Rotation shifts which loops trigger."))
    g.append(text(49, 296, "Reset starts counting from loop 1."))
    g.append(text(49, 320, "Explanation drawn over the grid; not shown on CARBON.",
                  7.5, "#aaa"))
    return "\n".join(g)

PITCHES = {}  # "hrm1" -> [57, 60, 64], produced by voicing.c.in (production code)
NOTE = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
def nn(p): return f"{NOTE[p % 12]}{p // 12 - 1}"

def load_pitches(path):
    for line in open(path):
        k, v = line.split()
        PITCHES[k] = [int(x) for x in v.split(",")]

def hrminv_setup_overlay():
    g = panel(0, "")
    g.append(text(49, 140, "One track steers the others", 10, "#fff", True))
    g.append(text(49, 154, "Track 1 is the Bias source. Tracks that choose"))
    g.append(text(49, 165, "Bias Track: T1 (Tonality menu) follow it."))
    rows = [("T1", "Bias source (this step)", "yes", "no"),
            ("T2", "Bias Track: T1", "yes", "yes"),
            ("T3", "Bias Track: T1", "yes", "yes"),
            ("T4", "Bias Track: off", "no", "no")]
    y = 186
    g.append(text(178, y, "Hrm", 8.5, "#fff", True, "middle"))
    g.append(text(214, y, "Inv", 8.5, "#fff", True, "middle"))
    g.append(text(252, y, "Notes", 8.5, "#fff", True, "middle"))
    for k, (t, desc, h, i) in enumerate(rows):
        ry = y + 8 + k * 22
        src = k == 0
        g.append(f'<rect x="49" y="{ry}" width="20" height="16" rx="3" '
                 f'fill="{FAMILY["shape"] if src else "#555"}"/>')
        g.append(text(59, ry + 11.5, t, 8.5, "#fff", True, "middle"))
        g.append(text(74, ry + 11.5, desc, 8, "#ddd"))
        for cx, v in ((178, h), (214, i)):
            col = FAMILY["shape"] if v == "yes" else "#666"
            g.append(f'<rect x="{cx-13}" y="{ry+2}" width="26" height="12" rx="2" fill="{col}"/>')
            g.append(text(cx, ry + 11, v, 7.5, "#fff", True, "middle"))
        g.append(text(252, ry + 11.5, "steer" if src else ("+bias" if v == "yes" else "own"),
                      7.5, "#ddd", False, "middle"))
        if 0 < k < 3:
            g.append(f'<path d="M58,{ry-6} v4" stroke="{FAMILY["shape"]}" stroke-width="1.5" '
                     'marker-end="url(#arr)"/>')
    g.append(text(49, 290, "Hrm: scale/root for the source AND followers."))
    g.append(text(49, 301, "Inv: voicing for followers only, not the source."))
    g.append(text(49, 320, "Explanation drawn over the grid; not shown on CARBON.",
                  7.5, "#aaa"))
    return "\n".join(g)

def hrminv_pitch_overlay():
    """Two bands: what is SET on T1 (source), what T2 (follower) PLAYS."""
    g = panel(0, "")
    src = [(nn(PITCHES[f"src{b}"][0]), PITCHES[f"src{b}"][1]) for b in range(4)]
    x0, cw = 104, 43
    shape = FAMILY["shape"]
    # --- Band 1: T1, the Bias source: one step per bar, its slots ---
    g.append(f'<rect x="45" y="131" width="229" height="64" rx="4" fill="none" '
             f'stroke="{shape}" stroke-width="1.2"/>')
    g.append(f'<rect x="45" y="131" width="56" height="64" rx="4" fill="{shape}"/>')
    g.append(text(73, 145, "T1", 12, "#fff", True, "middle"))
    g.append(text(73, 157, "source:", 7.5, "#fff", False, "middle"))
    g.append(text(73, 167, "Inv and", 7.5, "#fff", False, "middle"))
    g.append(text(73, 177, "Hrm are", 7.5, "#fff", False, "middle"))
    g.append(text(73, 187, "set here", 7.5, "#fff", True, "middle"))
    for b, (n, inv) in enumerate(src):
        bx, cx = x0 + b * cw + 2, x0 + b * cw + cw / 2
        g.append(text(cx, 141, f"Bar {b+1}", 7.5, "#aaa", False, "middle"))
        # An Inv equal to the previous bar's is not needed: it stays active.
        held = b > 0 and inv == src[b-1][1]
        slots = [("N", n, FAMILY["sound"], False)]
        if b == 0: slots.append(("Hrm", "Major", shape, False))
        slots.append(("Inv", str(inv), shape, held))
        for k, (lab, val, col, ghost) in enumerate(slots):
            sy = 145 + k * 16
            if ghost:
                g.append(f'<rect x="{bx}" y="{sy}" width="{cw-4}" height="14" rx="2" '
                         'fill="none" stroke="#888" stroke-width="0.8" stroke-dasharray="2 1.5"/>')
                g.append(text(bx + 3, sy + 10, lab, 7.5, "#999"))
                g.append(text(bx + cw - 7, sy + 10, val, 7.5, "#999", False, "end"))
            else:
                g.append(f'<rect x="{bx}" y="{sy}" width="{cw-4}" height="14" rx="2" fill="{col}"/>')
                g.append(text(bx + 3, sy + 10, lab, 7.5, "#fff", True))
                g.append(text(bx + cw - 7, sy + 10, val, 7.5, "#fff", True, "end"))
    # --- Band 2: T2, the follower: what it plays ---
    g.append(text(49, 207, "T2 stores C4 E4 G4 and plays:", 9.5, "#fff", True))
    rows = [("bias", "Bias only", "#666", "from T1 note"),
            ("hrm", "+ Hrm", shape, "from T1 Hrm"),
            ("inv", "+ Inv", shape, "from T1 Inv")]
    prev = None
    for r, (key, label, col, origin) in enumerate(rows):
        ry = 212 + r * 38
        g.append(text(49, ry + 14, label, 8.5, "#fff", True))
        g.append(text(49, ry + 25, origin, 7, "#aaa"))
        for b in range(4):
            notes = PITCHES[f"{key}{b}"]
            before = PITCHES[f"{prev}{b}"] if prev else notes
            bx = x0 + b * cw + 2
            g.append(f'<rect x="{bx}" y="{ry}" width="{cw-4}" height="35" rx="3" '
                     'fill="#22282e" stroke="#555" stroke-width="0.6"/>')
            # highest note on top, like a staff
            for i, p in enumerate(sorted(notes, reverse=True)):
                changed = prev and p not in before
                g.append(text(bx + (cw-4)/2, ry + 10 + 11 * i, nn(p), 8.5,
                              col if changed else "#fff", bool(changed), "middle"))
        prev = key
    g.append(text(49, 327, "Coloured = changed by that row.", 7, "#ddd"))
    return "\n".join(g)

def euclid_hits(length, density, rotation):
    """Mirror of the mask rule in src/seq/pattern.c pattern_get_step_enable().
    The euclid card checks this against the rendered grid pixels."""
    hits = (density * length + 50) // 100
    rot = rotation % length
    return "".join("1" if ((rel + length - rot) % length * hits) % length < hits
                   else "0" for rel in range(length)) if hits else "0" * length

def grid_hits(png, row0=0, rows=2):
    """Read which grid cells are lit (red family) from the rendered PNG."""
    d, i, z = png, 8, b""
    while i < len(d):
        n = struct.unpack(">I", d[i:i+4])[0]
        if d[i+4:i+8] == b"IDAT": z += d[i+8:i+8+n]
        i += 12 + n
    raw, st = zlib.decompress(z), 1 + LCD_W * 3
    out = ""
    for r in range(row0, row0 + rows):
        for c in range(8):
            x, y = 44 + 29 * c + 20, 94 + 29 * r + 20
            R, G, B = raw[y*st+1+x*3: y*st+4+x*3]
            out += "1" if R > 120 and G < 60 and B < 60 else "0"
    return out

EUCLID_PNG = None

def euclid_overlay():
    # Keep grid rows 1-2 (the 16-step window) visible; explain below them.
    gx, gy, gw, gh = GRID
    top = 152
    g = [HATCH_DEFS,
         f'<rect x="{gx}" y="{top}" width="{gw}" height="{gy+gh-top}" fill="#101418" '
         'fill-opacity="0.97"/>',
         f'<rect x="{gx+2}" y="{gy+2}" width="{gw-4}" height="{top-gy-4}" fill="none" '
         'stroke="#fff" stroke-width="1" stroke-dasharray="3 2"/>']
    shown = grid_hits(EUCLID_PNG)
    assert shown == euclid_hits(16, 31, 2), (shown, euclid_hits(16, 31, 2))
    g.append(text(49, 166, "Above: the real grid. 16-step window,", 9, "#fff", True))
    g.append(text(49, 177, "density 31% = 5 hits, rotation 2.", 9, "#fff", True))
    def row(y, bits, label):
        r = [text(49, y - 4, label, 8, "#ddd")]
        for k, b in enumerate(bits):
            x = 49 + k * 14
            if b == "1":
                r.append(f'<rect x="{x}" y="{y}" width="12" height="12" rx="2" '
                         f'fill="{FAMILY["sound"]}"/>')
            else:
                r.append(f'<rect x="{x}" y="{y}" width="12" height="12" rx="2" '
                         'fill="url(#hatch)" stroke="#888" stroke-width="0.5"/>')
        return "".join(r)
    g.append(row(196, "1" * 16, "Density 100%: the track's own pattern (here every step)"))
    g.append(row(226, euclid_hits(16, 50, 0), "Density 50%, rotation 0: 8 hits"))
    g.append(row(256, euclid_hits(16, 31, 0), "Density 31%, rotation 0: 5 hits"))
    g.append(row(286, euclid_hits(16, 31, 2), "Density 31%, rotation 2: same rhythm, 2 later"))
    g.append(text(49, 314, "Hits fall only on steps that hold events; nothing is",
                  7.5, "#ddd"))
    g.append(text(49, 323, "deleted. Explanation panel not shown on CARBON.", 7.5, "#aaa"))
    return "\n".join(g)

DIAGRAMS = {
 "main": dict(
  title="CARBON main display",
  subtitle="Normal operation. Rendered from example data.",
  callouts=[
   # side, target (x,y), highlight (x,y,w,h), heading, body
   ("L", (43, 56), (43, 50, 70, 12), "Song number", "Currently loaded song slot."),
   ("L", (43, 73), (43, 68, 230, 10), "Status indicators",
    "RUN, REC, clock source, keyboard transpose and LIVE. Dim when inactive."),
   ("L", (43, 85), (43, 80, 224, 10), "Song mode line",
    "Song-mode position, or performance status and short notices."),
   ("L", (45, 354), (45, 331, 230, 46), "Mini sequencers",
    "All six tracks at a glance, with selection and arp-enable bars."),
   ("R", (210, 56), (120, 50, 90, 12), "Tempo", "BPM; follows external clock when synced."),
   ("R", (276, 56), (216, 50, 60, 12), "Scene", "Panel scene. Scene + number selects."),
   ("R", (278, 209), (40, 90, 238, 238), "Step grid",
    "64 steps of the first selected track: motion window, pattern hits, playhead and note-pitch marks."),
   ("R", (272, 408), (48, 385, 224, 46), "Status / menu area",
    "Track settings in normal use; menu or Step Edit readout when active."),
  ]),

 "anatomy": dict(
  title="Anatomy of a step",
  subtitle="Step Edit, expanded view. Rendered from example data.",
  overlay=anatomy_overlay,
  callouts=[
   ("R", (ch(29), row(1)+5), (ch(5), row(1), 192, 10), "Six event slots",
    "Each step holds up to six events. Empty slots do nothing."),
   ("L", (ch(9), row(2)+5), (ch(9), row(1), 24, 22), "A condition",
    "Prb 50%: slots to its right run only when it triggers."),
   ("L", (ch(0), row(4)+5), (ch(0), row(2), 32, 46), "Note settings",
    "Note, Vel, Gate and Dly apply to note slots; other types show ---."),
   ("L", (ch(0), row(6)+5), (ch(0), row(6), 200, 10), "Whole-step settings",
    "Ratchet and the shared Step delay apply to the step, not one slot."),
   ("L", (40, 110), None, "About the overlay",
    "Drawn over the grid for this guide. On CARBON you see the step grid here."),
   ("R", (158, 136), cell_rect(11), "Selected step  -  Motion Start",
    "The real grid cell for step 12 stays visible."),
   ("R", (272, 191), None, "Condition triggers",
    "Every slot runs: C4, E4 and G4 play."),
   ("R", (272, 245), None, "Condition does not trigger",
    "C4 still plays; E4 and G4 are skipped this time. The next visit starts afresh."),
  ]),

 "prb": dict(
  title="Event card: Prb (probability)",
  subtitle="Condition. Step Edit, expanded, slot 1 selected. Rendered from example data.",
  overlay=prb_overlay,
  callouts=[
   ("L", (40, 110), None, "What it does",
    "Slots to its right run only when a random draw triggers, with the chosen chance."),
   ("L", (ch(5), row(2)+5), (ch(5), row(1), 24, 22), "Chance  -  Transpose dial",
    "0-100%. New Prb events start at 50%."),
   ("L", (ch(9), row(3)+5), (ch(9), row(2), 24, 22), "The note it controls",
    "Here a single note in slot 2. Put anything you want gated to the right of Prb."),
   ("R", (131, 108), cell_rect(2), "Selected step  -  Motion Start",
    "Step 3. The real grid cell stays visible."),
   ("R", (272, 221), None, "Example run",
    "Computed with CARBON's own condition code. Green = note plays."),
   ("R", (ch(29), row(1)+5), (ch(5), row(1), 32, 10), "Create a Prb",
    "Select a slot (Shift+Motion Start), then Shift+Transpose until it shows Prb."),
   ("R", (ch(29), row(5)+5), (ch(5), row(3), 24, 34), "Unused rows",
    "Vel, Gate and Dly show --- for conditions."),
  ]),
 "euc": dict(
  title="Event card: Euc (Euclidean loops)",
  subtitle="Condition. Step Edit, expanded, slot 1 selected. Rendered from example data.",
  overlay=euc_overlay,
  callouts=[
   ("L", (40, 110), None, "What it does",
    "Slots to its right run on A of every B loops of the track, spread as evenly as possible."),
   ("L", (ch(5), row(2)+5), (ch(5), row(1), 24, 22), "A:B  -  Transpose dial",
    "1:2 up to 7:8 (28 choices). Here 3:8."),
   ("L", (ch(5), row(3)+5), (ch(5), row(3), 24, 10), "Rotation  -  Length/Value",
    "0 to B-1: shifts which loops trigger."),
   ("R", (276, 108), cell_rect(7), "Selected step  -  Motion Start",
    "Step 8. The real grid cell stays visible."),
   ("R", (272, 191), None, "Loops, not steps",
    "One cell per pass through the pattern. Computed with CARBON's own condition code."),
   ("R", (ch(29), row(2)+5), (ch(9), row(1), 24, 22), "The note it controls",
    "G4 in slot 2. Good for fills and variations every few bars."),
  ]),
 "hrminv1": dict(
  title="Event card: Hrm and Inv on a Bias source (1/2)",
  subtitle="Shaping. Step Edit on Track 1, expanded, Hrm slot selected. Rendered from example data.",
  overlay=hrminv_setup_overlay,
  callouts=[
   ("L", (40, 110), None, "What they do",
    "Placed on a Bias source, they change harmony and chord voicing on the tracks that follow it."),
   ("L", (ch(5), row(2)+5), (ch(5), row(1), 24, 22), "Source note",
    "A3: the bias. Followers are transposed by its distance from C4."),
   ("L", (ch(9), row(3)+5), (ch(9), row(2), 24, 22), "Hrm  -  Transpose / Length",
    "Transpose: scale, Keep or 1-16 (here 2 = Major). Length/Value: root Keep, Off or C-B."),
   ("L", (ch(13), row(5)+5), (ch(13), row(2), 24, 22), "Inv  -  Transpose / Length",
    "Transpose: rotations 0-5 (here 2). Length/Value: Compact Off/On."),
   ("R", (54, 104), cell_rect(0), "Selected step  -  Motion Start",
    "Step 1 of Track 1. The real grid cell stays visible."),
   ("R", (ch(29), row(0)+5), (ch(0), row(0), 232, 10), "Full description",
    "The selected event in words: Hrm: Major / Keep."),
   ("R", (272, 236), None, "Who follows whom",
    "Set Bias Track on each follower. Tip: mute the source to use it only for steering."),
   ("R", (ch(29), row(4)+5), (ch(9), row(4), 56, 22), "Unused rows",
    "Gate and Dly show --- for Hrm and Inv."),
  ]),
 "hrminv2": dict(
  title="Event card: Hrm and Inv on a Bias source (2/2)",
  subtitle="Effect on pitches. Screen: Step Edit on Track 2, the follower. Rendered from example data.",
  overlay=hrminv_pitch_overlay,
  callouts=[
   ("L", (40, 110), None, "One chord, many harmonies",
    "T2 stores C-E-G once. T1 changes what is heard, bar by bar."),
   ("L", (45, 163), None, "Set on T1, the source",
    "Each bar, one T1 step holds a note (the bias) and, where it changes, an Inv value. Hrm Major is set once, in bar 1."),
   ("L", (49, 226), None, "Bias only",
    "Transposing the chord shape gives A-C#-E: right shape, wrong key."),
   ("L", (49, 264), None, "+ Hrm",
    "The scale pulls notes into C major: A-C-E, a real A minor chord."),
   ("L", (49, 302), None, "+ Inv",
    "T1's Inv value: 0 keeps root position; 1 and 2 raise the lowest one or two notes an octave."),
   ("R", (54, 104), cell_rect(0), "This screen: T2",
    "The follower. Its stored chord stays C4 E4 G4; nothing is set here."),
   ("R", (272, 300), None, "Computed, not drawn",
    "From CARBON's voicing and scale code. Song root C. Note names: C4 = MIDI note 60 (some DAWs call it C3)."),
   ("R", (266, 167), None, "Stays active until changed",
    "Bar 4 has no Inv of its own (dashed): Inv 1 from bar 3 still applies. The same holds for Hrm."),
   ("R", (ch(29), row(2)+5), (ch(5), row(2), 88, 10), "What you edit",
    "Only the stored chord. Hrm and Inv are not placed on T2."),
  ]),
 "euclid": dict(
  title="Euclidean density and rotation",
  subtitle="Normal mode, after turning Shift+Pattern. Rendered from example data.",
  overlay=euclid_overlay,
  callouts=[
   ("L", (40, 121), (40, 90, 238, 62), "The active hits",
    "Red cells: steps that play. Density spreads them as evenly as possible over the motion window."),
   ("L", (49, 202), None, "Density 100% = original",
    "At 100% the selected pattern plays exactly as before."),
   ("L", (49, 262), None, "Fewer hits, evenly spread",
    "Lower density removes hits; the rest stay evenly spaced."),
   ("L", (49, 292), None, "Rotation moves the rhythm",
    "The same pattern starts later. Rotation runs from 0 to the window length."),
   ("R", (272, 85), (230, 80, 46, 10), "Status line: 16:5:2",
    "Below 100%, the song line shows window length, hits and rotation."),
   ("R", (200, 390), (48, 385, 160, 10), "Which track",
    "Settings are stored per track and scene."),
   ("R", (160, 402), (48, 397, 120, 10), "Density  -  Shift+Pattern",
    "0-100%. Here 31%: 5 of 16 steps."),
   ("R", (152, 414), (48, 409, 112, 10), "Rotation  -  Shift+Transpose",
    "Shifts the hits along the window."),
  ]),
 "stepedit": dict(
  title="Step Edit, expanded view",
  subtitle="Hold Step Edit to switch compact/expanded. Rendered from example data.",
  callouts=[
   ("L", (ch(0), row(2)+5), (ch(0), row(2), 32, 10), "Note  -  Transpose dial",
    "Pitch of each note. With all slots selected, every note moves together."),
   ("L", (ch(0), row(3)+5), (ch(0), row(3), 32, 10), "Vel  -  Length/Value dial",
    "Velocity of each note (1-127)."),
   ("L", (ch(0), row(4)+5), (ch(0), row(4), 32, 10), "Gate  -  Gate dial",
    "Length of each note."),
   ("L", (ch(0), row(5)+5), (ch(0), row(5), 32, 10), "Dly  -  Speed dial",
    "Onset delay per note. Step = use the shared step delay; OFF = no delay."),
   ("L", (ch(0), row(6)+5), (ch(0), row(6), 200, 10), "Ratchet  -  Pattern dial",
    "Repeats for the whole step. Step delay is the shared delay (Speed with all slots)."),
   ("R", (178, 107), (160, 94, 27, 27), "Selected step  -  Motion Start",
    "Turn Motion Start to move between the 64 steps."),
   ("R", (ch(29), row(0)+5), (ch(0), row(0), 232, 10), "What you are editing",
    "Track and step number."),
   ("R", (ch(29), row(1)+5), (ch(5), row(1), 192, 10), "Event slots",
    "Up to six per step. N = note; others are controls such as Prb (probability)."),
   ("R", (ch(16), row(4)+5), (ch(13), row(1), 24, 60), "Selected slot  -  Shift+Motion Start",
    "Edit one slot (highlighted column) or all slots at once."),
  ]),
}

def wrap(text, width=WRAP):
    lines, cur = [], ""
    for w in text.split():
        if cur and len(cur) + len(w) + 1 > width:
            lines.append(cur); cur = w
        else:
            cur = (cur + " " + w).strip()
    return lines + [cur] if cur else lines

def layout(items, top, bottom, gap=16):
    """Place boxes near target y, preserving order and avoiding overlap."""
    ys = []
    for it in items:
        y = max(it["ty"] - it["h"] / 2, top if not ys else ys[-1] + items[len(ys)-1]["h"] + gap)
        ys.append(y)
    over = ys[-1] + items[-1]["h"] - bottom
    if over > 0:                       # shift up from the bottom, keep order
        ys[-1] -= over
        for k in range(len(ys) - 2, -1, -1):
            ys[k] = min(ys[k], ys[k+1] - items[k]["h"] - gap)
    return ys

def main(name, png, out):
    d = DIAGRAMS[name]
    data = base64.b64encode(recompress(open(png, "rb").read())).decode()
    sw, sh = LCD_W * SCALE, LCD_H * SCALE
    lx = COL + GAP
    W, H = 2 * (COL + GAP) + sw, TOP + sh + 60
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W:.0f} {H:.0f}" '
         'font-family="Helvetica, Arial, sans-serif">',
         '<defs><marker id="dot" viewBox="0 0 10 10" refX="5" refY="5" markerWidth="7" '
         'markerHeight="7"><circle cx="5" cy="5" r="4" fill="#d98a1f"/></marker></defs>',
         f'<rect width="{W:.0f}" height="{H:.0f}" fill="#fff"/>',
         f'<text x="{W/2}" y="48" text-anchor="middle" font-size="32" font-weight="bold" '
         f'fill="#222">{html.escape(d["title"])}</text>',
         f'<text x="{W/2}" y="80" text-anchor="middle" font-size="18" fill="#777">'
         f'{html.escape(d["subtitle"])}</text>',
         f'<rect x="{lx-8}" y="{TOP-8}" width="{sw+16}" height="{sh+16}" rx="10" fill="#333"/>',
         f'<image x="{lx}" y="{TOP}" width="{sw}" height="{sh}" preserveAspectRatio="none" '
         f'style="image-rendering:pixelated" href="data:image/png;base64,{data}"/>']
    if d.get("overlay"):
        o.append(f'<g transform="translate({lx} {TOP}) scale({SCALE})">')
        o.append(d["overlay"]())
        o.append('</g>')
    items = []
    for n, (side, (tx, ty), hl, head, body) in enumerate(d["callouts"], 1):
        # Headings "Value  -  Control" break after the dash when too long.
        if len(head) > HEAD_WRAP and "  -  " in head:
            a, b = head.split("  -  ", 1); heads = [a + "  -", b]
        else:
            heads = wrap(head, HEAD_WRAP)
        lines = wrap(body)
        items.append(dict(n=n, side=side, tx=lx + tx * SCALE, ty=TOP + ty * SCALE,
                          hl=hl, heads=heads, lines=lines,
                          h=44 + 26 * (len(heads) - 1) + LINE * len(lines)))
    for it in items:
        if not it["hl"]: continue
        x, y, w, h = it["hl"]
        o.append(f'<rect x="{lx+x*SCALE}" y="{TOP+y*SCALE}" width="{w*SCALE}" '
                 f'height="{h*SCALE}" fill="none" stroke="#d98a1f" stroke-width="2.5" rx="3"/>')
    for side in "LR":
        group = sorted((i for i in items if i["side"] == side), key=lambda i: i["ty"])
        if not group: continue
        for it, by in zip(group, layout(group, TOP - 20, H - 20)):
            bx = 20 if side == "L" else W - COL - 20 + 20
            bw = COL - 20
            sx = bx + bw if side == "L" else bx
            o.append(f'<line x1="{sx}" y1="{by+it["h"]/2}" x2="{it["tx"]}" y2="{it["ty"]}" '
                     'stroke="#d98a1f" stroke-width="2" marker-end="url(#dot)"/>')
            o.append(f'<rect x="{bx}" y="{by}" width="{bw}" height="{it["h"]}" rx="8" '
                     'fill="#fff8ec" stroke="#d98a1f" stroke-width="2"/>')
            o.append(f'<circle cx="{bx+20}" cy="{by+22}" r="13" fill="#d98a1f"/>')
            o.append(f'<text x="{bx+20}" y="{by+28}" text-anchor="middle" font-size="16" '
                     f'font-weight="bold" fill="#fff">{it["n"]}</text>')
            for k, hline in enumerate(it["heads"]):
                o.append(f'<text x="{bx+42}" y="{by+29+26*k}" font-size="{HEAD}" '
                         f'font-weight="bold" fill="#222">{html.escape(hline)}</text>')
            hoff = 26 * (len(it["heads"]) - 1)
            for k, line in enumerate(it["lines"]):
                o.append(f'<text x="{bx+16}" y="{by+54+hoff+LINE*k}" font-size="{BODY}" '
                         f'fill="#333">{html.escape(line)}</text>')
    o.append('</svg>')
    open(out, "w").write("\n".join(o) + "\n")

if __name__ == "__main__":
    if len(sys.argv) == 6 and sys.argv[4] == "--strips":
        load_strips(sys.argv[5]); sys.argv = sys.argv[:4]
    if len(sys.argv) == 6 and sys.argv[4] == "--pitches":
        load_pitches(sys.argv[5]); sys.argv = sys.argv[:4]
    if len(sys.argv) == 4 and sys.argv[1] == "euclid":
        EUCLID_PNG = open(sys.argv[2], "rb").read()
    if len(sys.argv) != 4 or sys.argv[1] not in DIAGRAMS:
        sys.exit("usage: annotate.py {%s} SCREEN.png OUT.svg" % ",".join(DIAGRAMS))
    main(*sys.argv[1:])
