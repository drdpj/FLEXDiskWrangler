# Testing FLEX Disk Wrangler

    tests/run_all.sh <build dir> [<the sample FLEX discs' folder>]

builds everything with whichever `qmake` is first on the path and ends `ALL PASSED` or `SOME TESTS FAILED`. It needs
Python 3. Without the second argument the sample discs aren't checked; with it, every sample is read (copies, in a
temporary folder).

| Test | What it checks |
| --- | --- |
| core tests | DiskWranglerCore's own tests (settings, transfer, UI helpers, containers and saving, the Importer, the command line, the Volume interface, the DFS and FAT modules), built from the pinned `core/` |
| `tests/flex_check.py make` | makes the synthetic discs everything else uses (deterministic): `basic.dsk` (text, binary, random, protected, deleted, dated and undated files), `big.dsk` (45 files over 5 directory sectors, 40 x 36), `empty.dsk` (77 x 15), `full.dsk` (its directory full: the next file makes it grow), `frag.dsk` (a free chain in which no two sectors follow each other), `random.dsk` (random files of 300 and 200 data sectors to copy), five damaged discs (`bad-freeloop`, `bad-freecount`, `bad-chain`, `bad-crosslink`, `bad-dirloop`), `short.dsk` (a file shorter than its SIR says), an OS-9 and a UniFLEX disc, and noise |
| `voltest` | detection by tiers on every synthetic disc; the FLEX volume through `dw::fs::Volume` (names, sizes, attributes, dates, contents, the retro-files family); every kind of change inside `Disc::transact`, each leaving a disc that checks clean and a faithful `reload()`; what FLEX refuses, each leaving the staged disc exactly as it was; a random file copied and read back; the directory growing; the checked save read back; Save As byte for byte; new discs of every preset; a boot loader read from a disc, copied and linked; each damaged disc's fault named and the disc read-only, and `short.dsk` read-only for its size; the name rules; and every sample disc (17 FLEX discs open, the 3 damaged ones fail their check and are read-only, the 5 others are named OS-9 or UniFLEX; on a copy of each healthy one a file added and one deleted, saved and read back) |
| `guitest` | the window (offscreen): the menus in the family's order, what is enabled on a writable disc and greyed on a damaged one, the rows and their cells, View (FLEX text expanded), Extract (as stored, lower-case names, dated), Copy, Properties, Disc information, Check; on a copy of `basic.dsk`: Add files (an empty file refused), Rename, Properties, Volume name, Delete (a protected file refused), Save and open again; New disc image with a boot loader copied, Link boot loader, Copy boot loader; and discs it doesn't open |
| `tests/cli/run_cli.sh` | `flexdw`'s golden cases (`tests/cli/cases/`): info, check, ls and get; put, rm, mv, attr, title, copy (a random file) and new; what FLEX refuses (an empty file, a name that exists, protected files, a full disc, a map over 168 runs, a long volume name); on the synthetic discs, outputs and image hashes compared; `--update` rewrites them (only in a commit of its own that says why) |
| `ldd` | `flexdw` links neither QtGui nor QtWidgets |
| `flex_check.py compare` | the independent FLEX reader (written from `docs/disc-format.md`, not from the program) against `flexdw ls`, `get` and `check` on every synthetic and sample disc: names, sizes, attributes, dates, contents and whether the disc has errors |
| `flex_check.py writes` | `flexdw`'s changes against the independent model of how FLEX writes (`Fms`, written from `docs/disc-format.md`, "How FLEX writes", not from the program): a new disc of every preset against NEWDISK's layout, and scenarios of put, copy (random files), rm, mv, attr and title on the synthetic discs and three sample system discs (FLEX2_B, SYS900, FL2_0SWT), the image compared byte for byte after every step and every refusal matched |

`tests/mkicon` writes the icon's PNGs (`icons/`); the `.ico` and `.icns` are made from them with Pillow, and
`icons/flexdiskwrangler.svg` renders the same as the PNGs (checked with Inkscape).
