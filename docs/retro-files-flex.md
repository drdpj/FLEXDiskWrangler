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
| `random` | boolean | a random-access file. The data is the file's data sectors without FLEX's two sector-map sectors; a receiver that writes FLEX discs makes a new map |

The item's date is the FLEX date (day precision, at midnight). The data is the file as stored on the disc, a whole
number of 252-byte sectors: a text file keeps FLEX's CR line ends, TAB + count space compression and NUL padding.
