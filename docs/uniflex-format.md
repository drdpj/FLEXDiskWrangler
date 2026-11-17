# UniFLEX discs: the format

UniFLEX is TSC's multi-user, Unix-like operating system for the 6809 (SWTPC S/09, Gimix and others), from 1980. Its
discs have nothing in common with FLEX's but the maker: 512-byte blocks, a System Information Record (SIR) like a Unix
superblock, file descriptor nodes (fdns, Unix's inodes) and a tree of directories. This program reads them (series
`uniflex-1`) and changes them as UniFLEX's kernel does (series `uniflex-2`, "How UniFLEX writes" below).

## Where the facts come from

Everything here was read from TSC's own code and checked on real discs. Nothing of TSC's is in the program.

The discs, sources and manuals come from the people who have kept UniFLEX alive: the **FLEX UniFLEX User Group**
(Michael Evenson's Evenson Consulting Services, swtpcemu.com, with its archive of disc images and its wiki) and **The
Missing 6809 UniFLEX Archive** (first at rtmx.com, now mirrored at retro.co.za/6809/UniFLEX), which keeps TSC's
UniFLEX releases and sources for SWTPC and Gimix machines. This work would not have been possible without them.

- **The 6809 kernel's sources** on the sample `ufsources.dsk` (a Winchester image of a UniFLEX development system):
  `/structures/sir` and `/structures/fdn` (the record layouts, with TSC's names, used below), `/kernal/diskman`
  (`mapfil`: a file's block map; `aldb` / `filfr`: taking a block from the free list), `/kernal/fdnman` (reading and
  writing fdns, `filfdn`: refilling the SIR's list of free fdns), `/kernal/fileman` (`crtsd`: device files). The
  owner's archives `UniFLEX_Kernel.z` and `UniFLEX_Core.z` (`design/background-docs`) hold the same files, byte for
  byte. For writing (series `uniflex-2`): `diskman` (`aldb`, `filfr`, `frdb`, `mkfch` and its `SORT_FREE_LIST`,
  `rmvfil`, `frind`), `fdnman` (`alfdn`, `filfdn`, `frfdn`, `frefdn`, `updfdn`), `fileman` (`create`, `crfil`, `link`,
  `unlink`, `chown`, `chprm`), `pathnam` (`pthnm`: where a new entry goes), `rdwr` (`filwr`).
- **TSC's utilities** on `ufsources.dsk`: `/utils1/crdir` (1.05, 1984), `/utils2/copy`, `kill`, `rename`, `move`,
  `/utils1/perms`, `owner`: the system calls each change is made of, in their order.
- **TSC's disc formatter**, `unibug/format` (UniFLEX Disk Format Program, core version 1.09, 29 November 1984) with its
  8" floppy part `formatfd`, in the kernel source sets the owner has (`testdata/UniFLEX/UniFLEX_SWTP_*`, `_GMX_*`, tar
  archives; `uos-s3`, the configuration UniFLEX 3.14A reports, is the one followed): how a new disc is laid out
  (`calcs`, `mksir`, `mffd`, `mfbl`, `mkrfdn`, `mkrtf`, `mkfree`, `dofb`, `mkbtkf`).
- **UniFLEX running**: UniFLEX 3.14A (SWTPc UOS-S3) booted from a copy of the sample `fd0_nopassword` in MAME's
  `swtpc09d3`, changing copies of discs in its second drive (series `uniflex-2`; "How UniFLEX writes").
- **TSC's manuals** (`design/background-docs/`): *68xxx UniFLEX Programmer's Guide* (Sep 1986) for the mode and
  permission bits as `status` returns them; *68xxx UniFLEX System Manager's Guide* (Mar 1987), section 9, for the
  time ("seconds since January 1, 1980, at the zeroth meridian"). These describe the later 68000 version; where they
  differ from the 6809 discs (long names), the 6809 code wins.
- **The discs**: the owner's collection (`testdata/UniFLEX/*.dsk`, 37 images: 8" floppies of four shapes and 15 MB
  Winchesters) and `C012_1`, `C014` in `testdata/FLEXDisks`. Every statement below holds on the healthy ones; the
  damaged ones are listed at the end.

## The image

A raw file of 512-byte blocks: block n is at byte n x 512. (The emulators that made the samples store the blocks in
order.) The file holds the volume (blocks 0 to the SIR's
`ssizfr`) and, after it, the swap space if the SIR has one (`sswpbg`, `sswpsz` blocks). So the file is
`max(ssizfr + 1, sswpbg + sswpsz)` blocks: 2464 (8" double-sided double density), 2432, 1232, 616 or 31212
(Winchester) on the samples. A file shorter than that has lost blocks (`empty_CMI5619.dsk`); one a block short of its
swap space (`C012_1`) loses nothing of the file system.

**In ImageDisk, DSK and TeleDisk files** (series `containers`, 16 Nov 2026), from TSC's floppy driver (`dmf3drvr`,
`uos-s3`) and formatter (`formatfd`): an 8" floppy is one density throughout (the driver reads the SIR single density
and, failing that, double; `formatfd` formats every track at the type's density): 8 sectors of 512 a side single
density (FM), 16 double (MFM), at 500 kbit/s. Block n is sector `n mod S + 1` of track `n / S` (S the sectors a
side); on a double-sided disc the cylinder is the track / 2 and the side its low bit, side 1's sectors numbered on
from side 0's (9-16 or 17-32), or from 1 again on an OMTI disc. Either way the disc is the container's tracks
cylinder by cylinder, side 0 then side 1, each side's sectors in ID order: block n at byte n x 512 of that, as in a
raw file. The probe takes a container whose tracks are all 512-byte sectors of one shape; changes, Save As and new
discs are as for FLEX (`disc-format.md`, "In ImageDisk, DSK and TeleDisk files"): every track keeps its density, IDs
and order; new discs are the four 8" types with side 1 numbered on, and can be standard DSK too.

| Blocks | What |
| --- | --- |
| 0 | the boot block (`format`'s `mkbt` copies a bootstrap here) |
| 1 | the SIR |
| 2 to `sszfdn` + 1 | the fdn list: 8 fdns of 64 bytes to a block, fdn 1 first |
| `sszfdn` + 2 to `ssizfr` | the volume space: files, directories, indirect blocks, the free list |
| `sswpbg` on | swap space (none on some discs) |

## The SIR (block 1)

Big-endian, as everything on the disc. `structures/sir`:

| Offset | Size | TSC's name | What |
| --- | --- | --- | --- |
| 0 | 1 | `supdt` | update flag (in memory) |
| 1 | 1 | `swprot` | mounted read-only (in memory) |
| 2, 3 | 1, 1 | `slkfr`, `slkfdn` | locks (in memory) |
| 4 | 4 | `sintid` | initialising system's identifier |
| 8 | 4 | `scrtim` | when the disc was made |
| 12 | 4 | `sutime` | when it was last updated |
| 16 | 2 | `sszfdn` | the fdn list's size in blocks |
| 18 | 3 | `ssizfr` | the volume's last block number (`format`: "volume block count, dec by one") |
| 21 | 3 | `sfreec` | free blocks |
| 24 | 2 | `sfdnc` | free fdns |
| 26 | 14 | `sfname` | file system name, NUL-padded (often empty) |
| 40 | 14 | `spname` | pack (volume) name, NUL-padded (often empty) |
| 54 | 2 | `sfnumb` | file system number |
| 56 | 2 | `sflawc` | flawed block count |
| 58 | 1 | `sdenf` | density: 0 single |
| 59 | 1 | `ssidf` | sides: 0 single (`$11` on the 2432-block discs) |
| 60 | 3 | `sswpbg` | swap space's first block |
| 63 | 2 | `sswpsz` | swap space's size in blocks |
| 65 | 1 | `s64k` | swap size a multiple of 64K |
| 66 | 11 | `swinc` | Winchester configuration (`sdenf` = `$FF` on a Winchester) |
| 77 | 11 | `sspare` | |
| 88 | 1 | `snfdn` | how many free fdns are listed here (at most 50, `CFDN`) |
| 89 | 100 | `scfdn` | free fdn numbers, 2 bytes each |
| 189 | 1 | `snfree` | how many free blocks are listed here (at most 100, `CDBLKS`) |
| 190 | 300 | `sfree` | free block numbers, 3 bytes each |

## Fdns

Fdn n (from 1) is the ((n - 1) mod 8)th 64-byte slot of block 2 + (n - 1) / 8. On the disc it is the in-memory fdn
from `fmode` on (`fdnman`'s `fdnbf` copies `FDNSIZ - fmode` bytes), then the time (`updfdn`):

| Offset | Size | TSC's name | What |
| --- | --- | --- | --- |
| 0 | 1 | `fmode` | 0: a free fdn. Bits: `$01` busy (in use), `$02` block device, `$04` character device, `$08` directory. A file is `$01`, a directory `$09`, devices `$03` and `$05` |
| 1 | 1 | `facces` | `$01` owner read, `$02` owner write, `$04` owner execute, `$08` others read, `$10` others write, `$20` others execute, `$40` set user id on execution. (`format` gives `/.badblocks` `$80`: "very special permissions") |
| 2 | 1 | `fdirlc` | links: the directory entries that name it |
| 3 | 2 | `fouid` | owner's user id |
| 5 | 4 | `fsize` | size in bytes |
| 9 | 39 | `ffmap` | 13 block numbers of 3 bytes |
| 48 | 4 | | time of the last change |
| 52 | 12 | | unused (zero) |

A busy fdn is any whose mode byte isn't 0 (`filfdn` takes an fdn as free only when that byte is 0).

**The block map** (`diskman`'s `mapfil`): entries 0-9 are the file's blocks 0-9; entry 10 is a single indirect block
(its blocks 10-137), entry 11 a double indirect block (138-16521), entry 12 a triple. An indirect block holds 128 block
numbers of 3 bytes (bytes 0-383; the rest is unused). A zero entry is a block never written: it reads as 512 zeros
(`mapfil` returns "not allocated" when reading). A file's data is its blocks in order, cut at `fsize`.

**Device files** (`$03` block, `$05` character) have no blocks: `crtsd` stores the major device number in byte 9 and
the minor in byte 10. (On `C012_1`, UniFLEX 1.07 of 1982, some have the number in byte 11 instead.) Their size is 0.

**Times** are 4-byte counts of seconds since 1 January 1980, 00:00 GMT (System Manager's Guide). 0 is no time.

## Directories

A directory is a file of 16-byte entries: a 2-byte fdn number (0: an empty slot) and a name of up to 14 bytes,
NUL-padded (no NUL when it is 14 long). The first two are `.` (the directory itself) and `..` (its parent; the
root's is itself). Names are bytes: any but NUL and `/`, case kept; the samples have one with a newline in it
(`McCoshCandFortran.dsk`, `/<newline>dynacalc`). The 68000 version's longer names (bit 7 of the first name byte set,
continued in the next entries) are not 6809's.

The root is fdn 1. A file's link count is the number of entries that name it; a directory's is its entry in its
parent, its own `.` and the `..` of each directory in it (`format` makes the root with 2).

## Free space

**Free blocks** (`aldb`, `filfr`): the SIR lists up to 100. Taking one takes entry `[snfree - 1]` and lowers the count;
a zero entry means the disc is full. Entry 0 is taken last: when it is, its block's first 300 bytes (100 entries) become
the new list (`filfr`), and the block is used like any other. So the free list is a chain: the SIR's entries 1 to
`snfree - 1`, then entry 0, whose block holds the next 100 (its own entry 0 the next link, 0 at the end of the chain);
every block named, links included, is free. Bytes 300-511 of a list block are left over from earlier use. `sfreec`
counts them all.

**Free fdns**: the SIR lists up to 50 (`snfdn`, `scfdn`); when they run out `filfdn` reads the fdn list for more. `sfdnc`
counts every free fdn.

## A new disc (`format`)

TSC's formatter (`uos-s3/unibug/format` 1.09, 29 November 1984, with its 8" floppy part `formatfd`: together UniFLEX
3.14's `/etc/formatfd`) lays a disc out in `strd`:

- **The shapes** (`formatfd`'s table): 77 cylinders; 8 blocks of 512 a side single density, 16 double; one side or two.
  The SIR's `sdenf` and `ssidf` are the type's flags (0 or 1; `$11` for the "+" double-sided types).
- **Sizes** (`calcs`): the volume is cylinders x heads x sectors blocks, less any swap space asked for (whole
  cylinders, after the volume). The fdn list is a 33rd of the disc (`100/3`: "3%"), rounded down, unless a count is
  given: 74 blocks on a 2464-block disc, 37 on 1232, 18 on 616. Free blocks: the volume less the fdn blocks and 3 (the
  boot block, the SIR, the root's block). Free fdns: 8 a block, less the root and `/.badblocks`.
- **The SIR** (`mksir`): both times the time of formatting, the names (up to 14 bytes, to a NUL) and number typed, the
  counts, the flags, the swap space if any. `mffd`: fdns 3 up, at most 50. `mfbl`: `(free + 1) mod 100` blocks (100 if
  that is 0), the first free ones, from block `fdn blocks + 3`, stored last entry first so that they are taken in
  order; entry 0, the last of them, is the first list block.
- **The fdns** (`clrfdn`, `mkrfdn`): every fdn block cleared; fdn 1 the root (`$09`, `$3F`, 2 links, size 48, its block
  the first after the fdns), fdn 2 `/.badblocks` (`$01`, `$80`, 1 link: the bad blocks found, none on a good disc),
  both timed. `mkrtf`: the root's block holds `.`, `..` and `.badblocks`.
- **The free lists** (`mkfree`, `dofb`): each list block holds the next 99 blocks, entry 99 first, and in entry 0 the
  block after them, the next list block; the last list stops at the end of the volume (its other entries and its link
  zero).
- **Block 0** (`mkbt`): TSC's bootstrap. The program leaves it empty: it carries no TSC code.

The samples `dsdd8inch` and `dssd8inch` are this byte for byte (block 0 and the SIR's update time aside), and so is
every new disc of the program (`tests/uniflex_check.py`, `format_disc`). **Checked against UniFLEX running:** UniFLEX
3.14A's own `/etc/formatfd +n +m=FD-DD` in MAME made a disc identical to the program's `uniflex-ds-dd` disc of the same
time, byte for byte, but for block 0 (the bootstrap).

## How UniFLEX writes

Read from the 6809 kernel (`kernal/diskman`, `fdnman`, `fileman`, `pathnam`, `rdwr`) and TSC's utilities on
`ufsources.dsk` (`/utils1/crdir`, `/utils2/copy`, `kill`, `rename`, `move`, `/utils1/perms`, `owner`), then **checked
against UniFLEX 3.14A running** (MAME's `swtpc09d3`, the workspace's `design/test-scripts/mame-uniflex`): the model in
`tests/uniflex_check.py` (class `Kernel`), replaying the session's commands on the same disc, gives UniFLEX's result
byte for byte but for the times.

**Taking a block** (`aldb`): the SIR's count is lowered and entry `[count]` taken; a zero entry means the disc is full.
When the entry taken is entry 0 (the count now 0), `filfr` first copies that block's first 300 bytes into the SIR's
list and sets the count to 100; the link block is then used like any other. Every block taken is cleared (`clrbfr`), so
the end of a file's last block is zeros. `sfreec` is lowered.

**Freeing a block** (`frdb`): with the count 0, entry 0 is set to 0 (the end of the chain) and the count to 1; with the
count 100, `mkfch` first sorts the SIR's entries 1-99, largest first (in the SIR itself: `SORT_FREE_LIST`), copies the
whole list (entry 0 unmoved) into the first 300 bytes of the freed block (the other 212 are whatever the kernel's
buffer held: the program leaves them as they were) and sets the count to 0. The block is then entry `[count]` and the
count raised; `sfreec` too. So a run of blocks freed is taken again last first.

**A file's blocks** (`mapfil` writing): block by block in order. A missing map slot or indirect block is taken when
first needed, before the block under it: for block 10, the single indirect block, then the data block; for block 138,
the double indirect block, the single indirect under it, then the data block. The size becomes the end of the last
byte written (`filwr`).

**Freeing a file's blocks** (`rmvfil`): the triple indirect tree, then the double, then the single (`frind`: each
indirect block's entries from 127 down to 0, each freed or its tree freed, then the indirect block itself), then the
direct blocks from 9 down to 0. The size and map become zero.

**Taking an fdn** (`alfdn`): when the SIR's list is empty, `filfdn` refills it with the free fdns (mode byte 0) from fdn
1 up, at most 50, skipping fdn blocks that `/.badblocks` names. The last entry is taken: the fdn is cleared and marked
busy, `sfdnc` lowered. `crfil` then sets the mode and access (masked by the user's default permissions), 1 link and the
user as owner.

**Freeing an fdn** (when the last link and the last use go: `frefdn`): `rmvfil`, then the mode byte cleared and `frfdn`:
`sfdnc` raised, and the fdn appended to the SIR's list if it holds fewer than 50. The fdn keeps its access, owner and
(zero) link count; its size and map are zero.

**Times**: an fdn is written with the system time at 48 whenever it is written (`updfdn`), so every fdn a change touches
(the file, the directories whose entries change, an fdn taken or freed) gets the time of the change. The SIR is written
(`update`) with the time at `sutime` and its in-memory bytes 0-3 zero. The program uses the time of the change for both.

**Directory entries**: a new entry (`crfil`, `link`) goes into the first slot whose fdn is 0 (`pthnm` notes it while
looking for the name), or after the last entry, growing the directory (a new block taken as above). A name is cut to
14 bytes. Removing an entry (`unlink`) writes only its two fdn bytes as zero: the name stays in the slot, and the
directory never shrinks.

**Links**: `link` adds an entry and raises the link count; `unlink` lowers it (a count of 127 is never changed). The
count reaching 0 frees the fdn as above.

**The utilities**:

- `crdir`: `crtsd` makes the directory (mode `$09`, access `$3F` masked, 1 link, its entry in the parent), `chown` to
  the user, then `link` of the directory as `dir/.` (its first block taken; 2 links) and of the parent as `dir/..` (the
  parent's links raised). A new directory is 32 bytes.
- `copy` (and the program bringing a file in): `create`: a new file is `crfil` with the source's access; a name that
  exists is reused, its blocks freed (`rmvfil`) and its fdn, access and owner kept. The data is then written in order.
  The time is the time of the copy: `copy` keeps nothing of the source but its access.
- `kill`: `unlink`. A directory (`kill +d`) only when it holds nothing but `.` and `..`: `dir/.` unlinked, then
  `dir/..` (the parent's links lowered), then the directory.
- `rename` and `move` within a disc: `link` to the new name, then `unlink` the old; a file of the new name is unlinked
  first. **Neither renames nor moves a directory.**
- `perms`: the seven access bits (`$80`, "execute only", kept). `owner`: the owner's user id.

**Checked against UniFLEX running** (UniFLEX 3.14A in MAME, the workspace's `design/test-scripts/mame-uniflex`;
16 Nov 2026): `crdir`, `copy` (small files, one reaching its single indirect block, one its double, one over a file
that exists), `perms`, `owner`, `rename`, `kill`, `kill +d`, a directory growing past a block, on a sample disc and on
one made by `uniflex_check.py`'s maker; the model in `uniflex_check.py`, replaying the same commands on the same disc,
gives UniFLEX's result byte for byte, but for the times (UniFLEX's clock is the emulator's) and bytes 300-511 of the
list blocks `mkfch` writes (below). The program gives the model's result byte for byte (`uniflex_check.py writes`).

## Where the program chooses

What UniFLEX's code doesn't settle, or settles in a way a disc image can't follow:

- **The tail of a list block.** `mkfch` writes a whole buffer, of which UniFLEX sets only the first 300 bytes; the
  other 212 are whatever the kernel's buffer last held (seen: other blocks' data, fdns, a directory of the system
  disc). The program leaves them as the block held them. Nothing reads them.
- **The time** of every fdn written and of the SIR is the time of the change, as UniFLEX gives it; a file brought in
  doesn't keep its own (UniFLEX's `copy` keeps only the access).
- **A file brought in** keeps the access bits and owner it brings (retro-files family `uniflex`); one that brings none
  gets owner 0 (the system manager, as whoever runs `copy` as root) and `rw-rw-`, the access UniFLEX's shell gives a
  file it creates (seen in UniFLEX 3.14A).
  One brought over a file of the same name is that file emptied and written again, its fdn, access and owner kept
  (UniFLEX's `create`).
- **A change that doesn't fit** (the disc full, no free fdn) is refused and undone whole; UniFLEX would leave what it
  had written.
- **Directories** (owner, 16 Nov 2026: "we want to be compliant with the disk formats, but we want people to be able
  to do things that are useful when arranging files on disks"). UniFLEX's `rename` and `move` refuse directories; the
  kernel lets the system manager `link` and `unlink` one. The program renames a directory within its own directory
  (link, then unlink: its `.` and `..` stay right) and moves one to another directory with the same calls a system
  manager would use to do it by hand: `link` to the new name, `unlink` the old, `unlink` its `..` (the old parent's
  links lowered), `link` the new parent as its `..` (in the slot just emptied; the new parent's links raised). Never
  into itself or a directory inside it (the program follows the `..`s up from the destination). Deleting a directory deletes
  what is in it first, depth first in the directory's order, each as `kill` does, then the directory as `kill +d`
  does.
- **A drag within the window moves** (the window's choice, not UniFLEX's): each item is renamed into the folder dropped
  on as above, all in one change. A name the folder already has is refused, though UniFLEX's `rename` and the program's
  Rename replace a file: a drag shouldn't remove one without asking.
- **`/.badblocks`** can't be deleted or renamed (UniFLEX's `unlink` refuses fdn 2 with its `$80`).
- **Renaming onto a name that exists** is refused (the family's rule: the core asks for the name to be free), where
  UniFLEX's `rename` would unlink that file first.
- **The SIR's names and number** are written as they are typed, as `format` writes them; no UniFLEX utility changes
  them on a made disc.
- **A new disc** has no swap space and no bootstrap (above).

## The samples

Fourteen of the 39 are healthy, the check finding nothing: `Copy of emptydisk`, `Copy of fd0`, `dsdd8inch`,
`dssd8inch`, `fd0_nopassword`, `McCoshCandFortran` (one warning: the name with a newline), `Oldfd0`, `pw03`, `UNIFLEX`,
`UnixUtilsforUniFLEX`, `w_boot`, `wd0`, `wd1`, `wd2`. `tests/uniflex_samples.txt` lists every sample with what the
check says of it; the tests hold the program to it.

The damage, all of it real (each confirmed by hand from the bytes):

- **A SIR that doesn't match the disc**, as when an image is copied from a running system: `emptydisk`, `fd1`-`fd3`,
  `copymaster` and the discs made from them (`account`, `clock`, `dmp`, `dynamite`, `init`, `spooler`, `pascal`): the
  free list's chain reaches block 766, which holds code, so most free blocks can't be found; the root's link count is
  one more than its entries; the free fdn count one less than the fdns that are free. `pascal` also names a free fdn
  (`/core`) and lists a block twice in the SIR's free list.
- **Free lists**: `INITIALIZERII` names a block twice; `ufsources` lists 11 blocks of `/kernal/core` as free (its SIR
  counts 9910 free, the list holds 11).
- **Free fdns**: `fd0`, `C012_1` (UniFLEX 1.07), `floppydump`, `floppydump2` count them wrongly; the last two list fdn 194
  as free in the SIR, while it is in use (by no directory: a file being made when the image was taken).
- **Links**: `extra_1` and `extra_2` have files named in two or four places whose link count is 1.
- **Never finished**: `ssdd8inch`, `sssd8inch` and `fdnewtest`: the root directory's block and the free lists were
  never written (all zeros), though the SIR and the fdns were.
- **Overwritten**: `C014`'s `/randy` and `/modem` directories hold other files' data (found in series `flex-1`).
- **Cut short**: `empty_CMI5619` holds 26112 of its 31212 blocks.
