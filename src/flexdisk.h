/*
 * FLEX Disk Wrangler - a tool for FLEX disc images
 * Copyright (C) 2026 Daniel Jameson
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
// flexdisk.h - the FLEX filesystem (TSC FLEX 2 for the 6800, FLEX 9 for the 6809): the System Information Record,
// the directory, the files' sector chains and the free chain, read from a disc seen as one run of 256-byte sectors
// (a dw::device::Linear). QtCore only; no window, no core interface (flexvolume.h adapts it). docs/disc-format.md
// has the layout and how sure we are of each part.
//
// A sector is addressed as (track, sector), sectors numbered from 1; logical sector (t, s) is at byte
// (t * sectorsPerTrack + s - 1) * 256 of the disc. Every sector of a file, the directory and the free chain starts
// with a link to the next (0, 0 at the end) and a 16-bit record number; the other 252 bytes are data.
//
// Writing (series flex-2) does what FLEX's own file management system does, step for step: where a new entry goes,
// how the directory grows, which sectors a file takes, what a deleted file leaves, what the SIR holds after each
// change; and makeBlank lays a new disc out as NEWDISK does. docs/disc-format.md ("How FLEX writes") has each rule and
// the FMS routine it comes from. Every change is staged in the container, refuses what FLEX refuses, and ends by
// reading the disc again (so reload() gives exactly the state a change leaves). No change keeps a rollback of its own:
// the caller's Disc::transact puts the container back if one fails half way.
#pragma once

#include <QByteArray>
#include <QDate>
#include <QString>
#include <QVector>

#include <diskwrangler/device/linear.h>
#include <diskwrangler/fs/volume.h>

namespace Flex {

const int SectorSize = 256;
const int DataBytes = 252;                   // a sector's bytes after the link and the record number
const int SirSector = 3;                     // on track 0
const int FirstDirSector = 5;                // on track 0
const int EntriesPerDirSector = 10;
const int EntrySize = 24;

struct TS {
    int t = 0, s = 0;
    TS() = default;
    TS(int track, int sector) : t(track), s(sector) {}
    bool isNull() const { return t == 0 && s == 0; }
    bool operator==(const TS &o) const { return t == o.t && s == o.s; }
    bool operator!=(const TS &o) const { return !(*this == o); }
    QString toString() const;                // "12/5"
};

// The System Information Record: track 0, sector 3, bytes $10-$27.
struct Sir {
    QByteArray label;                        // 11 bytes as stored ($10-$1A)
    int volumeNumber = 0;                    // $1B-$1C
    TS firstFree, lastFree;                  // $1D-$20
    int freeCount = 0;                       // $21-$22
    int month = 0, day = 0, year = 0;        // $23-$25, the date the disc was made
    int lastTrack = 0, sectors = 0;          // $26 the highest track number, $27 sectors per track
    QString labelText() const;               // up to the first NUL, trailing spaces taken off
    QDate date() const;                      // invalid if the bytes aren't a date
    static Sir parse(const QByteArray &sector);
};

// What the SIR of a disc of `bytes` bytes says, without a disc: whether its geometry could be FLEX's and fits.
// why: the reason it can't. exact: the geometry gives exactly `bytes` bytes.
bool sirPlausible(const Sir &s, qint64 bytes, QString *why, bool *exact);

// The attribute bits (directory byte 11). FLEX's PROT changes only these four and keeps bits 0-3.
enum Attr : quint8 { WriteProtect = 0x80, DeleteProtect = 0x40, ReadProtect = 0x20, CatalogProtect = 0x10 };
const quint8 ProtectionBits = 0xF0;

const int MapRunsPerSector = 84;             // a random file's map: 3-byte runs at bytes 4, 7 ... 253
const int MaxMapRuns = 2 * MapRunsPerSector; // FLEX's error 23 ("file sector map overflow") beyond these
const int VolumeNameLength = 8;              // SIR $10-$17; $18-$1A (NEWDISK's "extension", a formatter's tag) is kept
const int BootLinkOffset = 5;                // FLEX's LINK writes the system file's first sector at sector 1, bytes 5-6

// A FLEX date as stored (M, D, Y; the year is the last two digits). Years before 70 are taken as 20xx.
QDate flexDate(int month, int day, int year);

struct File {
    int slot = -1;                           // directory slot: (directory sector in chain order) * 10 + entry
    QByteArray rawName;                      // the 8 name bytes and 3 extension bytes as stored
    QString name;                            // "NAME.EXT" ("NAME" without an extension)
    quint8 attr = 0;
    quint8 reserved12 = 0, reserved20 = 0;   // kept as they are
    TS start, end;
    int sectors = 0;                         // the directory's count, the random file's map sectors included
    quint8 random = 0;                       // byte 19: non-zero for a random-access file (FLEX writes 2)
    int month = 0, day = 0, year = 0;
    QString problem;                         // the chain is broken: what is wrong (the file can't be read)
    bool isRandom() const { return random != 0; }
    int mapSectors() const { return isRandom() ? 2 : 0; }
    int dataSectors() const { return qMax(0, sectors - mapSectors()); }
    qint64 size() const { return qint64(dataSectors()) * DataBytes; }
    QDate date() const { return flexDate(month, day, year); }
};

class Disk {
public:
    // Reads the disc. Fails if the SIR's geometry doesn't fit the disc or the directory can't be followed at all;
    // a disc with faults that can still be listed attaches, and check() says what is wrong.
    bool attach(const dw::device::Linear &disc, QString *err);
    bool reload(QString *err);
    bool isAttached() const { return !m_disc.isNull(); }
    const dw::device::Linear &linear() const { return m_disc; }

    const Sir &sir() const { return m_sir; }
    int tracks() const { return m_sir.lastTrack + 1; }
    int sectorsPerTrack() const { return m_sir.sectors; }
    bool inRange(const TS &ts) const { return ts.t >= 0 && ts.t <= m_sir.lastTrack && ts.s >= 1 && ts.s <= m_sir.sectors; }

    const QVector<File> &files() const { return m_files; }
    int indexOf(const QString &name) const;          // by name, ignoring case; -1 if none
    QVector<TS> dirSectors() const { return m_dirSectors; }
    int deletedEntries() const { return m_deleted; }
    int freeSlots() const { return m_freeSlots; }    // deleted and never-used directory slots

    bool readSector(const TS &ts, QByteArray *out, QString *err) const;
    // The file's data: every sector's 252 data bytes, in chain order, without a random file's two map sectors.
    // Fails for a file whose chain is broken (File::problem).
    bool readFile(const File &f, QByteArray *out, QString *err) const;
    // The sectors of a file's chain from its start to its end; false (why) if it leaves the disc, loops, or doesn't
    // reach the end in the directory's count.
    bool chain(const File &f, QVector<TS> *out, QString *why) const;

    dw::fs::Report check() const;

    // ----- writing (series flex-2). Names are given as stored ("NAME.EXT", upper case); the caller checks them
    // against the name rules. Each refuses with FLEX's reason where FLEX refuses.

    // A new file (FMS open for write, the data, close): its entry in the first deleted or never-used directory slot
    // (the directory grown by a sector when there is none), its sectors taken from the head of the free chain,
    // numbered 1, 2 ... (a random file's two map sectors first, numbered 0, its map built from the sectors it got).
    // attr: the protection bits; date: the entry's (an invalid date stores 0-0-0). Empty data is refused: FLEX can't
    // hold an empty file. A name already in the directory is refused.
    bool writeFile(const QString &name, const QByteArray &data, quint8 attr, bool random, const QDate &date, QString *err);
    // FMS delete: refused for a write-protected file (error 11) and a delete- or read-protected one (error 12); the
    // file's chain, unchanged, appended to the end of the free chain, then its entry's first byte set to $FF.
    bool deleteFile(const QString &name, QString *err);
    // FMS rename: the new name must not exist; refused as delete is. The entry keeps its slot and everything else.
    bool renameFile(const QString &from, const QString &to, QString *err);
    // PROT: the four protection bits of the entry (bits 0-3 kept).
    bool setProtection(const QString &name, quint8 attr, QString *err);
    // The SIR's volume name ($10-$17, up to 8 characters, NUL-padded; $18-$1A kept) and number ($1B-$1C).
    bool setVolumeName(const QByteArray &name, QString *err);
    bool setVolumeNumber(int number, QString *err);

    // ----- the boot loader (track 0, sectors 1 and 2). FLEX's own NEWDISK puts TSC's loader in sector 1; this program
    // writes none, but copies one from a disc the user has.
    bool bootSectors(QByteArray *out, QString *err) const;        // sectors 1 and 2, 512 bytes
    bool hasLoader() const;                                       // sector 1 holds anything
    TS bootLink() const;                                          // sector 1 bytes 5-6: what LINK pointed it at
    // Sectors 1 and 2 from another disc (512 bytes), as they are, then linked as linkTo says (0/0: not linked).
    bool setBootSectors(const QByteArray &sectors, const TS &linkTo, QString *err);
    // LINK: the loader pointed at a file's first sector.
    bool setBootLink(const TS &to, QString *err);

    // A blank disc laid out as NEWDISK lays one out, on a disc of exactly tracks * sectors * 256 bytes: every sector
    // linked to the next, the SIR, the directory from 0/5 to the end of track 0, the free chain from 1/1 to the end.
    // Sector 1 (the boot loader's) stays empty.
    static bool makeBlank(dw::device::Linear *disc, int tracks, int sectors, const QByteArray &name, int number,
                          const QDate &date, QString *err);

private:
    bool readDirectory(QString *err);
    qint64 offsetOf(const TS &ts) const { return (qint64(ts.t) * m_sir.sectors + ts.s - 1) * SectorSize; }
    bool writeSector(const TS &ts, const QByteArray &data, QString *err);
    bool writeSirFree(QString *err);                              // $1D-$22 from m_sir, the rest of the SIR kept
    bool takeSector(TS *out, QString *err);                       // FMS LD79A: the first free sector
    bool entrySlot(const File &f, TS *sector, int *index) const;
    bool writeEntry(const TS &sector, int index, const QByteArray &entry, QString *err);
    const File *protectedFile(const QString &name, QString *err) const;   // the file, or null and why (delete / rename)
    dw::device::Linear m_disc;
    Sir m_sir;
    QVector<File> m_files;
    QVector<TS> m_dirSectors;
    int m_deleted = 0, m_freeSlots = 0;
};

} // namespace Flex
