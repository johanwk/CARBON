#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""README banner: four rendered screens with short tags (standalone SVG).

Screens are the renderer's fixture PNGs (exact pixels, example data). The
Hrm/Inv screen carries the same overlay as card 2/2, without callouts.
Usage: banner.py FIXTURE_DIR PITCHES OUT.svg
"""
import base64, html, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import annotate
from pngcrush import recompress

W, H = 1280, 640
LCD_W, LCD_H = 320, 480
S = 0.9                       # screen scale in the banner
PANELS = [
 ("08-step-edit-expanded-slot", None, "sound", "Up to six events per step",
  "Notes, chances, harmony and more"),
 ("13-card-hrminv-pitches", annotate.hrminv_pitch_overlay, "shape",
  "One chord, many harmonies", "Steer other tracks from a Bias track"),
 ("20-drum-lanes-p1", None, "cond", "Drum lanes",
  "Hold a DRUM track; edit in Step Edit"),
 ("04-clock-overview", None, "out", "Every setting on one page",
  "Hold a menu button"),
]

def e(t): return html.escape(t)

def main(fixtures, pitches, out):
    annotate.load_pitches(pitches)
    sw, sh = LCD_W * S, LCD_H * S
    gap = (W - 4 * sw) / 5
    o = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" '
         'font-family="Helvetica, Arial, sans-serif">',
         f'<rect width="{W}" height="{H}" fill="#14181c"/>',
         f'<text x="{W/2}" y="52" text-anchor="middle" font-size="34" font-weight="bold" '
         'fill="#fff">CARBON community firmware</text>',
         f'<text x="{W/2}" y="84" text-anchor="middle" font-size="19" fill="#bbb">'
         'More musical steps, and a display that shows you everything</text>']
    top = 106
    for k, (name, overlay, fam, tag, sub) in enumerate(PANELS):
        x = gap + k * (sw + gap)
        data = base64.b64encode(recompress(
            open(os.path.join(fixtures, f"{name}-lcda.png"), "rb").read())).decode()
        o.append(f'<rect x="{x-6}" y="{top-6}" width="{sw+12}" height="{sh+12}" rx="8" '
                 'fill="#2c3238"/>')
        o.append(f'<image x="{x}" y="{top}" width="{sw}" height="{sh}" preserveAspectRatio="none" '
                 f'style="image-rendering:pixelated" href="data:image/png;base64,{data}"/>')
        if overlay:
            o.append(f'<g transform="translate({x} {top}) scale({S})">{overlay()}</g>')
        col = annotate.FAMILY[fam]
        ty = top + sh + 18
        o.append(f'<rect x="{x-6}" y="{ty}" width="{sw+12}" height="5" rx="2" fill="{col}"/>')
        o.append(f'<text x="{x+sw/2}" y="{ty+30}" text-anchor="middle" font-size="19" '
                 f'font-weight="bold" fill="#fff">{e(tag)}</text>')
        o.append(f'<text x="{x+sw/2}" y="{ty+53}" text-anchor="middle" font-size="15" '
                 f'fill="#bbb">{e(sub)}</text>')
    o.append(f'<text x="{W/2}" y="{H-12}" text-anchor="middle" font-size="12.5" fill="#888">'
             'Screens rendered from example data by CARBON\'s own display code. '
             'Unofficial firmware; not a Kilpatrick Audio release.</text>')
    o.append('</svg>')
    open(out, "w").write("\n".join(o) + "\n")

if __name__ == "__main__":
    if len(sys.argv) != 4: sys.exit("usage: banner.py FIXTURE_DIR PITCHES OUT.svg")
    main(*sys.argv[1:])
