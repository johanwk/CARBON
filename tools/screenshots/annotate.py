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
import base64, html, os, sys
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
    if len(sys.argv) != 4 or sys.argv[1] not in DIAGRAMS:
        sys.exit("usage: annotate.py {%s} SCREEN.png OUT.svg" % ",".join(DIAGRAMS))
    main(*sys.argv[1:])
