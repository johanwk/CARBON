#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Convenience launcher; never builds, captures or uploads implicitly.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$ROOT/tools/carbon-backup/target/release/carbon-backup"
usage() {
 cat <<'EOF'
Usage: ./carbon COMMAND [arguments]

Everyday song transfers:
  backup song SLOT FILE.json            Save a song from CARBON as JSON
  upload song SLOT FILE.json            Replace a song on CARBON from JSON

  ./carbon backup song 37 song37.json
  ./carbon upload song 37 song37.json

CARBON Port 1 is automatic; no firmware/stopped flags or token copying.
Follow the confirmation prompt and keep CARBON stopped and undisturbed.
Upload saves a recovery backup and verifies readback. Then LOAD the song.
Current upload/template support: firmware 1.35 only, not 1.36.

Other commands:

  analyse MIDI                         Inspect tracks, bars and conversion fit
  plan MIDI [selection options]        Show an offline JSON conversion plan
  convert MIDI OUTPUT_JSON [selection options]
                                       Create song JSON (USB device, quarter steps)
  validate JSON                        Check syntax and compiled song validity
  compile JSON BACKUP                   Compile JSON to single-song archive
  view FILE [options]                  Inspect notes or grid
  tool COMMAND [arguments]             Full carbon-backup CLI, including transfer

Selection: --start-bar N --bars N --tracks 2,3 --step-length auto|TICKS
Convert defaults: USB-device output (4), quarter-note steps (96).
Explicit options override defaults; use --output-port 0|1|3|4 for another port.
At most64 quarter steps fit: longer selections are truncated and reported.
Conversion replaces all musical tracks/scenes; template must be 1.35/schema2.
Bundled default: tools/carbon-backup/templates/default-1.35.json
Supports features through firmware 1.35 only; 1.36 is not yet supported.
Custom template: convert MIDI TEMPLATE_JSON OUTPUT_JSON [options]
No automatic upload. Read the embedded conversion report before transfer.
Example:
  ./carbon analyse zelda.mid
  ./carbon convert zelda.mid zelda.json --bars 16
  ./carbon validate zelda.json

Build once: cargo build --release --locked --manifest-path tools/carbon-backup/Cargo.toml
Full command help: ./carbon tool --help
EOF
}
case "${1:-}" in ''|-h|--help|help) usage; exit 0;; esac
CMD=$1; shift
if [ "${1:-}" = --help ] || [ "${1:-}" = -h ]; then usage; exit 0; fi
if [ ! -x "$BIN" ]; then echo 'ERROR: executable missing; build using the command in ./carbon --help' >&2; exit 1; fi
case "$CMD" in
 backup|download|upload) exec "$BIN" "$CMD" "$@";;
 analyse) exec "$BIN" analyse-midi "$@";;
 plan) exec "$BIN" plan-midi "$@";;
 validate) exec "$BIN" validate-json "$@";;
 compile) exec "$BIN" import-json "$@";;
 view) exec "$BIN" view "$@";;
 tool) exec "$BIN" "$@";;
 convert)
  if [ "$#" -lt 2 ]; then echo 'Usage: ./carbon convert MIDI [TEMPLATE_JSON] OUTPUT_JSON [options]' >&2; exit 1; fi
  MIDI=$1; shift
  TEMPLATE="$ROOT/tools/carbon-backup/templates/default-1.35.json"
  if [ "$#" -ge 2 ]; then
   case "$2" in --*) ;; *) TEMPLATE=$1; shift;; esac
  fi
  OUTPUT=$1; shift
  HAS_PORT=0; HAS_STEP=0
  for ARG in "$@"; do
   case "$ARG" in --output-port) HAS_PORT=1;; --step-length) HAS_STEP=1;; esac
  done
  if [ "$HAS_PORT" -eq 0 ]; then set -- "$@" --output-port 4; fi
  if [ "$HAS_STEP" -eq 0 ]; then set -- "$@" --step-length 96; fi
  exec "$BIN" import-midi "$MIDI" "$TEMPLATE" "$OUTPUT" "$@";;
 *) echo "ERROR: unknown command $CMD; use ./carbon --help" >&2; exit 1;;
esac
