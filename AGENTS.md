# AGENTS.md - working on FLEX Disk Wrangler

Read this before changing code: what the program is, where things are, the rules that must not be broken, and how
to build and test. Two companion documents hold the detail:

* **[docs/disc-format.md](docs/disc-format.md)** - the facts about FLEX discs, how sure we are of each, and the real
  discs they were seen on. Read it before touching `flexdisk.*` or detection.
* **[docs/uniflex-format.md](docs/uniflex-format.md)** - the same for UniFLEX discs, from TSC's kernel and formatter
  sources. Read it before touching `uniflexfs.*`.
* **[docs/testing.md](docs/testing.md)** - every test and the data it uses.

The plan for this program (decisions, the sample discs, the series) is in the workspace's `design/plan/flex.md`.

## What this is
A Qt 6 / Qt 5.15 (C++17) desktop tool for TSC FLEX disc images (6800 FLEX 2, 6809 FLEX 9), the third program of the
family with Acorn Disk Wrangler and CP/M Disk Wrangler, built on **DiskWranglerCore as it is** (owner, 15 Nov 2026).
One disc per window. It lists, views, extracts, drags out and copies files, and checks the disc (series flex-1); it
adds, deletes, renames and protects files, changes the volume name and number, makes new discs and copies a boot
loader from another disc (series flex-2), every change **as FLEX's own file management system makes it**
(`docs/disc-format.md`, "How FLEX writes"). It also reads **UniFLEX** discs (series uniflex-1: the tree, the files, the
check of the whole file system) and changes them as UniFLEX's kernel and utilities do (series uniflex-2: files,
directories, renames and moves of files, access, owners, the SIR's names, new discs as TSC's formatter makes them).

* **Licence and headers.** GPLv3-or-later. Every source file starts with the header naming
  `Copyright (C) 2026 Daniel Jameson` (`#` comments for scripts and build files); put it on any new file.
* **Other people's work** goes in `THIRD-PARTY-NOTICES.md` with its terms; material only consulted for facts in its
  "Consulted" table.
* **Never commit third-party disc images**: the tests read the sample discs from outside the repo, and make their own
  synthetic ones (`tests/flex_check.py make`).
* **`core/` is the git submodule DiskWranglerCore**, shared with Acorn and CP/M Disk Wrangler and pinned to a tag (clone with
  `--recurse-submodules`, or `git submodule update --init`; v1.1.1 was the first with this program's installer AppId).
  Never edit files under `core/` from here; a change the core needs is the owner's decision (the plan says to stop and ask).

## Source map (program in `src/`, tests in `tests/`)
| File | Role |
|---|---|
| `flexdisk.h/.cpp` | `Flex::Disk`, the filesystem on a `dw::device::Linear`: the SIR, the directory (every slot of every directory sector), each file's chain, reading a file (a random file without its two map sectors), `check()`; writing as FLEX does (`writeFile`, `deleteFile`, `renameFile`, `setProtection`, the volume name and number, the boot loader's sectors and link, `makeBlank` as NEWDISK), each change ending in a `reload()`. QtCore, no core interfaces |
| `flexvolume.h/.cpp` | DiskWranglerCore's side: `Flex::NameRules`, `FlexVolume` (the `dw::fs::Volume`, `fsId()` "flex", flat, attributes write / delete / read / catalog (editable) / random / sectors, retro-files family `flex`; a file written keeps the date and protection it brings, as FLEX's COPY does), `OpenedDisc` (the window's Document and `flexdw`'s opened image alike: a FLEX disc (`disk()`, mount "FLEX", group "flex": the raw layout from the SIR, read-only for errors or a size that isn't the SIR's geometry) or a UniFLEX one (`uniflex()`, mount "UniFLEX", group "uniflex": the file as 512-byte blocks, read-only for errors or a file shorter than its volume), the checked save, Save As (`canSaveAs`: a raw image raw only, a disc in a container any kind but TeleDisk), `convertTo` (`flexdw convert`)), `self()` / `appInfo()`, the presets (the sample discs' shapes, TSC's FLEX formats, and TSC's four 8" UniFLEX formats, `fsId` "uniflex"), `createBlank` (a UniFLEX preset through `UniFlex::formatDisc`; raw, or an ImageDisk / DSK file of the disc itself: `writeNew`, the presets' `fm`, `rate` and `containers`), `readBootLoader`, `FlexProbe`, `OtherSystemsProbe` (OS-9: named, not supported), `verdict()` (FLEX, UniFLEX, OS-9); the presets carry `track0` (a short track 0 is padded in the image) |
| `uniflexfs.h/.cpp` | `UniFlex::Fs`, the UniFLEX filesystem on a `dw::device::Linear` of 512-byte blocks: the SIR, the fdns, the tree from the root (`Node`s, each directory listed once), a file's block map (10 direct, single, double, triple indirect; never-written blocks read as zeros), the free list as the kernel takes it, `check()`. Reading only. QtCore, no core interfaces |
| `uniflexvolume.h/.cpp` | `UniFlex::NameRules` (1-14 bytes, case-sensitive), `UniFlexVolume` (`fsId()` "uniflex", directories; attributes the seven access bits and the owner (retro-files family `uniflex`), links, fdn, type; each change a `UniFlex::Writer` call with the time now from the core's clock), `UniFlexProbe` (a UniFLEX SIR and a root directory: certain when the file is exactly the volume and swap space) |
| `uniflexwrite.h/.cpp` | `UniFlex::Writer`: every change as UniFLEX makes it, the kernel's routines step by step (`aldb` / `filfr`, `frdb` / `mkfch`, `alfdn` / `filfdn`, `frfdn`, `mapfil` writing, `rmvfil`, `crfil`, `link`, `unlink`, `update`) and the utilities on them (`copy`, `crdir`, `kill`, `rename`, `perms`, `owner`), writing through the disc's `Linear`, each change ending in `Fs::reload`; `formatDisc` (a new disc as TSC's `format` lays it out). QtCore, no core interfaces |
| `flextext.h/.cpp` | FLEX text for the viewer: CR line ends, TAB + count space compression, NUL padding |
| `hostpolicy.h/.cpp` | QtCore, shared with `flexdw`: `Flex::HostPolicy` (files out as stored, upper-case names lower-cased) and `hostRead()` (the core's host reader's rules) |
| `mainwindow.*` | the window on the core's kit (`dw::ui::DiscWindow`): two panes, one shown at a time and no tabs (`paneGroups` "flex" and "uniflex", `paneShown`); `FlexPane` (drags out with `execDragOut`; a window with no disc opens dropped images, a writable disc imports dropped files, retro-files JSON and items from another window (`DW_MIME_ITEMS`: `itemsPayload`); a UniFLEX tree takes its own items and moves them to the folder dropped on, `moveItems`; while a drag is over it, the folder the drop will land in is the current row, as in Acorn Disk Wrangler), the hooks (columns, rows, status, opening, Save As, Properties with the name and protection, Volume name and number, Disc information with the boot loader; for UniFLEX the tree's columns and rows, a Properties and a Disc information that only show), New disc image, Add files, Paste, Rename, Copy boot loader from / Link boot loader to file. `hasDisc()` is any disc, `hasFlex()` / `hasUniflex()` which |
| `viewer.*` | View: the core's viewer dialog with FLEX's text view, or UniFLEX's (CR line ends, TABs kept: the dialog's context `fs` says which), in place of the plain one |
| `icon.*` | the 5.25-inch floppy icon (QPainter); `icons/` holds the PNGs (`tests/mkicon`), the `.ico` / `.icns` (Pillow) and a hand-written SVG that renders the same |
| `cli/` | `flexdw`: `FlexCli::Backend` (open through the same detection and `OpenedDisc`; `new` from the presets, `--title` the volume or pack name; a file whose size isn't its geometry read-only); the commands are the core's |

**Raw images and containers.** `OpenedDisc::attach` gives a raw file a layout of `last track + 1` cylinders, one head,
`sectors` sectors of 256 bytes from ID 1, so the stored bytes are FLEX's logical sectors in order. A double-density
disc's single-density track 0 (fewer sectors; TSC's *6809 FLEX Adaptation Guide*, Appendix B) is read padded to the
full track (as the sample EFORTH) or stored at its own size: `storedTrack0` / `endsTrack0` find the second, the layout
gets the core's `RawLayout::t0sectrk`, and `Disk::offsetOf` / `inRange` use `track0Sectors()`. An ImageDisk, DSK or
TeleDisk file (series containers) gives its stored bytes: cylinder by cylinder, side 0 then side 1, sectors in ID
order, with a single-density track 0 at its own size (the core's `Container::setOwnTrack0`, turned on by `verdict()`
and `attach`: core v1.2.0), so it reads exactly as the raw file with track 0 stored short; the UniFLEX probe takes
containers of uniform 512-byte tracks. `docs/disc-format.md` and `docs/uniflex-format.md` say how the discs lie. Raw new
discs keep a short track 0 padded; container ones are the disc itself (`writeNew`).

## The rules that must not be broken
1. **Nothing is written to the file except by `Container::commit()` / `saveAs()`**; filesystem code stages writes.
2. **Validate before writing**: `OpenedDisc::save` runs `check()` and refuses on any error, refuses if the file
   changed on disk since it was opened, and the written copy is read back and checked (`checkWritten`).
3. **A disc that fails `check()` is read-only** (`OpenedDisc::readOnlyWhy`), and a file whose chain is broken can't be
   read (its `problem` says why): the program never guesses where a broken chain goes.
4. **No rollback of its own, and a faithful `reload()`**: changes go through `Disc::transact`; `reload()` must give
   exactly the state a change left in memory (every `Disk` change ends by reading the disc again; `voltest` compares).
   Changing the disc outside `FlexVolume` (the boot loader) is done in a transaction too.
5. **Files go out as stored** (owner, 15 Nov 2026): no conversion of text on Extract, drags or Copy.
6. **UniFLEX behaviour follows UniFLEX itself** (series uniflex-2): how a change is made (which blocks and fdns are
   taken and freed and in what order, where a new entry goes, the free lists and the SIR after it, a new disc) is
   settled from TSC's kernel, utility and formatter sources and recorded in `docs/uniflex-format.md` ("How UniFLEX
   writes", "A new disc", "Where the program chooses") before the program does it; it was checked against UniFLEX
   3.14 running in MAME (the workspace's `design/test-scripts/mame-uniflex`). `tests/uniflex_check.py` holds the
   program to it: its reader (`compare`, `samples`) and its model of the kernel (`writes`, byte for byte). A UniFLEX
   disc with check errors, or a file shorter than its volume, is read-only.
7. **FLEX behaviour follows FLEX itself**: anything about how FLEX writes (deletion, where new entries and sectors go,
   the SIR after a change, new discs) is settled from FLEX on the sample system discs (FLEX 2.0 and three FLEX 9s,
   disassembled; the FMS's source on FLX505), and recorded in `docs/disc-format.md`, before the program does it.
   `tests/flex_check.py writes` holds the program to it byte for byte.
8. **No TSC code in the program**: FLEX's boot loader is copied from a disc the user has, never carried.

## The window (kept in step with the family)
Menus, button bar and context menu in the family's order (CP/M Disk Wrangler's AGENTS.md, "The window"): File: New
window, New disc image, Open | Save, Save as, Revert | Create local config file | Close, Quit. Edit: View (F3), Extract
to folder (Ctrl+E) | Add files, New directory (F7, as Acorn's; UniFLEX discs only) | Copy, Paste | Rename (F2), Properties
(Alt+Return), Delete | Volume name. Tools: Check disc (Ctrl+K), Disc information (Ctrl+I) | Copy boot loader from, Link
boot loader to file | Keep a .bak copy when saving. Buttons: View, Extract, Add files, New dir, Rename, Properties,
Delete. `guitest` asserts the order. Window title `<file> * - FLEX Disk Wrangler`. On a UniFLEX disc, files copied in
go to the folder selected (or the selected file's folder), or the one dropped on (a drop below the rows: the root, never
the row being dragged). Dragging within a UniFLEX tree moves (a name the folder already has is refused, never replaced).

## Build
```
qmake flexdiskwrangler.pro && make -j$(nproc)     # or: cmake -S . -B build && cmake --build build -j$(nproc)
qmake flexdw.pro && make -j$(nproc)               # the console program, in its own build folder
```
A new source file goes in both `.pro` files that need it, `CMakeLists.txt`, and any `tests/*.pro` that builds it.
**Qt 6 and Qt 5.15 must both keep building.** `version.pri` / `version.cmake` are the family's identical copies
(APP_VERSION from `git describe`). No source file may have the name of one of the core's (qmake puts every object
in one folder).

CI: `.github/workflows/build.yml` is the family's shared workflow, identical in the three repos (Windows, macOS and
Linux packages on every push; a release on a `v*` tag, with a Windows installer, whose AppId for this program is in the
core's `ci/installer.iss` since v1.1.1).

## Test
```
tests/run_all.sh <build dir> [<the sample FLEX discs' folder> [<the UniFLEX sample discs' folder>]]
```
ends "ALL PASSED". It needs Python 3. [docs/testing.md](docs/testing.md) lists each test.
