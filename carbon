#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Convenience launcher; never builds, captures or uploads implicitly.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
BIN="$ROOT/tools/carbon-backup/target/release/carbon-backup"
usage() {
 cat <<'EOF'
Usage: ./carbon COMMAND [arguments]

  analyse MIDI                         Inspect tracks, bars and conversion fit
  plan MIDI [selection options]        Show an offline JSON conversion plan
  convert MIDI TEMPLATE_JSON OUTPUT_JSON [selection options]
                                       Create song JSON (USB device, quarter steps)
  validate JSON                        Check syntax and compiled song validity
  compile JSON BACKUP                   Compile JSON to single-song archive
  view FILE [options]                  Inspect notes or grid
  tool COMMAND [arguments]             Full carbon-backup CLI, including transfer

Selection: --start-bar N --bars N --tracks 2,3 --step-length auto|TICKS
Convert defaults: USB-device output (4), quarter-note steps (96).
Explicit options override defaults; use --output-port 0|1|3|4 for another port.
At most64 quarter steps fit: longer selections are truncated and reported.
Conversion replaces all musical tracks/scenes; template must be1.35/schema2.
No automatic upload. Read the embedded conversion report before transfer.
Example:
  ./carbon analyse zelda.mid
  ./carbon convert zelda.mid template.json zelda.json --bars 16
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
 analyse) exec "$BIN" analyse-midi "$@";;
 plan) exec "$BIN" plan-midi "$@";;
 validate) exec "$BIN" validate-json "$@";;
 compile) exec "$BIN" import-json "$@";;
 view) exec "$BIN" view "$@";;
 tool) exec "$BIN" "$@";;
 convert)
  if [ "$#" -lt 3 ]; then echo 'Usage: ./carbon convert MIDI TEMPLATE_JSON OUTPUT_JSON [options]' >&2; exit 1; fi
  MIDI=$1; TEMPLATE=$2; OUTPUT=$3; shift 3
  HAS_PORT=0; HAS_STEP=0
  for ARG in "$@"; do
   case "$ARG" in --output-port) HAS_PORT=1;; --step-length) HAS_STEP=1;; esac
  done
  if [ "$HAS_PORT" -eq 0 ]; then set -- "$@" --output-port 4; fi
  if [ "$HAS_STEP" -eq 0 ]; then set -- "$@" --step-length 96; fi
  exec "$BIN" import-midi "$MIDI" "$TEMPLATE" "$OUTPUT" "$@";;
 *) echo "ERROR: unknown command $CMD; use ./carbon --help" >&2; exit 1;;
esac
