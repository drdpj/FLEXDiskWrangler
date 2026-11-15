# Third-party notices

FLEX Disk Wrangler is Copyright (C) 2026 Daniel Jameson and is licensed under the GNU General Public License,
version 3 or (at your option) any later version (see `LICENSE`). This file lists everything in it, or used to make
or test it, that comes from someone else, and on what terms.

## Included in the source

### DiskWranglerCore - `core/`
The code shared by the Disk Wranglers (containers, the filesystem interface, the window kit, the command line), a git
submodule. Copyright (C) 2026 Daniel Jameson, GPL v3 or later. Its own notices are in `core/`.

## Used by the program

### Qt
The program uses Qt (<https://www.qt.io>) under the GNU Lesser General Public License v3 (`licences/LGPL-3.0.txt`),
linked as shared libraries so that they can be replaced. A release build also lists the licences of every library
shipped with it (Help > Licences).

## Consulted for facts only

| What | Used for |
| --- | --- |
| The owner's FLEX disc images (`testdata/FLEXDisks`, not in this repository) | the layout of the SIR, the directory, file and free chains, random files' sector maps and deleted entries, as `docs/disc-format.md` records; the tests read them from outside the repo |
