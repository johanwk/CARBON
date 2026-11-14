# Licensing and distribution checklist

Original CARBON and community source is GPL-3.0-or-later unless an individual
file says otherwise. Preserve COPYING and every existing copyright/license
notice. Kilpatrick Audio, STMicroelectronics and Arm names do not imply
endorsement of this community firmware.

## Firmware dependencies

The repository includes ST HAL, CMSIS and ST USB middleware with their own
notices. HAL files include source/binary redistribution and non-endorsement
conditions; CMSIS includes Arm notices and Drivers/CMSIS/Lib/license.txt. USB
middleware headers refer to the MCD-ST Liberty SW License Agreement V2 at
http://www.st.com/software_license_agreement_liberty_v2. These must not simply
be relabeled GPL. Toolchain libraries linked from Newlib/libgcc also have
applicable runtime notices.

**Binary publication gate:** collect the applicable complete vendor/runtime
license texts and required binary notices, verify the ST middleware terms and
the combined distribution obligations, and include them with the binary. The
header inspection here is not legal clearance of all upstream vendor terms.
Do not infer that an upstream public repository removes these obligations.

Distribute the exact corresponding firmware source and build instructions
alongside any approved firmware asset, with its source commit and checksum.
The source package must retain original notices, not only this summary.

## Host tool

carbon-backup and the launcher are GPL-3.0-or-later. Cargo.lock pins dependencies.
Direct dependencies: midir 0.10.4 (MIT), midly 0.5.3 (Unlicense), sha2 0.10.9,
serde 1.0.229 and serde_json 1.0.154 (MIT OR Apache-2.0). The locked transitive
metadata is predominantly MIT/Apache-2.0/Unlicense; unicode-ident additionally
requires Unicode-3.0 terms. Platform dependencies vary. No host executable is
proposed as a public asset yet. Before distributing one, collect actual linked
dependency notices for that target, retain full license texts and provide
corresponding tool source. `cargo metadata --locked --format-version 1` gives
the locked inventory; declarations are not substitutes for the license texts.

## Content and publication scope

Do not ship downloaded music, personal backups, recovery archives, HTML exports,
assistant transcripts or internal briefings. Bundled templates are normalized
blank data with no musical notes; tests use generated fixtures. Private local
files are not deleted during preparation. Inspect both the final tree and any
newly published commits; a clean final tree alone does not audit history.
