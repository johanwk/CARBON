#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Family map of the twelve Step Edit event types (standalone SVG).

Colours match annotate.py FAMILY. Event list and cycle order are taken from
step_edit.c (Shift+Transpose cycle); see STEP-EVENTS.org.
Usage: families.py OUT.svg
"""
import html, sys

FAMILIES = [
 ("Sound", "#3f9d4a", "What plays?",
  [("N", "Note", "Pitch, velocity, length and delay. The only event you hear directly.")]),
 ("Conditions", "#2f6fc0", "Do the slots to the right run this time?",
  [("Scn", "Scene gate", "Only in the chosen scenes."),
   ("Prb", "Probability", "A random chance, 0-100%."),
   ("Euc", "Euclidean", "A of every B loops, evenly spread."),
   ("Pre", "Previous", "If a condition triggered last time."),
   ("!Pr", "Not previous", "If nothing triggered last time.")]),
 ("Messages out", "#8a4fc0", "What else is sent to the synth?",
  [("CC", "MIDI CC", "A controller value, e.g. filter or mod wheel."),
   ("PC", "Program Change", "Switch the synth's patch.")]),
 ("Shaping", "#c0504d", "How do this or other tracks change?",
  [("CCi", "Internal CC", "Changes a setting of the tracks that follow this one as Bias Track."),
   ("Hrm", "Harmony", "Changes scale and root for this track and its Bias followers."),
   ("Inv", "Inversion", "Re-voices chords on tracks that follow this one as Bias Track."),
   ("Gli", "Glide", "Slide into the notes to its right on this step.")]),
]
CYCLE = ["N", "CC", "CCi", "PC", "Scn", "Prb", "Euc", "Pre", "!Pr", "Hrm", "Inv", "Gli"]

def e(t): return html.escape(t)

def main(out):
    W, colw, gap, x0 = 1260, 285, 20, 30
    o = []
    y0 = 150
    tile_h, tile_gap = 74, 12
    H = y0 + 70 + 5 * (tile_h + tile_gap) + 150
    o.append(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
             'font-family="Helvetica, Arial, sans-serif">')
    o.append(f'<rect width="{W}" height="{H}" fill="#fff"/>')
    o.append(f'<text x="{W/2}" y="48" text-anchor="middle" font-size="32" font-weight="bold" '
             'fill="#222">Step events: four families</text>')
    o.append(f'<text x="{W/2}" y="80" text-anchor="middle" font-size="18" fill="#777">'
             'Each step has six slots. Each slot holds one event, read left to right.</text>')
    o.append(f'<text x="{W/2}" y="112" text-anchor="middle" font-size="17" fill="#444">'
             'Ask what you want the step to do, then pick from that family.</text>')
    for k, (name, col, question, events) in enumerate(FAMILIES):
        x = x0 + k * (colw + gap)
        o.append(f'<rect x="{x}" y="{y0}" width="{colw}" height="62" rx="10" fill="{col}"/>')
        o.append(f'<text x="{x+16}" y="{y0+27}" font-size="22" font-weight="bold" '
                 f'fill="#fff">{e(name)}</text>')
        o.append(f'<text x="{x+16}" y="{y0+50}" font-size="14.5" fill="#fff">{e(question)}</text>'
                 if len(question) < 34 else
                 f'<text x="{x+16}" y="{y0+50}" font-size="13" fill="#fff">{e(question)}</text>')
        for j, (lab, full, desc) in enumerate(events):
            ty = y0 + 74 + j * (tile_h + tile_gap)
            o.append(f'<rect x="{x}" y="{ty}" width="{colw}" height="{tile_h}" rx="8" '
                     f'fill="#fff" stroke="{col}" stroke-width="2.5"/>')
            o.append(f'<rect x="{x+12}" y="{ty+12}" width="52" height="30" rx="5" fill="{col}"/>')
            o.append(f'<text x="{x+38}" y="{ty+33}" text-anchor="middle" font-size="17" '
                     f'font-weight="bold" fill="#fff" font-family="Menlo, monospace">{e(lab)}</text>')
            o.append(f'<text x="{x+76}" y="{ty+33}" font-size="17" font-weight="bold" '
                     f'fill="#222">{e(full)}</text>')
            words, lines, cur = desc.split(), [], ""
            for w in words:
                if cur and len(cur) + len(w) + 1 > 36: lines.append(cur); cur = w
                else: cur = (cur + " " + w).strip()
            lines.append(cur)
            for i, line in enumerate(lines[:2]):
                o.append(f'<text x="{x+12}" y="{ty+56+16*i}" font-size="13.5" '
                         f'fill="#333">{e(line)}</text>')
    # cycle order strip
    cy = H - 110
    o.append(f'<text x="{x0}" y="{cy}" font-size="17" font-weight="bold" fill="#222">'
             'Choosing a type: select one slot (Shift+Motion Start), then turn '
             'Shift+Transpose. Types come in this order:</text>')
    colour = {lab: col for _, col, _, evs in FAMILIES for lab, _, _ in evs}
    bw, bg = 82, 16
    bx0 = (W - (12 * bw + 11 * bg)) / 2
    for i, lab in enumerate(CYCLE):
        bx = bx0 + i * (bw + bg)
        o.append(f'<rect x="{bx}" y="{cy+18}" width="{bw}" height="34" rx="6" fill="{colour[lab]}"/>')
        o.append(f'<text x="{bx+bw/2}" y="{cy+41}" text-anchor="middle" font-size="17" '
                 f'font-weight="bold" fill="#fff" font-family="Menlo, monospace">{e(lab)}</text>')
        if i < 11:
            ax = bx + bw + 3
            o.append(f'<path d="M{ax},{cy+35} l{bg-6},0 m-5,-4 l5,4 l-5,4" fill="none" '
                     'stroke="#888" stroke-width="2"/>')
    o.append(f'<text x="{W/2}" y="{cy+80}" text-anchor="middle" font-size="14" fill="#777">'
             'The list wraps around: after Gli comes N again. Ratchet and Step delay are '
             'not events; they belong to the whole step.</text>')
    o.append('</svg>')
    open(out, "w").write("\n".join(o) + "\n")

if __name__ == "__main__":
    if len(sys.argv) != 2: sys.exit("usage: families.py OUT.svg")
    main(sys.argv[1])
