# Vendored mGBA source

This directory contains a source copy of [mGBA](https://github.com/mgba-emu/mgba).
It is tracked directly by the Dewpoint Advance repository and is not a Git
submodule.

- Upstream repository: https://github.com/mgba-emu/mgba.git
- Imported revision: `5157ce208a5965e8a47bf5b48b5aae5198c22a5e`

When updating mGBA, replace this directory with the tracked files from the
desired upstream revision, preserve this file with the new revision, and review
the resulting diff before committing it.

Local changes to preserve during updates:
- `GBAAudio::dmgVolume` and `pcmVolume` hold host percentages (default 100).
  `GBAAudioSample` scales PSG and FIFO A/B separately before bias and clipping.
  These host preferences are deliberately not reset or serialized as GBA state.
