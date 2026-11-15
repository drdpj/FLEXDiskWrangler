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
#include "flexdisk.h"

#include <QHash>
#include <QSet>

namespace Flex {

static int u16(const QByteArray &b, int off) { return int(uchar(b[off])) << 8 | int(uchar(b[off + 1])); }
static TS tsAt(const QByteArray &b, int off) { return TS(uchar(b[off]), uchar(b[off + 1])); }
static uint keyOf(const TS &ts) { return uint(ts.t) << 8 | uint(ts.s); }

QString TS::toString() const { return QString("%1/%2").arg(t).arg(s); }

QString Sir::labelText() const {
    QByteArray b = label;
    const int nul = b.indexOf('\0');
    if (nul >= 0) b.truncate(nul);
    return QString::fromLatin1(b).trimmed();
}

QDate flexDate(int month, int day, int year) {
    if (month < 1 || month > 12 || day < 1 || day > 31 || year > 99) return QDate();
    return QDate(year < 70 ? 2000 + year : 1900 + year, month, day);    // invalid if the day isn't in the month
}

QDate Sir::date() const { return flexDate(month, day, year); }

Sir Sir::parse(const QByteArray &s) {
    Sir r;
    r.label = s.mid(0x10, 11);
    r.volumeNumber = u16(s, 0x1B);
    r.firstFree = tsAt(s, 0x1D);
    r.lastFree = tsAt(s, 0x1F);
    r.freeCount = u16(s, 0x21);
    r.month = uchar(s[0x23]);
    r.day = uchar(s[0x24]);
    r.year = uchar(s[0x25]);
    r.lastTrack = uchar(s[0x26]);
    r.sectors = uchar(s[0x27]);
    return r;
}

bool sirPlausible(const Sir &s, qint64 bytes, QString *why, bool *exact) {
    *exact = false;
    if (s.sectors < FirstDirSector) { *why = QString("the SIR gives %1 sectors per track (the directory starts at sector 5)").arg(s.sectors); return false; }
    if (s.lastTrack < 1) { *why = "the SIR gives no tracks after track 0"; return false; }
    const qint64 size = qint64(s.lastTrack + 1) * s.sectors * SectorSize;
    *exact = size == bytes;
    if (size > bytes + qint64(s.sectors) * SectorSize) {
        *why = QString("the SIR describes %1 tracks of %2 sectors (%3 bytes), more than the disc holds (%4)")
                   .arg(s.lastTrack + 1).arg(s.sectors).arg(size).arg(bytes);
        return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ reading

bool Disk::readSector(const TS &ts, QByteArray *out, QString *err) const {
    if (!inRange(ts)) { *err = QString("sector %1 is not on the disc").arg(ts.toString()); return false; }
    return m_disc.read((qint64(ts.t) * m_sir.sectors + ts.s - 1) * SectorSize, SectorSize, out, err);
}

bool Disk::attach(const dw::device::Linear &disc, QString *err) {
    m_disc = disc;
    if (!reload(err)) { m_disc = dw::device::Linear(); return false; }
    return true;
}

bool Disk::reload(QString *err) {
    m_files.clear();
    m_dirSectors.clear();
    m_deleted = m_freeSlots = 0;
    QByteArray s;
    if (!m_disc.read(qint64(SirSector - 1) * SectorSize, SectorSize, &s, err)) return false;
    m_sir = Sir::parse(s);
    bool exact = false;
    if (!sirPlausible(m_sir, m_disc.size(), err, &exact)) return false;
    return readDirectory(err);
}

static QString entryName(const QByteArray &e) {
    QByteArray n = e.left(8), x = e.mid(8, 3);
    int z = n.indexOf('\0');
    if (z >= 0) n.truncate(z);
    z = x.indexOf('\0');
    if (z >= 0) x.truncate(z);
    return x.isEmpty() ? QString::fromLatin1(n) : QString::fromLatin1(n) + "." + QString::fromLatin1(x);
}

// The directory: linked sectors from track 0 sector 5. Every slot is looked at: one whose first byte is 0 was never
// used, one with bit 7 set was deleted (FLEX writes $FF there); the rest are files. (A disc in the samples,
// SY22_411, has files after a never-used slot, so the listing doesn't stop at the first one.)
bool Disk::readDirectory(QString *err) {
    TS ts(0, FirstDirSector);
    QSet<uint> seen;
    while (!ts.isNull()) {
        if (!inRange(ts)) {
            if (m_dirSectors.isEmpty()) { *err = QString("the directory's first sector %1 is not on the disc").arg(ts.toString()); return false; }
            break;                                           // check() reports it
        }
        if (seen.contains(keyOf(ts))) break;                 // a loop: check() reports it
        seen.insert(keyOf(ts));
        QByteArray b;
        if (!readSector(ts, &b, err)) return false;
        const int index = m_dirSectors.size();
        m_dirSectors << ts;
        for (int i = 0; i < EntriesPerDirSector; ++i) {
            const QByteArray e = b.mid(16 + i * EntrySize, EntrySize);
            const quint8 first = uchar(e[0]);
            if (first == 0) { m_freeSlots++; continue; }
            if (first & 0x80) { m_deleted++; m_freeSlots++; continue; }
            File f;
            f.slot = index * EntriesPerDirSector + i;
            f.rawName = e.left(11);
            f.name = entryName(e);
            f.attr = uchar(e[11]);
            f.reserved12 = uchar(e[12]);
            f.start = tsAt(e, 13);
            f.end = tsAt(e, 15);
            f.sectors = u16(e, 17);
            f.random = uchar(e[19]);
            f.reserved20 = uchar(e[20]);
            f.month = uchar(e[21]);
            f.day = uchar(e[22]);
            f.year = uchar(e[23]);
            QVector<TS> c;
            QString why;
            if (!chain(f, &c, &why)) f.problem = why;
            m_files << f;
        }
        ts = tsAt(b, 0);
    }
    return true;
}

int Disk::indexOf(const QString &name) const {
    for (int i = 0; i < m_files.size(); ++i)
        if (m_files[i].name.compare(name, Qt::CaseInsensitive) == 0) return i;
    return -1;
}

bool Disk::chain(const File &f, QVector<TS> *out, QString *why) const {
    out->clear();
    if (f.sectors == 0) { *why = "the directory gives it no sectors"; return false; }
    TS ts = f.start;
    QSet<uint> seen;
    for (int n = 0; n < f.sectors; ++n) {
        if (ts.isNull()) { *why = QString("its chain ends after %1 of its %2 sectors").arg(n).arg(f.sectors); return false; }
        if (!inRange(ts)) { *why = QString("its chain leaves the disc at %1").arg(ts.toString()); return false; }
        if (seen.contains(keyOf(ts))) { *why = QString("its chain loops back to %1").arg(ts.toString()); return false; }
        seen.insert(keyOf(ts));
        out->append(ts);
        if (ts == f.end) {
            if (n + 1 != f.sectors) { *why = QString("its chain reaches its last sector %1 after %2 sectors, not %3").arg(f.end.toString()).arg(n + 1).arg(f.sectors); return false; }
            return true;
        }
        QByteArray b;
        QString e;
        if (!readSector(ts, &b, &e)) { *why = e; return false; }
        ts = tsAt(b, 0);
    }
    *why = QString("its chain doesn't reach its last sector %1 in its %2 sectors").arg(f.end.toString()).arg(f.sectors);
    return false;
}

bool Disk::readFile(const File &f, QByteArray *out, QString *err) const {
    out->clear();
    QVector<TS> c;
    QString why;
    if (!chain(f, &c, &why)) { *err = why; return false; }
    out->reserve(int(f.size()));
    for (int i = f.mapSectors(); i < c.size(); ++i) {
        QByteArray b;
        if (!readSector(c[i], &b, err)) return false;
        out->append(b.mid(4));
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ check

dw::fs::Report Disk::check() const {
    dw::fs::Report r;
    if (!isAttached()) { r.errors << "no disc"; return r; }
    QHash<uint, QString> owner;                      // sector -> what uses it
    auto claim = [&](const TS &ts, const QString &who) {
        const uint k = keyOf(ts);
        if (owner.contains(k)) r.errors << QString("sector %1 belongs to both %2 and %3").arg(ts.toString(), owner[k], who);
        else owner[k] = who;
    };

    // the directory's own chain
    {
        TS ts(0, FirstDirSector);
        QSet<uint> seen;
        while (!ts.isNull()) {
            if (!inRange(ts)) { r.errors << QString("the directory's chain leaves the disc at %1").arg(ts.toString()); break; }
            if (seen.contains(keyOf(ts))) { r.errors << QString("the directory's chain loops back to %1").arg(ts.toString()); break; }
            seen.insert(keyOf(ts));
            claim(ts, "the directory");
            QByteArray b;
            QString e;
            if (!readSector(ts, &b, &e)) { r.errors << e; break; }
            ts = tsAt(b, 0);
        }
    }

    // the files
    QHash<QString, int> names;
    for (const File &f : m_files) {
        const QString key = f.name.toUpper();
        if (names.contains(key)) r.errors << QString("%1 is in the directory twice").arg(f.name);
        names[key]++;
        if (!f.problem.isEmpty()) { r.errors << f.name + ": " + f.problem; continue; }
        QVector<TS> c;
        QString why;
        chain(f, &c, &why);
        for (const TS &ts : c) claim(ts, f.name);
        QByteArray last;
        QString e;
        if (readSector(f.end, &last, &e) && !tsAt(last, 0).isNull())
            r.warnings << QString("%1: its last sector %2 links on to %3").arg(f.name, f.end.toString(), tsAt(last, 0).toString());
        // record numbers: 1, 2, 3 ... for the data; 0 for a random file's map
        for (int i = 0; i < c.size(); ++i) {
            QByteArray b;
            if (!readSector(c[i], &b, &e)) break;
            const int want = i < f.mapSectors() ? 0 : i - f.mapSectors() + 1;
            if (u16(b, 2) != want) {
                r.warnings << QString("%1: sector %2 has record number %3, not %4").arg(f.name, c[i].toString()).arg(u16(b, 2)).arg(want);
                break;
            }
        }
        if (f.isRandom()) {
            // the map: (track, sector, count) runs from byte 4 of the first map sector, covering the data sectors
            QByteArray m1, m2;
            if (readSector(c[0], &m1, &e) && readSector(c[1], &m2, &e)) {
                const QByteArray map = m1.mid(4) + m2.mid(4);
                QVector<TS> listed;
                for (int p = 0; p + 2 < map.size() && listed.size() < f.dataSectors(); p += 3) {
                    TS ts = tsAt(map, p);
                    int n = uchar(map[p + 2]);
                    if (ts.isNull() || n == 0) break;
                    for (int k = 0; k < n && inRange(ts); ++k) {
                        listed << ts;
                        if (++ts.s > m_sir.sectors) { ts.s = 1; ts.t++; }
                    }
                }
                if (listed != c.mid(f.mapSectors()))
                    r.warnings << QString("%1: its random-access map doesn't list its sectors as its chain does").arg(f.name);
            }
        }
        if (f.month || f.day || f.year) {
            if (!f.date().isValid()) r.warnings << QString("%1: its date %2-%3-%4 is not a date").arg(f.name).arg(f.month).arg(f.day).arg(f.year);
        }
    }

    // the free chain, from the SIR's first free sector to its last
    {
        TS ts = m_sir.firstFree;
        QSet<uint> seen;
        int n = 0;
        TS last;
        bool broken = false;
        while (!ts.isNull()) {
            if (!inRange(ts)) { r.errors << QString("the free chain leaves the disc at %1").arg(ts.toString()); broken = true; break; }
            if (seen.contains(keyOf(ts))) { r.errors << QString("the free chain loops back to %1 after %2 sectors").arg(ts.toString()).arg(n); broken = true; break; }
            seen.insert(keyOf(ts));
            claim(ts, "the free chain");
            n++;
            last = ts;
            QByteArray b;
            QString e;
            if (!readSector(ts, &b, &e)) { r.errors << e; broken = true; break; }
            ts = tsAt(b, 0);
        }
        if (!broken) {
            if (n != m_sir.freeCount)
                r.errors << QString("the SIR says %1 sectors are free; the free chain holds %2").arg(m_sir.freeCount).arg(n);
            if (n > 0 && last != m_sir.lastFree)
                r.errors << QString("the free chain ends at %1, not at the SIR's last free sector %2").arg(last.toString(), m_sir.lastFree.toString());
            if (n == 0 && !m_sir.lastFree.isNull())
                r.warnings << QString("the free chain is empty but the SIR's last free sector is %1").arg(m_sir.lastFree.toString());
        }
    }

    // sectors after track 0 that nothing uses
    int lost = 0;
    for (int t = 1; t <= m_sir.lastTrack; ++t)
        for (int s = 1; s <= m_sir.sectors; ++s)
            if (!owner.contains(keyOf(TS(t, s)))) lost++;
    if (lost) r.warnings << QString("%1 sector%2 after track 0 belong%3 to no file and aren't free").arg(lost).arg(lost == 1 ? "" : "s").arg(lost == 1 ? "s" : "");
    return r;
}

// ------------------------------------------------------------------------------------------------ writing
// Each step is FLEX's (docs/disc-format.md, "How FLEX writes"; the labels are those of the FMS source, which are TSC
// FLEX 9.0's addresses; FLEX 2.0 does the same at its own).

static void putTS(QByteArray *b, int off, const TS &ts) { (*b)[off] = char(ts.t); (*b)[off + 1] = char(ts.s); }
static void putU16(QByteArray *b, int off, int v) { (*b)[off] = char((v >> 8) & 0xFF); (*b)[off + 1] = char(v & 0xFF); }

bool Disk::writeSector(const TS &ts, const QByteArray &data, QString *err) {
    if (!inRange(ts)) { *err = QString("sector %1 is not on the disc").arg(ts.toString()); return false; }
    if (data.size() != SectorSize) { *err = "a sector is 256 bytes"; return false; }
    return m_disc.write(offsetOf(ts), data, err);
}

// The free list as the FMS keeps it, written back into the SIR ($1D-$22) and nothing else of it (LD94A).
bool Disk::writeSirFree(QString *err) {
    QByteArray s;
    if (!readSector(TS(0, SirSector), &s, err)) return false;
    putTS(&s, 0x1D, m_sir.firstFree);
    putTS(&s, 0x1F, m_sir.lastFree);
    putU16(&s, 0x21, m_sir.freeCount);
    return writeSector(TS(0, SirSector), s, err);
}

// LD79A: the first free sector. Its link is the new first free sector and the count goes down by one; when its link is
// 0/0 the first free, the last free and the count all become 0. The caller writes the sector (cleared, numbered).
bool Disk::takeSector(TS *out, QString *err) {
    if (m_sir.firstFree.isNull()) { *err = "the disc is full: no free sectors are left"; return false; }
    const TS ts = m_sir.firstFree;
    QByteArray b;
    if (!readSector(ts, &b, err)) return false;
    const TS next = tsAt(b, 0);
    if (next.isNull()) {
        m_sir.firstFree = m_sir.lastFree = TS();
        m_sir.freeCount = 0;
    } else {
        m_sir.firstFree = next;
        m_sir.freeCount = (m_sir.freeCount - 1) & 0xFFFF;
    }
    *out = ts;
    return true;
}

bool Disk::entrySlot(const File &f, TS *sector, int *index) const {
    if (f.slot < 0 || f.slot / EntriesPerDirSector >= m_dirSectors.size()) return false;
    *sector = m_dirSectors[f.slot / EntriesPerDirSector];
    *index = f.slot % EntriesPerDirSector;
    return true;
}

bool Disk::writeEntry(const TS &sector, int index, const QByteArray &entry, QString *err) {
    if (entry.size() != EntrySize) { *err = "a directory entry is 24 bytes"; return false; }
    QByteArray b;
    if (!readSector(sector, &b, err)) return false;
    b.replace(16 + index * EntrySize, EntrySize, entry);
    return writeSector(sector, b, err);
}

// What the FMS refuses before a delete or a rename: a write-protected file (error 11), a delete- or read-protected
// one (error 12).
const File *Disk::protectedFile(const QString &name, QString *err) const {
    const int i = indexOf(name);
    if (i < 0) { *err = "no such file: " + name; return nullptr; }
    const File &f = m_files[i];
    if (f.attr & WriteProtect) { *err = f.name + " is write-protected"; return nullptr; }
    if (f.attr & DeleteProtect) { *err = f.name + " is delete-protected"; return nullptr; }
    if (f.attr & ReadProtect) { *err = f.name + " is read-protected"; return nullptr; }
    if (!f.problem.isEmpty()) { *err = f.name + ": " + f.problem; return nullptr; }
    return &f;
}

static QByteArray storedName(const QString &name) {
    const int dot = name.indexOf('.');
    const QByteArray n = (dot < 0 ? name : name.left(dot)).toLatin1().left(8);
    const QByteArray x = (dot < 0 ? QString() : name.mid(dot + 1)).toLatin1().left(3);
    return n.leftJustified(8, '\0') + x.leftJustified(3, '\0');
}

static TS nextSector(const TS &ts, int sectors) { return ts.s < sectors ? TS(ts.t, ts.s + 1) : TS(ts.t + 1, 1); }

static QByteArray mapRun(const TS &start, int count) {
    QByteArray r(3, '\0');
    r[0] = char(start.t);
    r[1] = char(start.s);
    r[2] = char(count);
    return r;
}

// FMS open for write (LD9DD), the bytes written (LD715), close (LDAC8).
bool Disk::writeFile(const QString &name, const QByteArray &data, quint8 attr, bool random, const QDate &date, QString *err) {
    if (data.isEmpty()) { *err = name + " is empty: FLEX can't hold an empty file"; return false; }
    if (indexOf(name) >= 0) { *err = name + " already exists"; return false; }
    const int dataSectors = int((data.size() + DataBytes - 1) / DataBytes);
    const int n = dataSectors + (random ? 2 : 0);

    // The slot: FLEX's search (LD8D6) remembers the first deleted or never-used slot it meets, and stops at a
    // never-used one; so it is the first slot that is either.
    TS slotSector;
    int slotIndex = -1;
    for (const TS &ds : m_dirSectors) {
        QByteArray b;
        if (!readSector(ds, &b, err)) return false;
        for (int i = 0; i < EntriesPerDirSector && slotIndex < 0; ++i) {
            const quint8 first = uchar(b[16 + i * EntrySize]);
            if (first == 0 || (first & 0x80)) { slotSector = ds; slotIndex = i; }
        }
        if (slotIndex >= 0) break;
    }
    const bool grow = slotIndex < 0;
    if (n + (grow ? 1 : 0) > m_sir.freeCount) {
        *err = QString("there isn't room for %1: it needs %2 sector%3%4, and %5 %6 free").arg(name).arg(n).arg(n == 1 ? "" : "s")
                   .arg(grow ? " and one more for the directory" : "").arg(m_sir.freeCount).arg(m_sir.freeCount == 1 ? "is" : "are");
        return false;
    }
    if (n > 0xFFFF) { *err = name + " is too large for a FLEX file"; return false; }

    // No free slot: the directory grows by a sector from the free chain (LDA44), cleared, record number 1, linked from
    // the last directory sector; the entry goes in its first slot.
    if (grow) {
        TS ns;
        if (!takeSector(&ns, err)) return false;
        QByteArray fresh(SectorSize, '\0');
        putU16(&fresh, 2, 1);
        if (!writeSector(ns, fresh, err)) return false;
        QByteArray last;
        if (!readSector(m_dirSectors.last(), &last, err)) return false;
        putTS(&last, 0, ns);
        if (!writeSector(m_dirSectors.last(), last, err)) return false;
        slotSector = ns;
        slotIndex = 0;
    }

    // The file's sectors, one at a time from the head of the free chain.
    QVector<TS> secs;
    for (int k = 0; k < n; ++k) {
        TS ts;
        if (!takeSector(&ts, err)) return false;
        secs << ts;
    }

    // A random file's map (LDC48, LDC9D): runs of consecutive data sectors, a run ending at 255 sectors.
    QVector<QByteArray> runs;
    if (random) {
        TS start = secs[2], prev = secs[2];
        int count = 1;
        for (int k = 3; k < n; ++k) {
            if (secs[k] == nextSector(prev, m_sir.sectors) && count < 255) { count++; prev = secs[k]; continue; }
            runs << mapRun(start, count);
            start = prev = secs[k];
            count = 1;
        }
        runs << mapRun(start, count);
        if (runs.size() > MaxMapRuns) {
            *err = QString("%1 can't be a random file here: its sectors would lie in %2 separate runs, and FLEX's map holds %3")
                       .arg(name).arg(runs.size()).arg(MaxMapRuns);
            return false;
        }
    }

    // Each sector cleared, numbered (a random file's map 0, its data from 1; a sequential file's from 1), linked to the
    // next one taken, the last to 0/0.
    for (int k = 0; k < n; ++k) {
        QByteArray s(SectorSize, '\0');
        putTS(&s, 0, k + 1 < n ? secs[k + 1] : TS());
        const int dataIndex = random ? k - 2 : k;
        putU16(&s, 2, dataIndex < 0 ? 0 : dataIndex + 1);
        if (dataIndex >= 0) {
            const QByteArray chunk = data.mid(dataIndex * DataBytes, DataBytes);
            s.replace(4, chunk.size(), chunk);
        } else {
            for (int r = 0; r < MapRunsPerSector; ++r) {
                const int i = k * MapRunsPerSector + r;
                if (i < runs.size()) s.replace(4 + 3 * r, 3, runs[i]);
            }
        }
        if (!writeSector(secs[k], s, err)) return false;
    }

    // The entry (written by FLEX at open and again at close): name, attributes, both reserved bytes 0, first and last
    // sector, the count, the random flag (FLEX writes 2), the date.
    QByteArray e = storedName(name);
    e.append(char(attr & ProtectionBits));
    e.append('\0');
    QByteArray tail(EntrySize - 13, '\0');                 // bytes 13-23
    putTS(&tail, 0, secs.first());
    putTS(&tail, 2, secs.last());
    putU16(&tail, 4, n);
    tail[6] = char(random ? 2 : 0);
    if (date.isValid()) {
        tail[8] = char(date.month());
        tail[9] = char(date.day());
        tail[10] = char(date.year() % 100);
    }
    e.append(tail);
    if (!writeEntry(slotSector, slotIndex, e, err)) return false;
    if (!writeSirFree(err)) return false;
    return reload(err);
}

// FMS delete (LDBDB).
bool Disk::deleteFile(const QString &name, QString *err) {
    const File *p = protectedFile(name, err);
    if (!p) return false;
    const File f = *p;
    if (!f.start.isNull()) {
        if (m_sir.lastFree.isNull()) {
            m_sir.firstFree = f.start;                       // the free chain was empty: the file's chain becomes it
        } else {
            QByteArray last;                                 // the last free sector now links to the file's first
            if (!readSector(m_sir.lastFree, &last, err)) return false;
            putTS(&last, 0, f.start);
            if (!writeSector(m_sir.lastFree, last, err)) return false;
        }
        m_sir.lastFree = f.end;
        m_sir.freeCount = (m_sir.freeCount + f.sectors) & 0xFFFF;
    }
    TS ds;
    int i;
    if (!entrySlot(f, &ds, &i)) { *err = "the directory has changed"; return false; }
    QByteArray b;
    if (!readSector(ds, &b, err)) return false;
    b[16 + i * EntrySize] = char(0xFF);                      // LDBAC: the first byte, the rest of the entry kept
    if (!writeSector(ds, b, err)) return false;
    if (!writeSirFree(err)) return false;
    return reload(err);
}

// FMS rename (LDB31): the entry rewritten in its own slot with the new name; nothing else changes.
bool Disk::renameFile(const QString &from, const QString &to, QString *err) {
    const File *p = protectedFile(from, err);
    if (!p) return false;
    const File f = *p;
    if (indexOf(to) >= 0) { *err = to + " already exists"; return false; }
    TS ds;
    int i;
    if (!entrySlot(f, &ds, &i)) { *err = "the directory has changed"; return false; }
    QByteArray b;
    if (!readSector(ds, &b, err)) return false;
    b.replace(16 + i * EntrySize, 11, storedName(to));
    if (!writeSector(ds, b, err)) return false;
    return reload(err);
}

// PROT: the protection bits, in place.
bool Disk::setProtection(const QString &name, quint8 attr, QString *err) {
    const int k = indexOf(name);
    if (k < 0) { *err = "no such file: " + name; return false; }
    const File f = m_files[k];
    TS ds;
    int i;
    if (!entrySlot(f, &ds, &i)) { *err = "the directory has changed"; return false; }
    QByteArray b;
    if (!readSector(ds, &b, err)) return false;
    const int at = 16 + i * EntrySize + 11;
    b[at] = char((uchar(b[at]) & ~ProtectionBits) | (attr & ProtectionBits));
    if (!writeSector(ds, b, err)) return false;
    return reload(err);
}

bool Disk::setVolumeName(const QByteArray &name, QString *err) {
    if (name.size() > VolumeNameLength) { *err = QString("a FLEX volume name has up to %1 characters").arg(VolumeNameLength); return false; }
    QByteArray s;
    if (!readSector(TS(0, SirSector), &s, err)) return false;
    s.replace(0x10, VolumeNameLength, name.leftJustified(VolumeNameLength, '\0'));
    if (!writeSector(TS(0, SirSector), s, err)) return false;
    return reload(err);
}

bool Disk::setVolumeNumber(int number, QString *err) {
    if (number < 0 || number > 0xFFFF) { *err = "a FLEX volume number is 0 to 65535"; return false; }
    QByteArray s;
    if (!readSector(TS(0, SirSector), &s, err)) return false;
    putU16(&s, 0x1B, number);
    if (!writeSector(TS(0, SirSector), s, err)) return false;
    return reload(err);
}

// ------------------------------------------------------------------------------------------------ the boot loader

bool Disk::bootSectors(QByteArray *out, QString *err) const {
    QByteArray a, b;
    if (!readSector(TS(0, 1), &a, err) || !readSector(TS(0, 2), &b, err)) return false;
    *out = a + b;
    return true;
}

bool Disk::hasLoader() const {
    QByteArray a;
    QString e;
    if (!readSector(TS(0, 1), &a, &e)) return false;
    for (char c : a) if (c) return true;
    return false;
}

TS Disk::bootLink() const {
    QByteArray a;
    QString e;
    if (!readSector(TS(0, 1), &a, &e)) return TS();
    return tsAt(a, BootLinkOffset);
}

bool Disk::setBootSectors(const QByteArray &sectors, const TS &linkTo, QString *err) {
    if (sectors.size() != 2 * SectorSize) { *err = "a boot loader is two sectors"; return false; }
    QByteArray a = sectors.left(SectorSize);
    putTS(&a, BootLinkOffset, linkTo);
    return writeSector(TS(0, 1), a, err) && writeSector(TS(0, 2), sectors.mid(SectorSize), err);
}

bool Disk::setBootLink(const TS &to, QString *err) {
    QByteArray a;
    if (!readSector(TS(0, 1), &a, err)) return false;
    putTS(&a, BootLinkOffset, to);
    return writeSector(TS(0, 1), a, err);
}

// ------------------------------------------------------------------------------------------------ a new disc

// NEWDISK (TSC's, FLEX 2.0 and FLEX 9.0 alike): every sector formatted with a link to the next, data and record
// numbers zero; the SIR written; the last sector of track 0 made the directory's end. NEWDISK then puts its boot
// loader in sector 1; this program leaves sector 1 empty.
bool Disk::makeBlank(dw::device::Linear *disc, int tracks, int sectors, const QByteArray &name, int number, const QDate &date, QString *err) {
    if (sectors < FirstDirSector || sectors > 255) { *err = "a FLEX disc has 5 to 255 sectors per track"; return false; }
    if (tracks < 2 || tracks > 256) { *err = "a FLEX disc has 2 to 256 tracks"; return false; }
    if (disc->size() != qint64(tracks) * sectors * SectorSize) { *err = "the disc's size isn't its geometry's"; return false; }
    if (name.size() > VolumeNameLength) { *err = QString("a FLEX volume name has up to %1 characters").arg(VolumeNameLength); return false; }
    if (number < 0 || number > 0xFFFF) { *err = "a FLEX volume number is 0 to 65535"; return false; }
    QByteArray d(int(disc->size()), '\0');
    for (int t = 0; t < tracks; ++t)
        for (int s = 1; s <= sectors; ++s) {
            const int off = (t * sectors + s - 1) * SectorSize;
            TS link;
            if (t == 0 && s == sectors) link = TS();                 // the directory's last sector
            else if (s < sectors) link = TS(t, s + 1);
            else if (t + 1 < tracks) link = TS(t + 1, 1);
            if (t == 0 && (s == 1 || s == SirSector)) link = TS();   // the loader's sector (empty) and the SIR
            d[off] = char(link.t);
            d[off + 1] = char(link.s);
        }
    QByteArray sir(SectorSize, '\0');
    sir.replace(0x10, VolumeNameLength, name.leftJustified(VolumeNameLength, '\0'));
    putU16(&sir, 0x1B, number);
    putTS(&sir, 0x1D, TS(1, 1));
    putTS(&sir, 0x1F, TS(tracks - 1, sectors));
    putU16(&sir, 0x21, (tracks - 1) * sectors);
    if (date.isValid()) {
        sir[0x23] = char(date.month());
        sir[0x24] = char(date.day());
        sir[0x25] = char(date.year() % 100);
    }
    sir[0x26] = char(tracks - 1);
    sir[0x27] = char(sectors);
    d.replace((SirSector - 1) * SectorSize, SectorSize, sir);
    return disc->write(0, d, err);
}

} // namespace Flex
