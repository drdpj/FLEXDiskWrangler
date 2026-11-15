# FLEX Disk Wrangler

View disc images of **TSC FLEX**, the disc operating system of 6800 (FLEX 2) and 6809 (FLEX 9) computers such as
SWTPC, Gimix and many others: list the files, view them (text with FLEX's space compression expanded, or hex), copy
them out to your computer, and check a disc's directory, file chains and free chain. Files drag out to your desktop
and to other programs, including the other Disk Wranglers
([Acorn](https://github.com/drdpj/AcornDiskWrangler), [CP/M](https://github.com/drdpj/CPMDiskWrangler)) through the
[retro-files](https://github.com/drdpj/DiskWranglerCore/blob/main/docs/retro-files-format.md) format.

**This first version only reads discs.** Adding, deleting and renaming files, changing attributes and the volume
name, and making new discs come next.

## What it opens
| Image | |
|---|---|
| Raw FLEX image (`.dsk`, as most FLEX emulators write it), in whatever geometry the disc's System Information Record gives (tried on discs of 20 to 77 tracks and 10 to 36 sectors a track) | read |
| OS-9 and UniFLEX discs | recognised and named, not opened |

A disc with a broken chain opens read-only with the fault named (Tools > Check disc); a file whose chain is broken is
shown in red with the reason, and can't be copied out.

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
