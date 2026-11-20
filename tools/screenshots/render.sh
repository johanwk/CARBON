#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Render prepared-fixture LCD images with production GUI code and ROM fonts.
# Usage: render.sh [OUTDIR [a|b|both]]   (default docs/screenshots/fixtures a)
set -eu
cd "$(dirname "$0")/../.."
OUT=${1:-docs/screenshots/fixtures}
LAYOUT=${2:-a}  # a (3.95" original), b (3.5" newer) or both
CC=${CC:-cc}
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT HUP INT TERM
mkdir -p "$OUT"
"$CC" -x c -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter -Wno-unused-function \
    -Wno-sign-compare -Wno-deprecated-declarations -Wno-missing-field-initializers \
    -fsanitize=address,undefined tools/screenshots/render.c.in src/text/*.c \
    src/seq/scale.c src/util/panel_utils.c src/util/seq_utils.c src/midi/midi_utils.c \
    src/seq/midi_ctrl.c src/seq/arp_progs.c -o "$TMP/render"
"$TMP/render" "$OUT" "$LAYOUT" > "$TMP/list"
# Lossless recompression; the renderer writes uncompressed deflate blocks.
python3 tools/screenshots/pngcrush.py $(cat "$TMP/list")
cat "$TMP/list"
# Annotated diagrams only for the default documentation set.
if [ "$OUT" = docs/screenshots/fixtures ] && [ "$LAYOUT" = a ]; then
    python3 tools/screenshots/annotate.py main "$OUT/01-normal-lcda.png" \
        docs/screenshots/main-display-annotated.svg
    python3 tools/screenshots/annotate.py stepedit "$OUT/08-step-edit-expanded-slot-lcda.png" \
        docs/screenshots/step-edit-annotated.svg
    echo docs/screenshots/main-display-annotated.svg
    echo docs/screenshots/step-edit-annotated.svg
fi
