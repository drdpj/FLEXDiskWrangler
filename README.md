[![Latest release](https://img.shields.io/github/v/release/drdpj/FLEXDiskWrangler?include_prereleases&label=release)](https://github.com/drdpj/FLEXDiskWrangler/releases)

# FLEX Disk Wrangler

Work with disc images of **TSC FLEX**, the disc operating system of 6800 (FLEX 2) and 6809 (FLEX 9) computers such as
SWTPC, Gimix and many others: list the files, view them (text with FLEX's space compression expanded, or hex), copy
them out to your computer and in from it, rename, protect and delete them, change the volume name, make new discs,
and check a disc's directory, file chains and free chain. Files drag out to your desktop and to other programs, and in
from them, including the other Disk Wranglers
([Acorn](https://github.com/drdpj/AcornDiskWrangler), [CP/M](https://github.com/drdpj/CPMDiskWrangler)) through the
[retro-files](https://github.com/drdpj/DiskWranglerCore/blob/main/docs/retro-files-format.md) format.

It also works with discs of **UniFLEX**, TSC's multi-user Unix-like system for the same 6809 machines: their
directory trees, files, permissions and owners, a check of the whole file system, and changes made the way UniFLEX
makes them (see below).

**Changes are made the way FLEX makes them.** Where a new file's entry goes, which sectors it takes, how the directory
grows, what deleting leaves, the sector map of a random-access file, the System Information Record after each change,
and how a new disc is laid out were all read from FLEX itself (FLEX 2.0 and three FLEX 9s, on the sample discs), so a
disc changed here is the disc FLEX would have made. A disc is checked before every save, and the saved copy is read
back and checked before it replaces the original.

## Download
Get the latest build from the [Releases page](https://github.com/drdpj/FLEXDiskWrangler/releases). The releases so
far are release candidates, ahead of 1.0.0.
None of the builds are signed, so your system may warn you the first time you run it.

* **Windows** (10 or later, 64-bit): run the `...-setup.exe` installer (it installs into Program Files; without
  administrator rights, choose to install it just for you), or unzip the `.zip` anywhere and run
  `flexdiskwrangler.exe`. If Windows warns you, choose **More info**, then **Run anyway**.
* **macOS** (12 or later, Intel or Apple Silicon): open the `.dmg` and drag the app to Applications. The first time,
  right-click it and choose **Open**.
* **Linux** (x86_64):
  * **Ubuntu 22.04+ or Debian 12+:** `sudo apt install ./flexdiskwrangler-<version>-linux-amd64.deb` (the `./`
    matters). It uses the system's Qt and adds the program to the applications menu. Remove it with
    `sudo apt remove flexdiskwrangler`.
  * **Any other distribution:** make the `.AppImage` executable (`chmod +x`) and run it. It doesn't add itself to
    the applications menu, so docks show a generic icon unless a tool such as Gear Lever or AppImageLauncher
    integrates it.

Help > About shows the version; Help > Licences shows the licences of the program and everything shipped with it.

## What it opens
| Image | |
|---|---|
| Raw FLEX image (`.dsk`, as most FLEX emulators write it), in whatever geometry the disc's System Information Record gives (tried on discs of 20 to 77 tracks and 10 to 36 sectors a track) | read and write |
| Raw UniFLEX image (`.dsk`: 8-inch floppies and Winchester hard discs, as emulators write them) | read and write |
| FLEX and UniFLEX discs in ImageDisk (`.imd`), DSK or extended DSK files, including a double-density disc's single-density track 0 | read and write; new discs too |
| FLEX and UniFLEX discs in TeleDisk (`.td0`) files | read; Save As makes a copy that can be changed |
| OS-9 discs | recognised and named, not opened |

A disc with a broken chain opens read-only with the fault named (Tools > Check disc); a file whose chain is broken is
shown in red with the reason, and can't be copied out. A file whose size isn't the geometry its System Information
Record gives opens read-only too.

## Changing a disc
* **Add files**, drop them on the list, or Paste: each becomes a FLEX file with its own date (today's if it brings
  none). FLEX can't hold an empty file, so an empty one is refused. A file from another FLEX disc keeps its
  protection and, if it was random-access, becomes a random-access file again with a new sector map.
* **Rename**, **Properties** (the name and the write, delete, read and catalogue protection) and **Delete**. As in
  FLEX, a write-, delete- or read-protected file can't be deleted, renamed or replaced: take the protection off in
  Properties first.
* **Volume name**: the name (up to 8 characters) and number in the System Information Record.
* **New disc image**: TSC's formats and the shapes of real FLEX discs (5¼-inch 35 x 10, 35 x 18, 35 x 20, 40 x 10,
  40 x 20 and 40 x 36; 8-inch 77 x 15, 77 x 26, 77 x 30 and 77 x 52), laid out as FLEX's NEWDISK lays a disc out. On a
  double-density disc whose track 0 is single density, a raw image pads track 0 to a full track, as emulators expect;
  choose ImageDisk or DSK in the file dialog to get the disc itself, sides, density and a single-density track 0 as
  TSC's format has them.
* **Save As** a disc held in an ImageDisk, DSK or TeleDisk file as ImageDisk, DSK or raw; a raw image is saved as raw
  (it doesn't say how its sectors lie on the disc). `flexdw convert` does the same.
* **The boot loader.** FLEX's boot loader is TSC's own code, so the program carries none: a new disc works under FLEX
  but doesn't start it. To make a disc that starts FLEX, copy the loader from a FLEX system disc you have (New disc
  image, or Tools > Copy boot loader from), put FLEX.SYS on the disc, and link the loader to it (Tools > Link boot
  loader to file), as FLEX's LINK command does.

## UniFLEX discs
A UniFLEX disc opens as a tree of folders. The columns show each file's access (`rwxr-x`: the owner's read, write
and execute, then everyone else's, and `s` when it runs with its owner's user id), owner, links, size, the time it was
last changed and its file descriptor node (fdn). View shows UniFLEX text with its CR line ends; Extract, dragging out
and Copy give the files exactly as stored, names as they are (UniFLEX names are case-sensitive), folders as folders.
Properties and Disc information show what the disc records; Tools > Check disc checks the whole file system: every
file's blocks, the directories, link counts, the free list and the free fdns.

**Changing a UniFLEX disc** works as on a FLEX disc: Add files, drops and Paste (into the folder selected, or the one
dropped on), **New directory**, dragging files and folders onto another folder in the tree to move them there (the folder they will land in is highlighted as you hover), Rename, Properties (the name, the access bits and the owner), Delete (a folder with everything in it),
Volume name (the pack name, file system name and number) and New disc image (TSC's four 8-inch formats). Every change
is made **the way UniFLEX's own kernel makes it**: which blocks and file descriptor nodes are taken and freed, in what
order, where a new name goes in a directory, how the free lists and the System Information Record end up. This was read
from TSC's kernel, utility and formatter sources and then checked against UniFLEX 3.14 itself, running in an emulator,
byte for byte. A file brought in gets the time of the change, as UniFLEX's `copy` gives it, and keeps the access and
owner it brings from another UniFLEX disc. Folders can be renamed and moved (UniFLEX's own tools do neither; the
program does it with the kernel's own steps, its `..` rewritten, so the disc stays as UniFLEX expects). A disc with errors (several of the surviving images were copied from running systems and are inconsistent)
opens read-only, with the faults named.

What a UniFLEX disc holds was read from TSC's own sources ([docs/uniflex-format.md](docs/uniflex-format.md)), kept
with the disc images by the **FLEX UniFLEX User Group** and **The Missing 6809 UniFLEX Archive**, whose work made this
possible.

## Files on your computer
Files come out exactly as they are on the disc, so that copying a file out and back in gives the same file: a FLEX
text file keeps its CR line ends and its space compression (a TAB followed by the number of spaces). The viewer
shows the text expanded. A random-access file comes out without its two sector-map sectors. Names are lower-cased,
and the file gets the date from the directory.

## The command-line program
`flexdw` (installed beside the program) does the same from a terminal or a script, with the same checks:

    flexdw info DISC.DSK          # what the disc is
    flexdw ls DISC.DSK            # the files
    flexdw get DISC.DSK / out     # copy every file into the folder out
    flexdw check DISC.DSK         # the disc's check; exit code 4 if it has errors
    flexdw put DISC.DSK notes.txt /           # add a file
    flexdw rm DISC.DSK OLD.TXT                # delete one
    flexdw new NEW.DSK --format=flex-40x10 --title=WORK     # a new disc (flexdw new --list: the shapes)
    flexdw ls UNIFLEX.DSK -R                  # a UniFLEX disc's whole tree
    flexdw mkdir UNIFLEX.DSK /work            # a UniFLEX directory
    flexdw new NEW.DSK --format=uniflex-ds-dd --title=pack  # a new UniFLEX disc

Add `--json` for output a program can read. The commands are those of the other Disk Wranglers' console programs
([cli.md](https://github.com/drdpj/DiskWranglerCore/blob/main/docs/cli.md)).

## Building
Qt 6 (Qt 5.15 also works) and a C++17 compiler. Clone with the shared core:

    git clone --recurse-submodules https://github.com/drdpj/FLEXDiskWrangler.git
    cd FLEXDiskWrangler
    qmake flexdiskwrangler.pro && make -j$(nproc)      # the window
    qmake flexdw.pro && make -j$(nproc)                # the command-line program (in another build folder)

or with CMake: `cmake -S . -B build && cmake --build build -j$(nproc)` (`-DUSE_QT5=ON` for Qt 5).

## Licence
GNU General Public License v3 or later. Copyright (C) 2026 Daniel Jameson. Qt is used under the LGPL v3; see
`THIRD-PARTY-NOTICES.md` and Help > Licences.
