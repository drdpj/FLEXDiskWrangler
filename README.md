# FLEX Disk Wrangler

Work with disc images of **TSC FLEX**, the disc operating system of 6800 (FLEX 2) and 6809 (FLEX 9) computers such as
SWTPC, Gimix and many others: list the files, view them (text with FLEX's space compression expanded, or hex), copy
them out to your computer and in from it, rename, protect and delete them, change the volume name, make new discs,
and check a disc's directory, file chains and free chain. Files drag out to your desktop and to other programs, and in
from them, including the other Disk Wranglers
([Acorn](https://github.com/drdpj/AcornDiskWrangler), [CP/M](https://github.com/drdpj/CPMDiskWrangler)) through the
[retro-files](https://github.com/drdpj/DiskWranglerCore/blob/main/docs/retro-files-format.md) format.

**Changes are made the way FLEX makes them.** Where a new file's entry goes, which sectors it takes, how the directory
grows, what deleting leaves, the sector map of a random-access file, the System Information Record after each change,
and how a new disc is laid out were all read from FLEX itself (FLEX 2.0 and three FLEX 9s, on the sample discs), so a
disc changed here is the disc FLEX would have made. A disc is checked before every save, and the saved copy is read
back and checked before it replaces the original.

## What it opens
| Image | |
|---|---|
| Raw FLEX image (`.dsk`, as most FLEX emulators write it), in whatever geometry the disc's System Information Record gives (tried on discs of 20 to 77 tracks and 10 to 36 sectors a track) | read and write |
| OS-9 and UniFLEX discs | recognised and named, not opened |

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
* **New disc image**: the shapes of real FLEX discs (35 or 40 tracks of 10, 18, 20 or 36 sectors, and 8-inch 77 x 15),
  laid out as FLEX's NEWDISK lays a disc out.
* **The boot loader.** FLEX's boot loader is TSC's own code, so the program carries none: a new disc works under FLEX
  but doesn't start it. To make a disc that starts FLEX, copy the loader from a FLEX system disc you have (New disc
  image, or Tools > Copy boot loader from), put FLEX.SYS on the disc, and link the loader to it (Tools > Link boot
  loader to file), as FLEX's LINK command does.

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
