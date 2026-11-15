# Testing FLEX Disk Wrangler

    tests/run_all.sh <build dir> [<the sample FLEX discs' folder>]

builds everything with whichever `qmake` is first on the path and ends `ALL PASSED` or `SOME TESTS FAILED`. It needs
Python 3. Without the second argument the sample discs aren't checked; with it, every sample is read (copies, in a
temporary folder).

| Test | What it checks |
| --- | --- |
| core tests | DiskWranglerCore's own tests (settings, transfer, UI helpers, containers and saving, the Importer, the command line, the Volume interface, the DFS and FAT modules), built from the pinned `core/` |
| `tests/flex_check.py make` | makes the synthetic discs everything else uses (deterministic): `basic.dsk` (text, binary, random, protected, deleted, dated and undated files), `big.dsk` (45 files over 5 directory sectors, 40 x 36), `empty.dsk` (77 x 15), five damaged discs (`bad-freeloop`, `bad-freecount`, `bad-chain`, `bad-crosslink`, `bad-dirloop`), `short.dsk` (a file shorter than its SIR says), an OS-9 and a UniFLEX disc, and noise |
| `voltest` | detection by tiers on every synthetic disc; the FLEX volume through `dw::fs::Volume` (names, sizes, attributes, dates, contents, the retro-files family); every change refused inside `Disc::transact` with nothing left staged; a faithful `reload()`; Save As byte for byte; each damaged disc's fault named and the disc read-only; the name rules; and every sample disc (15 FLEX discs open, the 2 damaged ones fail their check, the 5 others are named OS-9 or UniFLEX) |
| `guitest` | the window (offscreen): the menus in the family's order, what is greyed on a disc that is only read, the rows and their cells, View (FLEX text expanded), Extract (as stored, lower-case names, dated), Copy, Properties, Disc information, Check, and discs it doesn't open |
| `tests/cli/run_cli.sh` | `flexdw`'s golden cases (`tests/cli/cases/`): info, check, ls, get and the refused changes, on the synthetic discs, outputs and image hashes compared; `--update` rewrites them (only in a commit of its own that says why) |
| `ldd` | `flexdw` links neither QtGui nor QtWidgets |
| `flex_check.py compare` | the independent FLEX reader (written from `docs/disc-format.md`, not from the program) against `flexdw ls`, `get` and `check` on every synthetic and sample disc: names, sizes, attributes, dates, contents and whether the disc has errors |

`tests/mkicon` writes the icon's PNGs (`icons/`); the `.ico` and `.icns` are made from them with Pillow, and
`icons/flexdiskwrangler.svg` renders the same as the PNGs (checked with Inkscape).
