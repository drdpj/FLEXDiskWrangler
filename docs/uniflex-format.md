# UniFLEX discs: the format

UniFLEX is TSC's multi-user, Unix-like operating system for the 6809 (SWTPC S/09, Gimix and others), from 1980. Its
discs have nothing in common with FLEX's but the maker: 512-byte blocks, a System Information Record (SIR) like a Unix
superblock, file descriptor nodes (fdns, Unix's inodes) and a tree of directories. This program reads them (series
`uniflex-1`); it doesn't write them yet.

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
  byte.
- **TSC's disc formatter**, `unibug/format` (UniFLEX Disk Format Program, core version 1.09, 29 November 1984), in the
  kernel source sets the owner has (`testdata/UniFLEX/UniFLEX_SWTP_*`, `_GMX_*`, tar archives): how a new disc is laid
  out (`mksir`, `mkrfdn`, `mkrtf`, `mkfree`, `mkbtkf`).
- **TSC's manuals** (`design/background-docs/`): *68xxx UniFLEX Programmer's Guide* (Sep 1986) for the mode and
  permission bits as `status` returns them; *68xxx UniFLEX System Manager's Guide* (Mar 1987), section 9, for the
  time ("seconds since January 1, 1980, at the zeroth meridian"). These describe the later 68000 version; where they
  differ from the 6809 discs (long names), the 6809 code wins.
- **The discs**: the owner's collection (`testdata/UniFLEX/*.dsk`, 37 images: 8" floppies of four shapes and 15 MB
  Winchesters) and `C012_1`, `C014` in `testdata/FLEXDisks`. Every statement below holds on the healthy ones; the
  damaged ones are listed at the end.

## The image

A raw file of 512-byte blocks: block n is at byte n x 512. (The emulators that made the samples store the blocks in
order; an 8" disc's single-density track 0 is not visible in them.) The file holds the volume (blocks 0 to the SIR's
`ssizfr`) and, after it, the swap space if the SIR has one (`sswpbg`, `sswpsz` blocks). So the file is
`max(ssizfr + 1, sswpbg + sswpsz)` blocks: 2464 (8" double-sided double density), 2432, 1232, 616 or 31212
(Winchester) on the samples. A file shorter than that has lost blocks (`empty_CMI5619.dsk`); one a block short of its
swap space (`C012_1`) loses nothing of the file system.

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

`mksir`: the SIR as above, both times the time of formatting, the names and number typed. `mkrfdn`: fdn 1 the root
(`$09`, `$3F`, 2 links, size 48), fdn 2 `/.badblocks` (`$01`, `$80`, 1 link: the bad blocks found, so empty on a good
disc), both timed. `mkrtf`: the root's one block, the first after the fdn list: `.`, `..` and `.badblocks`. `mfbl` /
`mkfree`: the free blocks from the next one up, the first `(free + 1) mod 100` (100 if that is 0) in the SIR, the
rest in lists of 99 and a link, the last list ending in a zero link. The SIR's free fdn list starts at fdn 3.

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
