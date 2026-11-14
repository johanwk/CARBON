# Proposed community-1.36-rc1 prerelease

Unofficial CARBON firmware, not a Kilpatrick Audio release. Feature set frozen.
System identity: `1.36-jwk-rc1`; numeric song version: 1.36. No tag or public
release is created by this preparation. Use the accompanying build record for
the exact commit, checksum, toolchain, sizes and logs, never a filename alone.

## Contents

Cumulative Program Change, Euclidean/bar-relative timing, ordered conditions,
CCi, roots/Hrm, Inv/Compact, individual delays, curved glide, current-state
grid preview, protected performance edits, RAM snapshots and queued readouts.
Includes backup/JSON/MIDI import tooling. See README and the user guide.

Compared with snp3 (25dab50), preparation changes the display identity and
documentation, aligns the simple host workflow/default template to 1.36, adds
host compatibility tests and cross-language layout checks, and improves build
identity logging/host-source exclusion checks. No new musical features.

Simple upload now requires schema3/1.36; its prompt asserts this community
release. Advanced restore uses `--firmware-community-1.36`; legacy assertions
remain explicitly scoped. All target/approval/backup/base/readback protections
are retained. No migration of real songs is inferred from changing a suffix.

## Limitations and installation

Back up first. New songs/events are not faithfully downgradable. RAM snapshots
are not persistent backups. Host replacement is non-atomic and requires panel
LOAD afterward. MIDI portamento is receiver-dependent; grid preview is current
state, not prediction. Runtime interrupt and stack measurements are absent.
The new 1.36 simple workflow and exact rc1 still need representative hardware
acceptance. See RELEASE-CHECKLIST.md for remaining technical checks and
LICENSING.md for attribution.

Use only the bootloader-preserving DFU procedure in README at **0x08020000**.
Publish corresponding source/build instructions with any firmware binary.

## Relationship to upstream submissions

At preparation, PR #10 is open at fc00313, #11 at c7c4a02, #12 at 998fead;
all target upstream master and none is merged. The cumulative release descends
from #12 and adds later work. It must use a separate release branch/tag, not
silently expand those narrower PRs. No remote branches or PRs were modified.
