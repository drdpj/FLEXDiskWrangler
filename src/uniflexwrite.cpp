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
#include "uniflexwrite.h"

#include <algorithm>
#include <functional>

namespace UniFlex {

// SIR offsets (structures/sir)
static const int SUtime = 12, SFreec = 21, SFdnc = 24, SFname = 26, SPname = 40, SFnumb = 54;
static const int SNfdn = 88, SCfdn = 89, SNfree = 189, SFree = 190;
static const int ListBytes = SirFreeMax * 3;            // a free list as stored: 100 entries of 3 bytes

QByteArray storedName(const QByteArray &name) {
    QByteArray n = name.left(NameLength);
    n.append(QByteArray(NameLength - n.size(), '\0'));
    return n;
}

static void put3(QByteArray &b, int off, qint64 v) {
    b[off] = char(v >> 16); b[off + 1] = char(v >> 8); b[off + 2] = char(v);
}
static qint64 get3(const QByteArray &b, int off) {
    return qint64(quint8(b[off])) << 16 | quint8(b[off + 1]) << 8 | quint8(b[off + 2]);
}

static QByteArray fdnBytes(const Fdn &f, quint32 time) {
    QByteArray r(FdnSize, '\0');
    r[0] = char(f.mode); r[1] = char(f.access); r[2] = char(f.links);
    r[3] = char(f.owner >> 8); r[4] = char(f.owner);
    for (int i = 0; i < 4; ++i) r[5 + i] = char(f.size >> (24 - 8 * i));
    for (int i = 0; i < MapEntries; ++i) put3(r, 9 + 3 * i, f.map[i]);
    for (int i = 0; i < 4; ++i) r[48 + i] = char(time >> (24 - 8 * i));
    return r;
}

// ------------------------------------------------------------------------------------------------ the disc

bool Writer::rd(qint64 block, QByteArray *out, QString *err) const {
    return m_fs->linear().read(block * BlockSize, BlockSize, out, err);
}

bool Writer::wr(qint64 block, int off, const QByteArray &bytes, QString *err) {
    dw::device::Linear l = m_fs->linear();
    return l.write(block * BlockSize + off, bytes, err);
}

bool Writer::loadSir(QString *err) { return rd(SirBlock, &m_sir, err); }

bool Writer::flushSir(QString *err) {
    for (int i = 0; i < 4; ++i) m_sir[i] = '\0';                 // supdt, swprot, the locks: in memory only
    for (int i = 0; i < 4; ++i) m_sir[SUtime + i] = char(m_now >> (24 - 8 * i));
    return wr(SirBlock, 0, m_sir, err);
}

qint64 Writer::sir3(int off) const { return get3(m_sir, off); }
int Writer::sir2(int off) const { return quint8(m_sir[off]) << 8 | quint8(m_sir[off + 1]); }
void Writer::setSir3(int off, qint64 v) { put3(m_sir, off, v); }
void Writer::setSir2(int off, int v) { m_sir[off] = char(v >> 8); m_sir[off + 1] = char(v); }

// ------------------------------------------------------------------------------------------------ blocks

// aldb (with filfr): the SIR's last listed block; entry 0 last, its block's list becoming the SIR's. The block is
// cleared. 0 in *b: the disc is full (the count is lowered all the same, as aldb lowers it).
bool Writer::aldb(qint64 *b, QString *err) {
    *b = 0;
    int n = sir1(SNfree);
    if (n == 0) return true;
    --n;
    setSir1(SNfree, n);
    const qint64 x = sir3(SFree + 3 * n);
    if (x == 0) return true;
    if (x <= 1 + m_fs->sir().fdnBlocks || x > m_fs->sir().lastBlock) { *err = QString("the free list names block %1, outside the volume").arg(x); return false; }
    if (n == 0) {                                                // filfr
        QByteArray l;
        if (!rd(x, &l, err)) return false;
        m_sir.replace(SFree, ListBytes, l.left(ListBytes));
        setSir1(SNfree, SirFreeMax);
    }
    setSir3(SFreec, sir3(SFreec) - 1);
    if (!wr(x, 0, QByteArray(BlockSize, '\0'), err)) return false;   // clrbfr
    *b = x;
    return true;
}

// frdb (with mkfch): onto the end of the SIR's list; a full list first sorted (entries 1-99, largest first, in the SIR
// itself) and copied into the freed block's first 300 bytes, which then starts the new list.
bool Writer::frdb(qint64 b, QString *err) {
    int n = sir1(SNfree);
    if (n == 0) {
        setSir3(SFree, 0);
        n = 1;
    } else if (n == SirFreeMax) {
        QVector<qint64> e;
        for (int i = 0; i < SirFreeMax; ++i) e << sir3(SFree + 3 * i);
        std::sort(e.begin() + 1, e.end(), std::greater<qint64>());
        for (int i = 0; i < SirFreeMax; ++i) setSir3(SFree + 3 * i, e[i]);
        if (!wr(b, 0, m_sir.mid(SFree, ListBytes), err)) return false;
        n = 0;
    }
    setSir3(SFree + 3 * n, b);
    setSir1(SNfree, n + 1);
    setSir3(SFreec, sir3(SFreec) + 1);
    return true;
}

// ------------------------------------------------------------------------------------------------ fdns

bool Writer::getFdn(int n, Fdn *f, QString *err) const {
    if (n < 1 || n > m_fs->sir().fdnCount()) { *err = QString("fdn %1 is beyond the fdn list").arg(n); return false; }
    QByteArray b;
    if (!m_fs->linear().read(qint64(FirstFdnBlock) * BlockSize + qint64(n - 1) * FdnSize, FdnSize, &b, err)) return false;
    *f = Fdn::parse(b, n);
    return true;
}

bool Writer::putFdn(const Fdn &f, QString *err) {
    dw::device::Linear l = m_fs->linear();
    return l.write(qint64(FirstFdnBlock) * BlockSize + qint64(f.number - 1) * FdnSize, fdnBytes(f, m_now), err);
}

// filfdn: the free fdns from fdn 1 up, at most 50, skipping the fdn blocks /.badblocks names.
bool Writer::filfdn(QString *err) {
    QVector<qint64> bad;
    if (const Fdn *bb = m_fs->fdn(2)) {
        QString why;
        if (!m_fs->blocksOf(*bb, &bad, nullptr, nullptr, &why)) bad.clear();
    }
    int count = 0;
    const int blocks = m_fs->sir().fdnBlocks;
    for (int blk = FirstFdnBlock; blk < FirstFdnBlock + blocks && count < SirFdnsMax; ++blk) {
        if (bad.contains(blk)) continue;
        QByteArray b;
        if (!rd(blk, &b, err)) return false;
        for (int k = 0; k < FdnsPerBlock && count < SirFdnsMax; ++k) {
            if (b[k * FdnSize] != '\0') continue;
            setSir2(SCfdn + 2 * count, (blk - FirstFdnBlock) * FdnsPerBlock + k + 1);
            ++count;
        }
    }
    setSir1(SNfdn, count);
    return true;
}

// alfdn: the SIR's last listed free fdn, cleared and busy.
bool Writer::alfdn(int *n, QString *err) {
    if (sir1(SNfdn) == 0) {
        if (!filfdn(err)) return false;
        if (sir1(SNfdn) == 0) { *err = "the disc has no free fdn left (it holds as many files as it can)"; return false; }
    }
    const int k = qMin(sir1(SNfdn), SirFdnsMax) - 1;
    setSir1(SNfdn, k);
    *n = sir2(SCfdn + 2 * k);
    Fdn f;
    if (!getFdn(*n, &f, err)) return false;
    if (f.mode != 0) { *err = QString("the SIR lists fdn %1 as free, but it is in use").arg(*n); return false; }
    f = Fdn();
    f.number = *n;
    f.mode = Busy;
    if (!putFdn(f, err)) return false;
    setSir2(SFdnc, sir2(SFdnc) - 1);
    return true;
}

bool Writer::frfdn(int n, QString *) {
    setSir2(SFdnc, sir2(SFdnc) + 1);
    const int k = sir1(SNfdn);
    if (k < SirFdnsMax) {
        setSir2(SCfdn + 2 * k, n);
        setSir1(SNfdn, k + 1);
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ a file's blocks

bool Writer::ind(qint64 block, int k, qint64 *out, QString *err) const {
    QByteArray b;
    if (!rd(block, &b, err)) return false;
    *out = get3(b, 3 * k);
    return true;
}

// mapfil reading: 0 for a block never written.
bool Writer::mapRead(const Fdn &f, qint64 i, qint64 *b, QString *err) const {
    *b = 0;
    if (i < DirectBlocks) { *b = f.map[i]; return true; }
    i -= DirectBlocks;
    int slot = 10, levels = 1;
    if (i >= PerIndirect) { i -= PerIndirect; slot = 11; levels = 2;
        if (i >= PerIndirect * PerIndirect) { i -= PerIndirect * PerIndirect; slot = 12; levels = 3; } }
    qint64 x = f.map[slot];
    for (int l = levels - 1; l >= 0 && x; --l) {
        int k = int((i >> (7 * l)) & 127);
        if (!ind(x, k, &x, err)) return false;
    }
    *b = x;
    return true;
}

// mapfil writing: the map slot taken if missing, then each indirect level, then the data block. 0: the disc is full.
bool Writer::mapWrite(Fdn *f, qint64 i, qint64 *b, QString *err) {
    *b = 0;
    auto slotOf = [&](int s, qint64 *x) {
        if (!f->map[s]) {
            qint64 y;
            if (!aldb(&y, err)) return false;
            if (!y) { *x = 0; return true; }
            f->map[s] = y;
            if (!putFdn(*f, err)) return false;
        }
        *x = f->map[s];
        return true;
    };
    auto through = [&](qint64 blk, int k, qint64 *x) {
        if (!ind(blk, k, x, err)) return false;
        if (!*x) {
            if (!aldb(x, err)) return false;
            if (!*x) return true;
            QByteArray e(3, '\0');
            put3(e, 0, *x);
            if (!wr(blk, 3 * k, e, err)) return false;
        }
        return true;
    };
    if (i < DirectBlocks) return slotOf(int(i), b);
    i -= DirectBlocks;
    int slot = 10, levels = 1;
    if (i >= PerIndirect) { i -= PerIndirect; slot = 11; levels = 2;
        if (i >= PerIndirect * PerIndirect) { i -= PerIndirect * PerIndirect; slot = 12; levels = 3; } }
    qint64 x;
    if (!slotOf(slot, &x)) return false;
    for (int l = levels - 1; l >= 0 && x; --l)
        if (!through(x, int((i >> (7 * l)) & 127), &x)) return false;
    *b = x;
    return true;
}

// filwr: block by block; the size becomes the end of what is written if that is further.
bool Writer::writeAt(Fdn *f, qint64 pos, const QByteArray &data, QString *err) {
    qint64 done = 0;
    while (done < data.size()) {
        const qint64 p = pos + done;
        const qint64 i = p / BlockSize;
        const int off = int(p % BlockSize);
        const int take = int(qMin<qint64>(BlockSize - off, data.size() - done));
        qint64 b;
        if (!mapWrite(f, i, &b, err)) return false;
        if (!b) { *err = "the disc is full"; return false; }
        if (!wr(b, off, data.mid(int(done), take), err)) return false;
        done += take;
        if (p + take > f->size) f->size = p + take;
    }
    return putFdn(*f, err);
}

// frind: an indirect block's entries from 127 down (each freed, or its own tree), then the block itself.
bool Writer::frind(qint64 b, int level, QString *err) {
    QByteArray blk;
    if (!rd(b, &blk, err)) return false;
    for (int k = PerIndirect - 1; k >= 0; --k) {
        const qint64 x = get3(blk, 3 * k);
        if (!x) continue;
        if (!(level == 1 ? frdb(x, err) : frind(x, level - 1, err))) return false;
    }
    return frdb(b, err);
}

// rmvfil: the triple, double and single indirect trees, then the direct blocks 9 down to 0.
bool Writer::rmvfil(Fdn *f, QString *err) {
    if (f->isDevice()) return true;
    for (int s = 12; s >= 10; --s)
        if (f->map[s] && !frind(f->map[s], s - 9, err)) return false;
    for (int s = 9; s >= 0; --s)
        if (f->map[s] && !frdb(f->map[s], err)) return false;
    f->size = 0;
    for (qint64 &m : f->map) m = 0;
    return putFdn(*f, err);
}

// ------------------------------------------------------------------------------------------------ directories

bool Writer::entries(int dir, QVector<Entry> *out, QString *err) const {
    out->clear();
    Fdn f;
    if (!getFdn(dir, &f, err)) return false;
    const qint64 n = f.size / DirEntrySize;
    QByteArray blk;
    qint64 have = -1;
    for (qint64 k = 0; k < n; ++k) {
        const qint64 i = k * DirEntrySize / BlockSize;
        if (i != have) {
            qint64 b;
            if (!mapRead(f, i, &b, err)) return false;
            if (b) { if (!rd(b, &blk, err)) return false; }
            else blk = QByteArray(BlockSize, '\0');
            have = i;
        }
        const int o = int(k * DirEntrySize % BlockSize);
        out->append({quint8(blk[o]) << 8 | quint8(blk[o + 1]), blk.mid(o + 2, NameLength)});
    }
    return true;
}

// pthnm in one directory: the slot holding the name (-1: none) and the first empty slot before it (-1: none).
bool Writer::search(int dir, const QByteArray &name, int *slot, int *firstFree, QString *err) const {
    *slot = *firstFree = -1;
    QVector<Entry> es;
    if (!entries(dir, &es, err)) return false;
    const QByteArray want = storedName(name);
    for (int k = 0; k < es.size(); ++k) {
        if (es[k].fdn == 0) { if (*firstFree < 0) *firstFree = k; continue; }
        if (es[k].name == want) { *slot = k; return true; }
    }
    return true;
}

bool Writer::lookup(const BytePath &path, int *dir, int *fdn, QString *err) const {
    *dir = RootFdn;
    *fdn = RootFdn;
    for (int i = 0; i < path.size(); ++i) {
        if (i > 0) {
            Fdn d;
            if (!getFdn(*fdn, &d, err)) return false;
            if (!d.isDir()) { *err = "not a directory: " + printable(QString::fromLatin1(path[i - 1])); return false; }
            *dir = *fdn;
        }
        int slot, free;
        if (!search(*dir, path[i], &slot, &free, err)) return false;
        if (slot < 0) {
            if (i + 1 < path.size()) { *err = "no directory " + printable(QString::fromLatin1(path[i])); return false; }
            *fdn = 0;
            return true;
        }
        QVector<Entry> es;
        if (!entries(*dir, &es, err)) return false;
        *fdn = es[slot].fdn;
    }
    return true;
}

// crfil4: the entry in the first empty slot, or after the last (the directory growing).
bool Writer::enter(int dir, const QByteArray &name, int fdn, QString *err) {
    int slot, free;
    if (!search(dir, name, &slot, &free, err)) return false;
    Fdn d;
    if (!getFdn(dir, &d, err)) return false;
    const qint64 pos = (free >= 0 ? free : d.size / DirEntrySize) * qint64(DirEntrySize);
    QByteArray e(2, '\0');
    e[0] = char(fdn >> 8); e[1] = char(fdn);
    return writeAt(&d, pos, e + storedName(name), err);
}

// ------------------------------------------------------------------------------------------------ system calls

bool Writer::crfil(int dir, const QByteArray &name, quint8 mode, quint8 access, int owner, int *n, QString *err) {
    if (!alfdn(n, err)) return false;
    Fdn f;
    if (!getFdn(*n, &f, err)) return false;
    f.mode = mode | Busy;
    f.access = access & 0x7F;
    f.links = 1;
    f.owner = owner;
    if (!putFdn(f, err)) return false;
    return enter(dir, name, *n, err);
}

bool Writer::link(int fdn, int dir, const QByteArray &name, QString *err) {
    Fdn f;
    if (!getFdn(fdn, &f, err)) return false;
    if (f.links < 127) ++f.links;
    if (!putFdn(f, err)) return false;
    return enter(dir, name, fdn, err);
}

// unlink: the entry's fdn bytes zeroed (the name stays), the link count lowered; the last link frees the fdn.
bool Writer::unlink(int dir, const QByteArray &name, QString *err) {
    int slot, free;
    if (!search(dir, name, &slot, &free, err)) return false;
    if (slot < 0) { *err = "no such name: " + printable(QString::fromLatin1(name)); return false; }
    QVector<Entry> es;
    if (!entries(dir, &es, err)) return false;
    const int n = es[slot].fdn;
    Fdn d;
    if (!getFdn(dir, &d, err)) return false;
    if (!writeAt(&d, qint64(slot) * DirEntrySize, QByteArray(2, '\0'), err)) return false;
    Fdn f;
    if (!getFdn(n, &f, err)) return false;
    if (f.links < 127) --f.links;
    if (!putFdn(f, err)) return false;
    if (f.links == 0 && n != dir) {                             // frefdn: rmvfil, the mode cleared, frfdn
        if (!rmvfil(&f, err)) return false;
        f.mode = 0;
        if (!frfdn(n, err)) return false;
        if (!putFdn(f, err)) return false;
    }
    return true;
}

// The fdn of the directory a path names.
static bool dirOf(const Writer *w, bool found, int fdn, const Fdn &f, int *out, QString *err) {
    Q_UNUSED(w);
    if (!found || !fdn) { *err = "no such directory"; return false; }
    if (!f.isDir()) { *err = "not a directory"; return false; }
    *out = fdn;
    return true;
}

bool Writer::finish(bool ok, QString *err) {
    if (ok) ok = flushSir(err);
    QString why;
    if (!m_fs->reload(&why)) {
        if (ok) *err = why;
        return false;
    }
    return ok;
}

// ------------------------------------------------------------------------------------------------ the changes

bool Writer::writeFile(const BytePath &dirPath, const QByteArray &name, const QByteArray &data, quint8 access,
                       int owner, bool replace, QString *err) {
    if (!loadSir(err)) return false;
    int up, dir;
    if (!lookup(dirPath, &up, &dir, err)) return false;
    Fdn df;
    if (dir && !getFdn(dir, &df, err)) return false;
    if (!dirOf(this, true, dir, df, &dir, err)) return false;
    int pdir, n;
    if (!lookup(BytePath(dirPath) << name, &pdir, &n, err)) return false;
    bool ok;
    if (n) {
        Fdn f;
        if (!getFdn(n, &f, err)) return false;
        if (f.isDir()) { *err = printable(QString::fromLatin1(name)) + " is a directory"; return false; }
        if (f.isDevice()) { *err = printable(QString::fromLatin1(name)) + " is a device"; return false; }
        if (!replace) { *err = printable(QString::fromLatin1(name)) + " already exists"; return false; }
        ok = rmvfil(&f, err) && (data.isEmpty() || writeAt(&f, 0, data, err));
    } else {
        ok = crfil(dir, name, 0, access, owner, &n, err);
        if (ok && !data.isEmpty()) {
            Fdn f;
            ok = getFdn(n, &f, err) && writeAt(&f, 0, data, err);
        }
    }
    return finish(ok, err);
}

bool Writer::makeDir(const BytePath &dirPath, const QByteArray &name, QString *err) {
    if (!loadSir(err)) return false;
    int up, dir;
    if (!lookup(dirPath, &up, &dir, err)) return false;
    Fdn df;
    if (dir && !getFdn(dir, &df, err)) return false;
    if (!dirOf(this, true, dir, df, &dir, err)) return false;
    int pdir, n;
    if (!lookup(BytePath(dirPath) << name, &pdir, &n, err)) return false;
    if (n) { *err = printable(QString::fromLatin1(name)) + " already exists"; return false; }
    bool ok = crfil(dir, name, Directory, 0x3F, 0, &n, err)    // crtsd, then chown to the system manager
              && link(n, n, ".", err) && link(dir, n, "..", err);
    return finish(ok, err);
}

// One name in dir removed: a directory's contents first (each in the directory's order), then it as kill +d does.
bool Writer::removeIn(int dir, const QByteArray &name, QString *err) {
    int slot, free;
    if (!search(dir, name, &slot, &free, err)) return false;
    if (slot < 0) { *err = "no such name: " + printable(QString::fromLatin1(name)); return false; }
    QVector<Entry> es;
    if (!entries(dir, &es, err)) return false;
    const int n = es[slot].fdn;
    Fdn f;
    if (!getFdn(n, &f, err)) return false;
    if (f.isDir()) {
        QVector<Entry> kids;
        if (!entries(n, &kids, err)) return false;
        for (const Entry &k : kids) {
            const QByteArray kn = k.name.left(k.name.indexOf('\0') < 0 ? NameLength : k.name.indexOf('\0'));
            if (!k.fdn || kn == "." || kn == "..") continue;
            if (!removeIn(n, kn, err)) return false;
        }
        if (!unlink(n, ".", err) || !unlink(n, "..", err)) return false;
    }
    return unlink(dir, name, err);
}

bool Writer::remove(const BytePath &path, QString *err) {
    if (path.isEmpty()) { *err = "the root can't be removed"; return false; }
    if (!loadSir(err)) return false;
    int dir, n;
    if (!lookup(path, &dir, &n, err)) return false;
    if (!n) { *err = "no such name: " + printable(QString::fromLatin1(path.last())); return false; }
    if (n == 2 && dir == RootFdn) { *err = "/.badblocks can't be removed: UniFLEX keeps the disc's bad blocks in it"; return false; }
    return finish(removeIn(dir, path.last(), err), err);
}

bool Writer::rename(const BytePath &from, const BytePath &toDir, const QByteArray &toName, QString *err) {
    if (from.isEmpty()) { *err = "the root can't be renamed"; return false; }
    if (!loadSir(err)) return false;
    int fdir, n;
    if (!lookup(from, &fdir, &n, err)) return false;
    if (!n) { *err = "no such name: " + printable(QString::fromLatin1(from.last())); return false; }
    int up, tdir;
    if (!lookup(toDir, &up, &tdir, err)) return false;
    Fdn df;
    if (tdir && !getFdn(tdir, &df, err)) return false;
    if (!dirOf(this, true, tdir, df, &tdir, err)) return false;
    Fdn f;
    if (!getFdn(n, &f, err)) return false;
    const bool moveDir = f.isDir() && tdir != fdir;
    if (moveDir) {                                              // not into itself or anything in it: up the ..s from tdir
        int at = tdir;
        for (int depth = 0; at != RootFdn; ++depth) {
            if (at == n) { *err = "a directory can't be moved into itself or a directory inside it"; return false; }
            int slot, free;
            QVector<Entry> es;
            if (depth > 4096 || !search(at, "..", &slot, &free, err) || slot < 0 || !entries(at, &es, err)) {
                if (err->isEmpty()) *err = "a directory on the way has no .. entry";
                return false;
            }
            at = es[slot].fdn;
        }
    }
    if (n == 2 && fdir == RootFdn) { *err = "/.badblocks keeps its name: UniFLEX looks for it there"; return false; }
    int sdir, m;
    if (!lookup(BytePath(toDir) << toName, &sdir, &m, err)) return false;
    if (m == n) {
        if (storedName(toName) == storedName(from.last()) && tdir == fdir) return finish(true, err);
        *err = "both names are the same file";                 // rename: "Can't rename file to itself!"
        return false;
    }
    bool ok = true;
    if (m) {
        Fdn g;
        if (!getFdn(m, &g, err)) return false;
        if (g.isDir()) { *err = printable(QString::fromLatin1(toName)) + " is a directory"; return false; }
        ok = unlink(tdir, toName, err);
    }
    ok = ok && link(n, tdir, toName, err) && unlink(fdir, from.last(), err);
    // a directory moved: its .. unlinked (the old parent's links lowered) and the new parent linked in its place (in
    // the slot just emptied), as the system manager would with link and unlink
    if (moveDir) ok = ok && unlink(n, "..", err) && link(tdir, n, "..", err);
    return finish(ok, err);
}

bool Writer::setAccess(const BytePath &path, quint8 access, QString *err) {
    if (!loadSir(err)) return false;
    int dir, n;
    if (!lookup(path, &dir, &n, err)) return false;
    if (!n) { *err = "no such name"; return false; }
    Fdn f;
    if (!getFdn(n, &f, err)) return false;
    f.access = quint8((f.access & 0x80) | (access & 0x7F));
    return finish(putFdn(f, err), err);
}

bool Writer::setOwner(const BytePath &path, int owner, QString *err) {
    if (!loadSir(err)) return false;
    int dir, n;
    if (!lookup(path, &dir, &n, err)) return false;
    if (!n) { *err = "no such name"; return false; }
    Fdn f;
    if (!getFdn(n, &f, err)) return false;
    f.owner = owner;
    return finish(putFdn(f, err), err);
}

bool Writer::setNames(const QByteArray &fsName, const QByteArray &packName, int number, QString *err) {
    if (!loadSir(err)) return false;
    m_sir.replace(SFname, NameLength, storedName(fsName));
    m_sir.replace(SPname, NameLength, storedName(packName));
    setSir2(SFnumb, number);
    return finish(true, err);
}

// ------------------------------------------------------------------------------------------------ a new disc

QByteArray formatDisc(int cylinders, int heads, int sectors, quint8 density, quint8 sides, const QByteArray &fsName,
                      const QByteArray &packName, int number, quint32 now) {
    const qint64 vol = qint64(cylinders) * heads * sectors;          // volbc: every block, as no swap space is kept
    const int fdnBlocks = int(vol / 33);                              // fdnbc: format's default, 3% (100/3) of the disc
    const qint64 free = vol - (fdnBlocks + 3);                        // frebc: less the boot, SIR, fdns and root block
    const int fdnFree = fdnBlocks * FdnsPerBlock - 2;                 // fdnfc: less the root and /.badblocks
    int inCore = int((free + 1) % SirFreeMax);                        // blkic: (free + 1) mod 100, 100 for 0
    if (inCore == 0) inCore = SirFreeMax;
    qint64 seq = 3 + fdnBlocks;                                       // frseq: the first free block
    QByteArray d(int(vol * BlockSize), '\0');
    auto blk = [&](qint64 b) { return b * BlockSize; };
    auto p4 = [&](qint64 off, quint32 v) { for (int i = 0; i < 4; ++i) d[int(off + i)] = char(v >> (24 - 8 * i)); };
    auto p3 = [&](qint64 off, qint64 v) { d[int(off)] = char(v >> 16); d[int(off + 1)] = char(v >> 8); d[int(off + 2)] = char(v); };
    auto p2 = [&](qint64 off, int v) { d[int(off)] = char(v >> 8); d[int(off + 1)] = char(v); };
    // mksir
    const qint64 s = blk(SirBlock);
    p4(s + 8, now);                                                   // scrtim
    p4(s + SUtime, now);                                              // sutime
    p2(s + 16, fdnBlocks);
    p3(s + 18, vol - 1);
    p3(s + SFreec, free);
    p2(s + SFdnc, fdnFree);
    const auto name = [&](qint64 off, const QByteArray &n) {          // xfnam: up to 14 bytes, to a NUL
        for (int i = 0; i < NameLength && i < n.size() && n[i] != '\0'; ++i) d[int(off + i)] = n[i];
    };
    name(s + SFname, fsName);
    name(s + SPname, packName);
    p2(s + SFnumb, number);
    d[int(s + 58)] = char(density);
    d[int(s + 59)] = char(sides);
    const int listed = qMin(SirFdnsMax, fdnFree);                     // mffd: fdns from 3 up
    d[int(s + SNfdn)] = char(listed);
    for (int i = 0; i < listed; ++i) p2(s + SCfdn + 2 * i, 3 + i);
    d[int(s + SNfree)] = char(inCore);                                // mfbl: the first blocks, taken in order
    for (int i = inCore - 1; i >= 0; --i) p3(s + SFree + 3 * i, seq + (inCore - 1 - i));
    seq += inCore - 1;
    qint64 listBlock = seq;                                           // frblk: entry 0, the link
    // mkrfdn: the root (a directory of 3 entries) and /.badblocks
    const qint64 f = blk(FirstFdnBlock);
    d[int(f)] = char(Busy | Directory); d[int(f + 1)] = char(0x3F); d[int(f + 2)] = char(2);
    p4(f + 5, 3 * DirEntrySize);
    p3(f + 9, fdnBlocks + 2);
    p4(f + 48, now);
    d[int(f + 64)] = char(Busy); d[int(f + 65)] = char(0x80); d[int(f + 66)] = char(1);
    p4(f + 64 + 48, now);
    // mkrtf: ., .. and .badblocks
    const qint64 r = blk(fdnBlocks + 2);
    const char *names[] = {".", "..", ".badblocks"};
    for (int i = 0; i < 3; ++i) {
        p2(r + i * DirEntrySize, i < 2 ? 1 : 2);
        name(r + i * DirEntrySize + 2, names[i]);
    }
    // mkfree / dofb: each list block holds the next 99 (entry 99 first) and links to the block after them
    while (seq != vol) {
        QByteArray l(BlockSize, '\0');
        qint64 link = 0;
        bool end = false;
        for (int e = 99; e >= 1; --e) {
            if (++seq == vol) { end = true; break; }
            l[3 * e] = char(seq >> 16); l[3 * e + 1] = char(seq >> 8); l[3 * e + 2] = char(seq);
        }
        if (!end && ++seq != vol) link = seq;
        l[0] = char(link >> 16); l[1] = char(link >> 8); l[2] = char(link);
        d.replace(int(blk(listBlock)), BlockSize, l);
        listBlock = seq;
    }
    return d;
}

} // namespace UniFlex
