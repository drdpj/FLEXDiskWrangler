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
// Series flex-1 only reads. Nothing here writes yet.
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

// The attribute bits (directory byte 11).
enum Attr : quint8 { WriteProtect = 0x80, DeleteProtect = 0x40, ReadProtect = 0x20, CatalogProtect = 0x10 };

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

private:
    bool readDirectory(QString *err);
    dw::device::Linear m_disc;
    Sir m_sir;
    QVector<File> m_files;
    QVector<TS> m_dirSectors;
    int m_deleted = 0, m_freeSlots = 0;
};

} // namespace Flex
