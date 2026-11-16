# retro-files: the `flex` namespace

FLEX Disk Wrangler's attributes in the retro-files drag format (DiskWranglerCore's `docs/retro-files-format.md`).
This section moves into that document with the next release of DiskWranglerCore (decided 15 Nov 2026: the core is used
as it is for now).

### `flex` - FLEX file attributes (produced by FLEX Disk Wrangler)

| Key | Type | Meaning |
| --- | --- | --- |
| `writeProtect` | boolean | FLEX's write protection (directory attribute `$80`) |
| `deleteProtect` | boolean | delete protection (`$40`) |
| `readProtect` | boolean | read protection (`$20`) |
| `catalogProtect` | boolean | catalogue protection (`$10`): CAT doesn't list the file |
| `random` | boolean | a random-access file. The data is the file's data sectors without FLEX's two sector-map sectors; a receiver that writes FLEX discs makes a new map (FLEX Disk Wrangler does, from the sectors the file gets, as FLEX does) |

The item's date is the FLEX date (day precision, at midnight). FLEX Disk Wrangler, receiving an item, gives the new
file the item's date and the four protection keys above (as FLEX's COPY carries them), today's date when the item has
none. The data is the file as stored on the disc: the data bytes of each of its sectors in chain order. A FLEX sector
is 256 bytes, of which the first 4 are the link to the next sector and the record number, so each sector carries 252
bytes of data and the length is always a multiple of 252. A text file keeps FLEX's CR line ends, TAB + count space
compression and NUL padding.
