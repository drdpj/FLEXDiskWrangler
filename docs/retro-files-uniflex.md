# retro-files: the `uniflex` namespace

FLEX Disk Wrangler's UniFLEX attributes in the retro-files drag format (DiskWranglerCore's
`docs/retro-files-format.md`). Like the `flex` section (`retro-files-flex.md`), it moves into that document with the
next release of DiskWranglerCore.

### `uniflex` - UniFLEX file attributes (produced by FLEX Disk Wrangler)

Every key is on files and directories alike. The bits are an fdn's access byte (`docs/uniflex-format.md`).

| Key | Type | Meaning |
| --- | --- | --- |
| `ownerRead` | boolean | the owner may read it (`$01`) |
| `ownerWrite` | boolean | the owner may write it (`$02`) |
| `ownerExecute` | boolean | the owner may run it, or search it if a directory (`$04`) |
| `othersRead` | boolean | everyone else may read it (`$08`) |
| `othersWrite` | boolean | everyone else may write it (`$10`) |
| `othersExecute` | boolean | everyone else may run it (`$20`) |
| `setUserId` | boolean | it runs with its owner's user id (`$40`) |
| `owner` | number | the owner's user id (0-65535) |

The item's date is the fdn's time of last change, to the second (UniFLEX keeps seconds since 1980 in GMT). The data
is the file's bytes, exactly its size; blocks UniFLEX never wrote come out as zeros. Text files keep UniFLEX's CR line
ends and their TABs. A directory travels as a folder with its contents. A device file travels as an empty file (its
device numbers don't travel); the program's listing shows them.

**Received** (series `uniflex-2`): a file brought onto a UniFLEX disc gets the access bits and owner the item brings
(keys left out: owner 0 and `rw-rw-`, as UniFLEX's shell makes a file; as `docs/uniflex-format.md` "Where the program chooses" says); its date is
not kept: UniFLEX gives a file the time it is written. A folder becomes a directory.
