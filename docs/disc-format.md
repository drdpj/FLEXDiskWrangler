# FLEX discs: what the program knows, and how sure it is

FLEX is Technical Systems Consultants' disc operating system for 6800 (FLEX 2) and 6809 (FLEX 9) computers. This
document is the reference for `src/flexdisk.*`. Each fact says where it comes from: **seen** on the sample discs
(15 real FLEX discs, `testdata/FLEXDisks`, listed in the workspace's `design/plan/flex.md`), or **to confirm**
against FLEX itself (`FLEX.SYS` on the FLEX 2 and FLEX 9 system discs among the samples, to be disassembled before
the program writes anything, series flex-2).

## Images

* A **raw** image (`.dsk`, as most FLEX emulators write it): every sector in order, track by track, 256 bytes each.
  Logical sector (t, s) is at byte `(t * sectors-per-track + s - 1) * 256`. **Seen**: on all 15 discs the SIR's
  geometry gives exactly the file's size.
* On a double-sided disc FLEX numbers the sectors of both sides as one track (40 x 36 = 18 per side). The program
  reads the image as one "head" with all the sectors of a cylinder, which is how a raw image holds them. **To
  confirm** for writing other containers: which sector IDs side 1 carries.
* Only raw images are read so far. ImageDisk or DSK files of FLEX discs, and images whose track 0 is single density
  with fewer sectors than the other tracks, have not turned up yet.

## Sectors

Every sector of the directory, a file or the free chain: bytes 0-1 the next sector (track, sector), 0/0 at the end;
bytes 2-3 a record number; bytes 4-255 data (252 bytes). **Seen**: a file's data sectors are numbered 1, 2, 3 ...;
a random file's two map sectors are numbered 0.

## Track 0

* Sectors 1-2: the boot loader.
* Sector 3: the **System Information Record** (SIR). **Seen** on all 15 discs: `$10-$1A` volume name (11 bytes,
  NUL-padded), `$1B-$1C` volume number, `$1D-$1E` first free sector, `$1F-$20` last free sector, `$21-$22` free
  sector count, `$23-$25` the date the disc was made (month, day, year), `$26` the highest track number, `$27`
  sectors per track. The free chain runs from the first free sector to the last, whose link is 0/0, and holds as many
  sectors as the count says (on every healthy sample).
* Sector 4: unused here.
* Sectors 5 onwards: the **directory**, a chain of sectors linked as above.

## The directory

Each directory sector: 16 bytes of header (the link, the record number, then zeros), then 10 entries of 24 bytes:

| Bytes | |
| --- | --- |
| 0-7 | name, NUL-padded |
| 8-10 | extension, NUL-padded |
| 11 | attributes: `$80` write-protected, `$40` delete-protected, `$20` read-protected, `$10` catalogue-protected (**seen**: `$80` on one file; the others **to confirm**) |
| 12 | reserved (kept) |
| 13-14 | first sector |
| 15-16 | last sector |
| 17-18 | number of sectors, a random file's map included |
| 19 | random-access flag: **seen** 2 on random files, 0 otherwise |
| 20 | reserved (kept) |
| 21-23 | date: month, day, year (two digits; years before 70 are read as 20xx) |

* A first byte of 0: a slot never used. A first byte with bit 7 set: a deleted file (**seen**: FLEX writes `$FF`
  there, keeping the rest of the name). **Seen**: one sample (SY22_411) has files after a never-used slot, so the
  program reads every slot of every directory sector. Whether FLEX's own search stops at the first never-used slot
  is **to confirm** (it matters for where new entries go).
* Names **seen**: upper-case letters, digits and `-`.

## Files

A file is the chain from its first sector to its last, exactly as many sectors as the entry says, the last one
linking to 0/0. Its data is every sector's 252 data bytes in chain order: FLEX keeps no byte count, so the size is
known only to a sector.

A **random-access** file starts with two map sectors, then its data. **Seen**: the map is a list of 3-byte runs
(track, sector, count of consecutive sectors) from byte 4 of the first map sector, covering the data sectors in
order. The program gives a random file's data without the two map sectors: the map holds sector addresses, which
mean nothing anywhere else, and a writer makes a new one.

**Text** files end lines with CR and keep runs of spaces as a TAB (`$09`) followed by the number of spaces; the last
sector is padded with NULs. Files are copied out as stored; the viewer expands them.

## What Check reports

Errors (the disc is read-only, and a save is refused): a directory, file or free chain that leaves the disc or loops;
a file chain that doesn't reach its last sector in its count; a sector in two places (two files, a file and the free
chain, the directory and anything); a free chain that doesn't end at the SIR's last free sector or holds a different
number of sectors than the SIR says; a name twice. Warnings: a file's last sector linking on; a record number out of
order; a random file's map that doesn't list its chain; a date that isn't a date; sectors after track 0 that belong to
nothing (two healthy samples, ABASIC_1 and EFORTH, have them).

**Seen** faults on the samples: C009's free chain loops back to 70/8 after 467 of its 550 sectors; FLX503TX's
`SOLMON.TXT` never reaches its last sector (its first sector is record 1, the second record 1 again).

## Not FLEX

Recognised so that the message says what they are, and not opened: **OS-9** (an RBF identification sector in sector
0: the sector count, sectors per track, the allocation map's size matching the count, the root directory inside the
disc) and **UniFLEX** (the SIR in 512-byte block 1 naming the file system at byte 26 and the pack at byte 40). Both
**seen** among the samples (C022, C022_1; C012_1, C014).
