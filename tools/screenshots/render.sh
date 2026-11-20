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
    # Effect strips come from the production condition evaluator.
    "$CC" -x c -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
        tools/screenshots/conditions.c.in -o "$TMP/conditions"
    "$TMP/conditions" > "$TMP/strips"
    python3 tools/screenshots/annotate.py prb "$OUT/10-card-prb-lcda.png" \
        docs/screenshots/card-prb.svg --strips "$TMP/strips"
    python3 tools/screenshots/annotate.py euc "$OUT/11-card-euc-lcda.png" \
        docs/screenshots/card-euc.svg --strips "$TMP/strips"
    # Follower pitches come from the production voicing and scale code.
    "$CC" -x c -std=c11 -Wall -Wextra -Werror -Wno-unused-function \
        -Wno-deprecated-declarations tools/screenshots/voicing.c.in src/seq/scale.c \
        -o "$TMP/voicing"
    "$TMP/voicing" > "$TMP/pitches"
    python3 tools/screenshots/annotate.py hrminv1 "$OUT/12-card-hrminv-setup-lcda.png" \
        docs/screenshots/card-hrminv-1-setup.svg
    python3 tools/screenshots/annotate.py hrminv2 "$OUT/13-card-hrminv-pitches-lcda.png" \
        docs/screenshots/card-hrminv-2-pitches.svg --pitches "$TMP/pitches"
    python3 tools/screenshots/annotate.py euclid "$OUT/14-card-euclid-lcda.png" \
        docs/screenshots/card-euclid.svg
    echo docs/screenshots/card-euclid.svg
    python3 tools/screenshots/banner.py "$OUT" "$TMP/pitches" docs/screenshots/banner.svg
    echo docs/screenshots/banner.svg
    echo docs/screenshots/card-hrminv-1-setup.svg
    echo docs/screenshots/card-hrminv-2-pitches.svg
    python3 tools/screenshots/families.py docs/screenshots/step-event-families.svg
    echo docs/screenshots/card-prb.svg
    echo docs/screenshots/card-euc.svg
    echo docs/screenshots/step-event-families.svg
    python3 tools/screenshots/annotate.py anatomy "$OUT/09-step-anatomy-lcda.png" \
        docs/screenshots/step-anatomy.svg
    echo docs/screenshots/step-anatomy.svg
    echo docs/screenshots/main-display-annotated.svg
    echo docs/screenshots/step-edit-annotated.svg
fi
