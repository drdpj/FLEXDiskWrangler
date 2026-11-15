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

} // namespace Flex
