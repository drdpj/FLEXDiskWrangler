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
| TSC's *6809 FLEX Adaptation Guide* (the FLEX User's Group's copy) | the sector size, the SIR's offsets, NEWDISK's layout and its handling of bad sectors, sector interleave, and TSC's disc formats with a single-density track 0 on double-density discs, as `docs/disc-format.md` records; none of its text or code is in this repository |
| FLEX itself, on those discs: TSC FLEX 9.0 `FLEX.SYS`, `NEWDISK.CMD`, `CAT.CMD` and `DISKNAME.CMD` (SYS900), FHL Color FLEX 5.0:5 `FLEX505.SYS` and FHL's FMS source `FMSFLEX.TXT` / `FMSEQU.TXT` (FLX505, FLX503TX), GIMIX FLEX 2.2 `FLEXDATE.SYS`, `FORMAT.CMD` and `PROT.CMD` (SY22_411), TSC FLEX 2.0 `FLEX2.SYS`, `FLEX.SYS`, `FLEX.COR` and `NEWDISK.CMD` (FL2_0SWT, FLEX2-01) | read and disassembled to settle how FLEX writes (series flex-2), as `docs/disc-format.md` records; none of their code or text is in this repository |
| The FLEX UniFLEX User Group (Michael Evenson, Evenson Consulting Services: its disc image archive and wiki, swtpcemu.com) and The Missing 6809 UniFLEX Archive (rtmx.com, mirrored at retro.co.za/6809/UniFLEX) | where the UniFLEX discs, TSC's UniFLEX sources and manuals below were kept and found; acknowledged with thanks |
| The owner's UniFLEX disc images (`testdata/UniFLEX`, and `C012_1`, `C014` among the FLEX samples; not in this repository) | the layout of UniFLEX discs and the damage seen on real ones, as `docs/uniflex-format.md` records; the tests read them from outside the repo |
| TSC's UniFLEX sources, on those discs and in the owner's kernel source archives: the 6809 kernel's `/structures/sir` and `/structures/fdn`, `/kernal/diskman`, `/kernal/fdnman`, `/kernal/fileman` and `/utils1/makdev` (`ufsources.dsk`), and the UniFLEX Disk Format Program `unibug/format` (core version 1.09) | read to settle the SIR, the fdns, the block map, the free lists and device files (series uniflex-1), as `docs/uniflex-format.md` records; none of their code or text is in this repository |
| TSC's UniFLEX 8" floppy driver `dmf3drvr` and formatter `unibug/formatfd` (UniFLEX sources `uos-s3`, The Missing 6809 UniFLEX Archive) | read to settle how a UniFLEX disc lies on a floppy (one density throughout, block to cylinder, side and sector, side 1's sector numbers) for discs in ImageDisk and DSK files (series containers), as `docs/uniflex-format.md` records; none of their code or text is in this repository |
| HxC Floppy Emulator's FLEX disk layouts (`libhxcfe`'s `DiskLayout_FLEX_*.xml`, Jean-François Del Nero, GPL) and SAMdisk (Simon Owen) | HxC's layouts consulted for how emulators lay out FLEX discs (cylinder 0 single density, side 1 numbered on); both programs used outside the repository to make third-party ImageDisk and DSK files for testing (`design/test-scripts/containers` in the workspace); none of their code is in this repository |
| TSC's *68xxx UniFLEX Programmer's Guide* (1986) and *System Manager's Guide* (1987), *The UniFLEX Operating System* | the mode and permission bits and the time base, as `docs/uniflex-format.md` records; none of their text is in this repository |
