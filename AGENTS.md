# AGENTS.md - working on FLEX Disk Wrangler

Read this before changing code: what the program is, where things are, the rules that must not be broken, and how
to build and test. Two companion documents hold the detail:

* **[docs/disc-format.md](docs/disc-format.md)** - the facts about FLEX discs, how sure we are of each, and the real
  discs they were seen on. Read it before touching `flexdisk.*` or detection.
* **[docs/testing.md](docs/testing.md)** - every test and the data it uses.

The plan for this program (decisions, the sample discs, the series) is in the workspace's `design/plan/flex.md`.

## What this is
A Qt 6 / Qt 5.15 (C++17) desktop tool for TSC FLEX disc images (6800 FLEX 2, 6809 FLEX 9), the third program of the
family with Acorn Disk Wrangler and CP/M Disk Wrangler, built on **DiskWranglerCore as it is** (owner, 15 Nov 2026).
One disc per window. **Series flex-1 only reads**: it lists, views, extracts, drags out and copies files, and checks
the disc; every change is refused (`FlexVolume::ReadOnlyText`). Series flex-2 adds writing.

* **Licence and headers.** GPLv3-or-later. Every source file starts with the header naming
  `Copyright (C) 2026 Daniel Jameson` (`#` comments for scripts and build files); put it on any new file.
* **Other people's work** goes in `THIRD-PARTY-NOTICES.md` with its terms; material only consulted for facts in its
  "Consulted" table.
* **Never commit third-party disc images**: the tests read the sample discs from outside the repo, and make their own
  synthetic ones (`tests/flex_check.py make`).
* **`core/` is the git submodule DiskWranglerCore**, pinned to a tag (v1.1.0). Never edit files under `core/` from
  here; a change the core needs is the owner's decision (the plan says to stop and ask).

## Source map (program in `src/`, tests in `tests/`)
| File | Role |
|---|---|
| `flexdisk.h/.cpp` | `Flex::Disk`, the filesystem on a `dw::device::Linear`: the SIR, the directory (every slot of every directory sector), each file's chain, reading a file (a random file without its two map sectors), `check()`. QtCore, no core interfaces |
| `flexvolume.h/.cpp` | DiskWranglerCore's side: `Flex::NameRules`, `FlexVolume` (the `dw::fs::Volume`, `fsId()` "flex", flat, attributes write / delete / read / catalog / random / sectors, retro-files family `flex`), `OpenedDisc` (the window's Document and `flexdw`'s opened image alike: the raw layout from the SIR, the checked save, Save As raw only), `self()` / `appInfo()`, `FlexProbe`, `OtherSystemsProbe` (OS-9, UniFLEX: named, not supported), `verdict()` |
| `flextext.h/.cpp` | FLEX text for the viewer: CR line ends, TAB + count space compression, NUL padding |
| `hostpolicy.h/.cpp` | QtCore, shared with `flexdw`: `Flex::HostPolicy` (files out as stored, upper-case names lower-cased) and `hostRead()` (the core's host reader's rules) |
| `mainwindow.*` | the window on the core's kit (`dw::ui::DiscWindow`): `FlexPane` (drags out with `execDragOut`; a window with no disc opens dropped images), the hooks (columns, rows, status, opening, Save As, Properties shown read-only, Disc information) |
| `viewer.*` | View: the core's viewer dialog with FLEX's text view in place of the plain one |
| `icon.*` | the 5.25-inch floppy icon (QPainter); `icons/` holds the PNGs (`tests/mkicon`), the `.ico` / `.icns` (Pillow) and a hand-written SVG that renders the same |
| `cli/` | `flexdw`: `FlexCli::Backend` (open through the same detection and `OpenedDisc`; `new` refused until flex-2); the commands are the core's |

**Raw images only, so far.** `OpenedDisc::attach` gives the raw file a layout of `last track + 1` cylinders, one head,
`sectors` sectors of 256 bytes from ID 1, so the stored bytes are FLEX's logical sectors in order. The probe looks only
at raw files. (A FLEX disc in ImageDisk or DSK, or with a single-density track 0, needs its own work: none in the
samples yet.)

## The rules that must not be broken
1. **Nothing is written to the file except by `Container::commit()` / `saveAs()`**; filesystem code stages writes.
2. **Validate before writing**: `OpenedDisc::save` runs `check()` and refuses on any error, refuses if the file
   changed on disk since it was opened, and the written copy is read back and checked (`checkWritten`).
3. **A disc that fails `check()` is read-only** (`OpenedDisc::readOnlyWhy`), and a file whose chain is broken can't be
   read (its `problem` says why): the program never guesses where a broken chain goes.
4. **No rollback of its own, and a faithful `reload()`**: changes (flex-2) go through `Disc::transact`; `reload()` must
   give exactly the state a change left in memory (`voltest` compares them).
5. **Files go out as stored** (owner, 15 Nov 2026): no conversion of text on Extract, drags or Copy.
6. **FLEX behaviour follows FLEX itself**: anything about how FLEX writes (deletion, where new entries and sectors go,
   the SIR after a change, new discs) is settled from `FLEX.SYS` on the sample system discs, and recorded in
   `docs/disc-format.md`, before the program does it.

## The window (kept in step with the family)
Menus, button bar and context menu in the family's order (CP/M Disk Wrangler's AGENTS.md, "The window"): File: New
window, New disc image, Open | Save, Save as, Revert | Create local config file | Close, Quit. Edit: View (F3), Extract
to folder (Ctrl+E) | Add files | Copy, Paste | Rename (F2), Properties (Alt+Return), Delete | Volume name. Tools: Check
disc (Ctrl+K), Disc information (Ctrl+I) | Keep a .bak copy when saving. Buttons: View, Extract, Add files, Rename,
Properties, Delete. The items that change a disc are there and greyed in flex-1. `guitest` asserts the order. Window
title `<file> * - FLEX Disk Wrangler`.

## Build
```
qmake flexdiskwrangler.pro && make -j$(nproc)     # or: cmake -S . -B build && cmake --build build -j$(nproc)
qmake flexdw.pro && make -j$(nproc)               # the console program, in its own build folder
```
A new source file goes in both `.pro` files that need it, `CMakeLists.txt`, and any `tests/*.pro` that builds it.
**Qt 6 and Qt 5.15 must both keep building.** `version.pri` / `version.cmake` are the family's identical copies
(APP_VERSION from `git describe`). No source file may have the name of one of the core's (qmake puts every object
in one folder).

Not yet here (they come with a release): the CI workflow (the family's shared `build.yml` checks the console
program with `new`, which flex-2 adds) and a Windows installer (its AppId lives in the core's `ci/installer.iss`, a
core change).

## Test
```
tests/run_all.sh <build dir> [<the sample FLEX discs' folder>]
```
ends "ALL PASSED". It needs Python 3. [docs/testing.md](docs/testing.md) lists each test.
