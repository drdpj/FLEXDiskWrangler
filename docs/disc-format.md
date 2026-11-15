# FLEX discs: what the program knows, and how sure it is

FLEX is Technical Systems Consultants' disc operating system for 6800 (FLEX 2) and 6809 (FLEX 9) computers. This
document is the reference for `src/flexdisk.*`. Each fact says where it comes from: **seen** on the sample discs
(17 real FLEX discs, `testdata/FLEXDisks`, listed in the workspace's `design/plan/flex.md`), or **FLEX** (read from
FLEX's own code, below).

## Where the facts come from (series flex-2, 15 Nov 2026)

How FLEX writes was settled from FLEX itself, disassembled from the sample discs:

| Source | Disc | What it is |
| --- | --- | --- |
| `FLEX.SYS` | SYS900 | TSC **FLEX 9.0** (6809), linked from the boot at 17/3 |
| `FLEX505.SYS` | FLX505 | FHL Color FLEX **5.0:5** (6809, a licensed FLEX 9) |
| `FLEXDATE.SYS` | SY22_411 | GIMIX **FLEX 2.2** (6809, a licensed FLEX 9) |
| `FMSFLEX.TXT`, `FMSEQU.TXT` | FLX505, FLX503TX | FHL's source of the FMS (file management system), "last mod 2/8/83": a labelled disassembly of TSC's FMS, its labels TSC FLEX 9.0's addresses |
| `NEWDISK.CMD`, `CAT.CMD`, `DISKNAME.CMD` | SYS900 | TSC's disc formatter (SWTPC single density), the catalogue, a volume-name utility |
| `FORMAT.CMD`, `PROT.CMD` | SY22_411 | GIMIX's formatter (single / double density), the protection command |
| `FLEX2.SYS`, `FLEX.SYS`, `FLEX.COR` | FL2_0SWT, FLEX2-01 | TSC **FLEX 2.0** (6800), FMS at `$B400`; FL2_0SWT's boot is linked to `FLEX2.SYS` (1/10) |
| `NEWDISK.CMD` | FL2_0SWT | TSC's FLEX 2 disc formatter |

* The FHL source agrees with `FLEX505.SYS` instruction for instruction (1,085 instructions, every numeric operand). The
  FMS code of FHL 5.0:5 and GIMIX 2.2 is byte-identical. TSC FLEX 9.0's FMS is the same code at slightly different
  addresses; the only differences are a padding `NOP`, `LDB #0` for `CLRB`, and two instructions swapped in
  random-record seeking.
* **FLEX 2.0's FMS is the same in all five images** on FL2_0SWT and FLEX2-01, and it is the 6800 original of FLEX 9's:
  the directory search (`B8ED`), taking a sector (`B7B3`), open for write and the directory's growth (`B9FF`, `BA66`),
  close and the empty file (`BAE8`), delete (`BBEE`, the `$FF` mark at `BBBF`), rename (`BB4E`) and the random map
  (`BC69`) follow the same steps on the same FCB bytes; FLEX 2's NEWDISK writes the SIR at the same places.
  **Everything below about writing holds for FLEX 2.0 and the three FLEX 9s.** The discs agree: FL2_0SWT's two deleted
  files are exactly the last 29 sectors of its free chain, in the order they were deleted, and FLEX2_B's three deleted
  files lie in order near the end of its own.
* FLEX2_A and FLEX2_B are FLEX 2 utility discs with no DOS on them (their boot sector's link is 0/1, not linked).

## Images

* A **raw** image (`.dsk`, as most FLEX emulators write it): every sector in order, track by track, 256 bytes each.
  Logical sector (t, s) is at byte `(t * sectors-per-track + s - 1) * 256`. **Seen**: on all 17 discs the SIR's
  geometry gives exactly the file's size.
* On a double-sided disc FLEX numbers the sectors of both sides as one track. **FLEX** (TSC NEWDISK): side 1's sectors
  carry the IDs that follow side 0's (11-20 on a 10-sector side), so a container's sector IDs are the logical ones.
* Only raw images are read so far. ImageDisk or DSK files of FLEX discs have not turned up yet.
* **Seen**: EFORTH (35 x 18) has a directory only in sectors 5-10 of track 0, and sectors 11-18 of track 0 in the
  image belong to nothing: a disc whose track 0 was formatted single density (10 sectors) with the other tracks at 18.
  The raw image pads track 0 to 18. Nothing FLEX writes ever goes to track 0 outside the directory chain, so this
  needs nothing more.

## Sectors

Every sector of the directory, a file or the free chain: bytes 0-1 the next sector (track, sector), 0/0 at the end;
bytes 2-3 a record number; bytes 4-255 data (252 bytes).

* **FLEX**: a sector taken from the free chain is cleared to zeros, then given its record number; the link is
  written when the next sector is taken. So a file's last sector is padded with zeros and links to 0/0.
* Record numbers: a file's data sectors 1, 2, 3 ...; a random file's two map sectors 0 (**FLEX**, **seen**).

## Track 0

* Sectors 1-2: the boot loader. **FLEX** (TSC NEWDISK) writes its loader into sector 1 only; sector 2 keeps the link
  NEWDISK formatted it with (0/3) and zeros. **Seen** on every sample.
* Sector 3: the **System Information Record** (SIR). `$00-$0F` zero (link 0/0); `$10-$17` volume name and `$18-$1A`
  its "extension" (11 bytes, NUL-padded), `$1B-$1C` volume number, `$1D-$1E` first free sector, `$1F-$20` last free
  sector, `$21-$22` free sector count, `$23-$25` the date the disc was made (month, day, year), `$26` the highest
  track number, `$27` sectors per track. **Seen** on all 17 discs; **FLEX** (NEWDISK, GIMIX FORMAT).
* Sector 4: unused; it keeps NEWDISK's link 0/5 (**seen** on every sample but C009).
* Sectors 5 onwards: the **directory**, a chain of sectors linked as above.

### The volume name

**FLEX**: NEWDISK reads the name as a file specification, so `$10-$17` hold up to 8 characters and `$18-$1A` an
"extension" of up to 3. DISKNAME (on SYS900) shows and changes only the 8 characters, and keeps `$18-$1A`. **Seen**:
`$18-$1A` holds `00 4D 44` (`\0MD`) on 9 samples and `FHL` on EFORTH, a tag left by the formatter, never a name. The
program shows and edits the 8-character name (`$10-$17`) and keeps `$18-$1A` as it is.

DISKNAME also writes today's date into the SIR (`$23-$25`) when it changes the name or number.

## The directory

Each directory sector: 16 bytes of header (the link, the record number, then zeros), then 10 entries of 24 bytes
(**FLEX**: the entry is the FCB's bytes 4-27, read and written whole):

| Bytes | |
| --- | --- |
| 0-7 | name, NUL-padded |
| 8-10 | extension, NUL-padded |
| 11 | attributes: `$80` write-protected, `$40` delete-protected, `$20` read-protected, `$10` catalogue-protected; bits 0-3 kept (**FLEX**, below) |
| 12 | reserved: 0 in a new entry, kept otherwise |
| 13-14 | first sector |
| 15-16 | last sector |
| 17-18 | number of sectors, a random file's map included |
| 19 | random-access flag: 0 sequential; FLEX writes 2 for a random file and tests only non-zero |
| 20 | reserved: 0 in a new entry, kept otherwise |
| 21-23 | date: month, day, year (two digits; years before 70 are read as 20xx) |

* A first byte of 0: a slot never used. A first byte with bit 7 set: a deleted file (**FLEX** writes `$FF` there and
  keeps the rest of the entry).
* **FLEX's search stops at the first never-used slot** (the FMS's name search, and CAT alike): anything after it is
  invisible to FLEX. **Seen**: SY22_411 has two files (`ANALYSIS.CMD`, `CT.CMD`) in its last directory sector after
  160 never-used slots. The program reads every slot of every directory sector, so it lists them.
* Names **seen**: upper-case letters, digits and `-`.

### Attributes (FLEX)

PROT sets `$80` (W), `$40` (D) and `$10` (C), and `X` clears all four, keeping bits 0-3. Nothing in FLEX sets `$20`,
but the FMS honours it. What the FMS refuses:

| Attribute | Refused |
| --- | --- |
| `$80` write-protected | delete (error 11), rename (11), writing a byte into a random file (11) |
| `$40` delete-protected or `$20` read-protected | delete (error 12), rename (12) |
| `$20` read-protected | open for read, when FLEX asks for the check (error 17) |
| `$10` catalogue-protected | nothing; CAT leaves the file out |

## Files

A file is the chain from its first sector to its last, exactly as many sectors as the entry says, the last one
linking to 0/0. Its data is every sector's 252 data bytes in chain order: FLEX keeps no byte count, so the size is
known only to a sector.

A **random-access** file starts with two map sectors, then its data. The program gives a random file's data without
the two map sectors: the map holds sector addresses, which mean nothing anywhere else, and a writer makes a new one.

**FLEX**, the map (`LDC48`-`LDC9D` in the FMS): a list of 3-byte runs (track, sector, count) from byte 4 of the first
map sector, covering the data sectors in order. A run grows while the next data sector is the one after the last
(sector + 1, or sector 1 of the next track after the last sector of a track) and its count is below 255. Runs go at
bytes 4, 7, ... 253 of a map sector (84 runs), then on in the second map sector; a file needing more than 168 runs
can't be written (error 23, "file sector map overflow"). Unused map bytes are zero.

**Text** files end lines with CR and keep runs of spaces as a TAB (`$09`) followed by the number of spaces; the last
sector is padded with NULs. Files are copied out as stored; the viewer expands them.

## How FLEX writes (FLEX)

Settled from the FMS (labels are FHL's, which are TSC FLEX 9.0's addresses). The program does the same.

**The free chain.** The SIR's first free, last free and count are the free list; the FMS keeps them in memory while
it works and writes them back into SIR `$1D-$22` (and nothing else of the SIR) after each change.

* **Taking a sector** (`LD79A`): always the first free sector. The new first free is its link; the count goes down by
  one; when its link is 0/0 the first free, last free and count all become 0. With no free sector the write fails
  (error 7, "all sectors in use").
* **Giving sectors back** (deleting a file, `LDBDB`): the file's whole chain, unchanged, is appended at the **end** of
  the free chain: the last free sector's link is set to the file's first sector, the last free becomes the file's last
  sector, and the count goes up by the file's sector count. On an empty free chain the file's chain becomes it.

**A new file** (open for write, `LD9DD`; close, `LDAC8`):

1. The name must not be in the directory (error 3, "file exists").
2. Its entry goes in the **first free slot the search meets**: the first deleted slot, or the first never-used slot,
   whichever comes first (the search stops at the never-used one).
3. If there is none, the **directory grows** by one sector (`LDA44`): a sector is taken from the free chain as above
   (cleared, record number 1, link 0/0), the last directory sector is linked to it, the SIR is written, and the entry
   goes in its first slot. **Seen**: SYS900's directory has grown twice this way (29/7 and 37/8, record number 1).
4. The entry is written at once: name, attributes 0, both reserved bytes 0, first / last sector 0/0, count 0, the
   random flag 0, and today's date (month, day, year).
5. Data sectors are taken from the free chain one at a time as the data fills them, each linked from the one before,
   numbered 1, 2, 3 ... For a random file the first two sectors taken are its map (record 0), then the data from 1.
6. On close the last sector is written, a random file's last map run is written, then the entry (first and last
   sector, count, random flag 2), then the SIR.
7. **A file with no data is never kept**: closing a file to which nothing was written marks its entry deleted
   (`$FF`). FLEX can't hold an empty file.

**Deleting a file** (`LDBDB`): refused for `$80` (error 11) and `$40` / `$20` (error 12). The chain is given back as
above, then the entry's first byte becomes `$FF` (the rest of the entry, and the file's sectors, are left as they
were), then the SIR is written.

**Renaming a file** (`LDB31`): the new name must not exist (error 3); refused for `$80` (11) and `$40` / `$20` (12).
The entry is rewritten in its own slot with the new name and extension (a new name given without an extension keeps
the old one); nothing else changes, not the date.

**Attributes** (PROT): the entry's byte 11 changes in place; nothing else.

**A new disc** (TSC NEWDISK; GIMIX FORMAT writes the SIR the same way):

* Every sector is formatted with its link set to the next logical sector: (t, s+1), then (t+1, 1) after the last
  sector of a track, and 0/0 after the last sector of the disc. Data and record numbers are zero.
* The SIR (0/3): link 0/0; the volume name; the number; first free 1/1; last free (last track, sectors per track);
  count (last track) x (sectors per track); today's date; the last track; sectors per track.
* The last sector of track 0 is set to link 0/0, so the directory is 0/5 to the end of track 0, empty.
* The boot loader goes into sector 1 (TSC's own code; the program writes none, so its new discs don't boot until
  FLEX's own tools put a loader there).

## Where the program chooses (series flex-2, owner, 15 Nov 2026)

FLEX leaves these to whoever writes the disc, or does something the program can't:

* **An empty host file is refused** ("FLEX can't hold an empty file"): FLEX itself never keeps one.
* **A file copied in keeps its date and its four protection bits**, as FLEX's COPY does (it copies byte 11 and the date
  into the new file's FCB); with no date, today's. A random-access file stays random-access, with a new map.
* **Rename takes the name given**, extension and all (FLEX's RENAME keeps the old extension when the new name has
  none; that is the command's, not the disc's). A write-, delete- or read-protected file is not renamed, deleted or
  replaced, as in the FMS; Properties takes the protection off first. The random-access flag can't be changed.
* **The volume name** is the 8 characters at `$10-$17`, stored as typed (printable ASCII; DISKNAME's rule); `$18-$1A`
  and the SIR's date are kept.
* **A new disc** is NEWDISK's layout with no boot loader: sector 1 is all zero (as on D_MAN_1 and DMDR_SC1), so the
  disc works under FLEX but doesn't start it. A loader can be copied from a FLEX disc the user has (sectors 1 and 2 as
  they are), and linked as LINK does (sector 1 bytes 5-6: the file's first sector). A copied loader is linked to the
  disc's `FLEX.SYS` if it has one, else left unlinked (0/0: TSC's FLEX 9 loader then says "DISK NOT LINKED").
* **The shapes of new discs** are those of the samples: 35 x 10, 40 x 10, 35 x 18, 40 x 20, 40 x 36 and 77 x 15.
* **Check is stricter than FLEX**: FLEX's search would not see files after a never-used slot, and FLEX would go on
  using a free chain whose count is wrong; the program lists the first and refuses to change a disc with the second.

## What Check reports

Errors (the disc is read-only, and a save is refused): a directory, file or free chain that leaves the disc or loops;
a file chain that doesn't reach its last sector in its count; a sector in two places (two files, a file and the free
chain, the directory and anything); a free chain that doesn't end at the SIR's last free sector or holds a different
number of sectors than the SIR says; a name twice. Warnings: a file's last sector linking on; a record number out of
order; a random file's map that doesn't list its chain; a date that isn't a date; sectors after track 0 that belong to
nothing (two healthy samples, ABASIC_1 and EFORTH, have them).

**Seen** faults on the samples: C009's free chain loops back to 70/8 after 467 of its 550 sectors; FLX503TX's
`SOLMON.TXT` never reaches its last sector (its first sector is record 1, the second record 1 again); FLEX2-01's free
chain runs past the SIR's last free sector 34/10 into the chain of a 44-sector file no entry names (23/6 to 27/9,
records 1-44), so it holds 61 sectors where the SIR says 17: a file's chain given back to the free chain (as deleting
does) without the SIR being written.

## Not FLEX

Recognised so that the message says what they are, and not opened: **OS-9** (an RBF identification sector in sector
0: the sector count, sectors per track, the allocation map's size matching the count, the root directory inside the
disc) and **UniFLEX** (the SIR in 512-byte block 1 naming the file system at byte 26 and the pack at byte 40). Both
**seen** among the samples (C022, C022_1; C012_1, C014).
