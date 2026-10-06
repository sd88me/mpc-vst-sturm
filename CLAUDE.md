# Sturm: agent guide

MPC OS VST2 instrument modelled on the DSI Tempest's voice. Start with `README.md`, `docs/STATUS.md`,
`docs/FIRMWARE.md` and `docs/ANALOG.md`, then mpc-vst-plugins' (checked out next to this repo) `CLAUDE.md`, `docs/NOTES.md` and
`docs/PORTING.md`.

Ground rules:
- Never commit DSI/Sequential firmware files, decoded images (`tp_fw.py unpack` output), extracted tables, factory sounds or
  projects, samples or recordings of the instrument. Curves are formulas and short breakpoint lists fitted to the firmware
  (document each fit in docs/FIRMWARE.md); `tools/fw/tp_fw.py` re-derives them from the user's own files. The sample names
  (`src/sample_names.c`) are the manual's list.
- No "Tempest", "DSI", "Dave Smith" or "Sequential" in the product name, plugin id or skin art; the README may say what it is
  modelled on, with the disclaimer.
- `vst/params.json`, `src/patch_tab.h` and `vst/layout.conf` are generated: edit `tools/gen_patch.py` / `tools/gen_layout.py` and
  run `tools/make_layout.sh`. Field order is the instrument's and append-only once released.
- Analog model changes: say in docs/ANALOG.md what is measured, what is inferred, and from what.
- Every release must be catalog-conformant (`release.py --repo ... --license MIT --id ...`, `catalog_check.py --catalog` OK).
