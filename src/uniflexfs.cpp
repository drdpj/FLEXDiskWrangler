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
#include "uniflexfs.h"

#include <functional>

#include <QHash>
#include <QSet>
#include <QTimeZone>

namespace UniFlex {

static int u16(const QByteArray &b, int o) { return int(uchar(b[o])) << 8 | uchar(b[o + 1]); }
static qint64 u24(const QByteArray &b, int o) { return qint64(uchar(b[o])) << 16 | qint64(uchar(b[o + 1])) << 8 | uchar(b[o + 2]); }
static quint32 u32(const QByteArray &b, int o) { return quint32(u16(b, o)) << 16 | quint32(u16(b, o + 2)); }

QString printable(const QString &s) {
    QString o;
    for (QChar c : s) {
        if (c.unicode() < 0x20 || c.unicode() == 0x7F) o += QString("\\x%1").arg(int(c.unicode()), 2, 16, QChar('0'));
        else o += c;
    }
    return o;
}

QDateTime timeOf(quint32 seconds) {
    if (seconds == 0) return QDateTime();
    // 1980-01-01 00:00 UTC is 315532800 seconds after 1970's
    return QDateTime::fromMSecsSinceEpoch((qint64(seconds) + 315532800) * 1000, QTimeZone::utc());
}

// ------------------------------------------------------------------------------------------------ the SIR

Sir Sir::parse(const QByteArray &b) {
    Sir s;
    if (b.size() < BlockSize) return s;
    s.created = u32(b, 8);
    s.updated = u32(b, 12);
    s.fdnBlocks = u16(b, 16);
    s.lastBlock = u24(b, 18);
    s.freeBlocks = u24(b, 21);
    s.freeFdns = u16(b, 24);
    s.fsName = b.mid(26, NameLength);
    s.packName = b.mid(40, NameLength);
    s.number = u16(b, 54);
    s.flawed = u16(b, 56);
    s.density = uchar(b[58]);
    s.sides = uchar(b[59]);
    s.swapStart = u24(b, 60);
    s.swapSize = u16(b, 63);
    s.listedFdnCount = uchar(b[88]);
    for (int i = 0; i < qMin(s.listedFdnCount, SirFdnsMax); ++i) s.listedFdns << u16(b, 89 + 2 * i);
    s.listedFreeCount = uchar(b[189]);
    for (int i = 0; i < qMin(s.listedFreeCount, SirFreeMax); ++i) s.listedFree << u24(b, 190 + 3 * i);
    return s;
}

QString Sir::nameText(const QByteArray &b) {
    const int nul = b.indexOf('\0');
    return QString::fromLatin1(nul < 0 ? b : b.left(nul));
}

bool sirPlausible(const Sir &s, qint64 bytes, QString *why) {
    const qint64 blocks = bytes / BlockSize;
    if (s.fdnBlocks < 1) { *why = "the SIR gives no fdn list"; return false; }
    if (s.lastBlock < s.firstDataBlock()) { *why = "the SIR's volume ends before its fdn list does"; return false; }
    if (s.firstDataBlock() > blocks) { *why = "the file is shorter than the SIR's fdn list"; return false; }
    if (s.freeBlocks > s.lastBlock) { *why = "the SIR counts more free blocks than the volume has"; return false; }
    if (s.freeFdns > s.fdnCount()) { *why = "the SIR counts more free fdns than its fdn list has"; return false; }
    if (s.listedFdnCount > SirFdnsMax || s.listedFreeCount > SirFreeMax) { *why = "the SIR's free lists are longer than UniFLEX's"; return false; }
    if (s.swapSize > 0 && s.swapStart <= s.lastBlock) { *why = "the SIR's swap space overlaps its volume"; return false; }
    return true;
}

// ------------------------------------------------------------------------------------------------ fdns

Fdn Fdn::parse(const QByteArray &b, int number) {
    Fdn f;
    f.number = number;
    f.mode = uchar(b[0]);
    f.access = uchar(b[1]);
    f.links = uchar(b[2]);
    f.owner = u16(b, 3);
    f.size = qint64(u32(b, 5));
    for (int i = 0; i < MapEntries; ++i) f.map[i] = u24(b, 9 + 3 * i);
    f.time = u32(b, 48);
    return f;
}

QString Fdn::typeText() const {
    if (mode == 0) return "free";
    if (mode == (Busy | Directory)) return "directory";
    if (mode == Busy) return "file";
    if (mode == (Busy | BlockDevice)) return QString("block device %1, %2").arg(major()).arg(minor());
    if (mode == (Busy | CharDevice)) return QString("character device %1, %2").arg(major()).arg(minor());
    return QString("unknown type $%1").arg(QString::number(mode, 16).toUpper().rightJustified(2, '0'));
}

QString Fdn::accessText() const {
    QString s;
    s += access & OwnerRead ? 'r' : '-';
    s += access & OwnerWrite ? 'w' : '-';
    s += access & OwnerExecute ? 'x' : '-';
    s += access & OthersRead ? 'r' : '-';
    s += access & OthersWrite ? 'w' : '-';
    s += access & OthersExecute ? 'x' : '-';
    if (access & SetUserId) s += 's';
    return s;
}

// ------------------------------------------------------------------------------------------------ reading

bool Fs::attach(const dw::device::Linear &disc, QString *err) {
    m_disc = disc;
    if (!reload(err)) { m_disc = dw::device::Linear(); return false; }
    return true;
}

bool Fs::readBlock(qint64 block, QByteArray *out, QString *err) const {
    if (block < 0 || block >= imageBlocks()) { *err = QString("block %1 is past the end of the image").arg(block); return false; }
    return m_disc.read(block * BlockSize, BlockSize, out, err);
}

bool Fs::blockOk(qint64 b, const char *what, QString *why) const {
    if (b < m_sir.firstDataBlock() || b > m_sir.lastBlock) {
        *why = QString("%1 names block %2, outside the volume (%3-%4)").arg(what).arg(b).arg(m_sir.firstDataBlock()).arg(m_sir.lastBlock);
        return false;
    }
    if (b >= imageBlocks()) { *why = QString("%1 names block %2, past the end of the image").arg(what).arg(b); return false; }
    return true;
}

bool Fs::blocksOf(const Fdn &f, QVector<qint64> *data, QVector<qint64> *indirect, QVector<qint64> *beyond, QString *why) const {
    if (data) data->clear();
    if (indirect) indirect->clear();
    if (beyond) beyond->clear();
    if (f.isDevice()) return true;
    const qint64 need = (f.size + BlockSize - 1) / BlockSize;
    if (need > m_sir.volumeBlocks()) { *why = QString("its size (%1 bytes) is more than the volume holds").arg(f.size); return false; }
    if (data) data->fill(0, int(need));
    auto put = [&](qint64 logical, qint64 b) {
        if (!b) return true;
        if (!blockOk(b, "its block map", why)) return false;
        if (logical < need) { if (data) (*data)[int(logical)] = b; }
        else if (beyond) beyond->append(b);
        return true;
    };
    for (int i = 0; i < DirectBlocks; ++i)
        if (!put(i, f.map[i])) return false;
    // level 1: a block of 128 data block numbers; 2 and 3: of 128 blocks of the level below (diskman's mapfil)
    std::function<bool(qint64, int, qint64)> walk = [&](qint64 b, int level, qint64 first) -> bool {
        if (!blockOk(b, level == 1 ? "an indirect block" : "a double or triple indirect block", why)) return false;
        if (indirect) indirect->append(b);
        QByteArray blk;
        if (!readBlock(b, &blk, why)) return false;
        qint64 span = 1;
        for (int l = 1; l < level; ++l) span *= PerIndirect;
        for (int k = 0; k < PerIndirect; ++k) {
            const qint64 x = u24(blk, 3 * k);
            if (!x) continue;
            if (level == 1) { if (!put(first + k, x)) return false; }
            else if (!walk(x, level - 1, first + k * span)) return false;
        }
        return true;
    };
    const qint64 single = DirectBlocks, dbl = single + PerIndirect, triple = dbl + qint64(PerIndirect) * PerIndirect;
    if (f.map[10] && !walk(f.map[10], 1, single)) return false;
    if (f.map[11] && !walk(f.map[11], 2, dbl)) return false;
    if (f.map[12] && !walk(f.map[12], 3, triple)) return false;
    return true;
}

bool Fs::readDir(const Fdn &f, QVector<QPair<int, QByteArray>> *entries, QString *why) const {
    entries->clear();
    QVector<qint64> blocks;
    if (!blocksOf(f, &blocks, nullptr, nullptr, why)) return false;
    QByteArray data;
    for (qint64 b : blocks) {
        QByteArray blk(BlockSize, '\0');
        if (b && !readBlock(b, &blk, why)) return false;
        data += blk;
    }
    data.truncate(int(f.size));
    for (int o = 0; o + DirEntrySize <= data.size(); o += DirEntrySize) {
        const int n = u16(data, o);
        if (!n) continue;
        QByteArray name = data.mid(o + 2, NameLength);
        const int nul = name.indexOf('\0');
        if (nul >= 0) name.truncate(nul);
        entries->append({n, name});
    }
    return true;
}

bool Fs::reload(QString *err) {
    m_nodes.clear();
    m_fdns.clear();
    m_treeErrors.clear();
    m_treeWarnings.clear();
    m_files = m_dirs = 0;
    QByteArray sb;
    if (!readBlock(SirBlock, &sb, err)) { *err = "the SIR can't be read: " + *err; return false; }
    m_sir = Sir::parse(sb);
    QString why;
    if (!sirPlausible(m_sir, m_disc.size(), &why)) { *err = why; return false; }
    QByteArray list;
    if (!m_disc.read(qint64(FirstFdnBlock) * BlockSize, qint64(m_sir.fdnBlocks) * BlockSize, &list, err)) return false;
    m_fdns.reserve(m_sir.fdnCount());
    for (int i = 0; i < m_sir.fdnCount(); ++i) m_fdns << Fdn::parse(list.mid(i * FdnSize, FdnSize), i + 1);
    m_refs.fill(0, m_fdns.size());
    const Fdn &root = m_fdns[0];
    if (root.mode != (Busy | Directory)) { *err = "the root (fdn 1) isn't a directory"; return false; }

    Node r;
    r.fdn = RootFdn;
    m_nodes << r;
    QSet<int> dirsSeen{RootFdn};
    auto path = [this](int node) { return printable("/" + pathOf(node).join('/')); };
    for (int at = 0; at < m_nodes.size(); ++at) {
        if (!m_nodes[at].problem.isEmpty()) continue;
        const Fdn *f = fdn(m_nodes[at].fdn);
        if (!f || !f->isDir()) continue;
        QVector<QPair<int, QByteArray>> entries;
        if (!readDir(*f, &entries, &why)) { m_nodes[at].problem = "the directory can't be read: " + why; continue; }
        bool dot = false, dotdot = false;
        const int parentFdn = at == 0 ? RootFdn : m_nodes[m_nodes[at].parent].fdn;
        QSet<QByteArray> names;
        for (const auto &e : entries) {
            if (e.first <= m_fdns.size()) m_refs[e.first - 1]++;
            if (e.second == ".") {
                if (e.first != f->number) m_treeErrors << QString("%1: its . names fdn %2, not itself (%3)").arg(path(at)).arg(e.first).arg(f->number);
                dot = true;
                continue;
            }
            if (e.second == "..") {
                if (e.first != parentFdn) m_treeErrors << QString("%1: its .. names fdn %2, not its parent (%3)").arg(path(at)).arg(e.first).arg(parentFdn);
                dotdot = true;
                continue;
            }
            Node n;
            n.name = QString::fromLatin1(e.second);
            n.fdn = e.first;
            n.parent = at;
            const int idx = m_nodes.size();
            m_nodes[at].kids << idx;
            m_nodes << n;
            if (names.contains(e.second)) m_treeErrors << QString("%1: the name is in its directory twice").arg(path(idx));
            names.insert(e.second);
            bool odd = e.second.isEmpty();
            for (char c : e.second) if (uchar(c) < 0x20 || uchar(c) >= 0x7F || c == '/') odd = true;
            if (odd) m_treeWarnings << QString("%1: a name with characters other than printable ASCII").arg(path(idx));
            Node &me = m_nodes[idx];
            const Fdn *g = fdn(e.first);
            if (!g) { me.problem = QString("its entry names fdn %1, beyond the fdn list (%2 fdns)").arg(e.first).arg(m_fdns.size()); continue; }
            if (!g->busy()) { me.problem = QString("its entry names fdn %1, which is free").arg(e.first); continue; }
            if (!g->knownType()) { me.problem = QString("fdn %1 has an unknown type (%2)").arg(e.first).arg(g->typeText()); continue; }
            if (g->isDir()) {
                ++m_dirs;
                if (dirsSeen.contains(e.first)) { me.problem = QString("a directory (fdn %1) already named in another place").arg(e.first); continue; }
                dirsSeen.insert(e.first);
            } else {
                ++m_files;
                if (!blocksOf(*g, nullptr, nullptr, nullptr, &why)) me.problem = why;
            }
        }
        if (!dot) m_treeErrors << QString("%1: no . entry").arg(path(at));
        if (!dotdot) m_treeErrors << QString("%1: no .. entry").arg(path(at));
    }
    return true;
}

int Fs::find(const QStringList &parts) const {
    if (m_nodes.isEmpty()) return -1;
    int at = 0;
    for (const QString &p : parts) {
        int next = -1;
        for (int k : m_nodes[at].kids)
            if (m_nodes[k].name == p) { next = k; break; }
        if (next < 0) return -1;
        at = next;
    }
    return at;
}

QStringList Fs::pathOf(int node) const {
    QStringList p;
    for (int at = node; at > 0; at = m_nodes[at].parent) p.prepend(m_nodes[at].name);
    return p;
}

bool Fs::readFile(const Node &n, QByteArray *out, QString *err) const {
    out->clear();
    const Fdn *f = fdnOf(n);
    if (!n.problem.isEmpty()) { *err = n.problem; return false; }
    if (!f) { *err = "no such fdn"; return false; }
    if (f->isDir()) { *err = "it is a directory"; return false; }
    if (f->isDevice()) return true;
    QVector<qint64> blocks;
    if (!blocksOf(*f, &blocks, nullptr, nullptr, err)) return false;
    out->reserve(int(f->size));
    for (int i = 0; i < blocks.size();) {
        if (!blocks[i]) { out->append(QByteArray(BlockSize, '\0')); ++i; continue; }
        int run = 1;                             // read consecutive blocks at once
        while (i + run < blocks.size() && blocks[i + run] == blocks[i] + run) ++run;
        QByteArray chunk;
        if (!m_disc.read(blocks[i] * BlockSize, qint64(run) * BlockSize, &chunk, err)) return false;
        out->append(chunk);
        i += run;
    }
    out->truncate(int(f->size));
    return true;
}

bool Fs::freeList(QVector<qint64> *blocks, QString *why) const {
    blocks->clear();
    QSet<qint64> seen;
    QVector<qint64> list = m_sir.listedFree;
    if (m_sir.listedFreeCount > SirFreeMax) { *why = QString("the SIR lists %1 free blocks, more than %2").arg(m_sir.listedFreeCount).arg(SirFreeMax); return false; }
    auto take = [&](qint64 b, QString *w) {
        if (!blockOk(b, "the free list", w)) return false;
        if (seen.contains(b)) { *w = QString("the free list names block %1 twice").arg(b); return false; }
        seen.insert(b);
        blocks->append(b);
        return true;
    };
    qint64 from = 0;                             // the block the list came from (0: the SIR)
    auto where = [&](QString *w) { *w += from ? QString(" (in the list held in block %1)").arg(from) : QString(" (in the SIR's list)"); return false; };
    for (;;) {
        // aldb takes entries from the top down; a zero entry means the disc is full
        for (int i = list.size() - 1; i >= 1; --i) {
            if (!list[i]) return true;
            if (!take(list[i], why)) return where(why);
        }
        if (list.isEmpty() || !list[0]) return true;
        if (!take(list[0], why)) return where(why);
        QByteArray blk;
        if (!readBlock(list[0], &blk, why)) return false;
        from = list[0];
        list.clear();
        for (int i = 0; i < SirFreeMax; ++i) list << u24(blk, 3 * i);     // filfr: 100 entries, 300 bytes
    }
}

dw::fs::Report Fs::check() const {
    dw::fs::Report r;
    if (m_nodes.isEmpty()) { r.errors << "no disc"; return r; }
    auto path = [this](int node) { return printable("/" + pathOf(node).join('/')); };
    if (shortImage())
        r.errors << QString("the image ends at block %1, but the volume goes on to block %2").arg(imageBlocks() - 1).arg(m_sir.lastBlock);
    r.errors << m_treeErrors;
    r.warnings << m_treeWarnings;

    // the files and directories reached, each fdn once: its problem, its blocks, its link count
    QHash<qint64, QString> owner;                // block -> what holds it
    QStringList twice;
    auto own = [&](qint64 b, const QString &who) {
        auto it = owner.constFind(b);
        if (it != owner.constEnd()) { twice << QString("block %1 is in both %2 and %3").arg(b).arg(it.value(), who); return; }
        owner.insert(b, who);
    };
    QSet<int> reached;
    for (int i = 0; i < m_nodes.size(); ++i) {
        const Node &n = m_nodes[i];
        if (!n.problem.isEmpty()) r.errors << QString("%1: %2").arg(i == 0 ? QString("/") : path(i), n.problem);
        const Fdn *f = fdnOf(n);
        if (!f || !f->busy() || !f->knownType() || reached.contains(n.fdn)) continue;
        reached.insert(n.fdn);
        if (m_refs[n.fdn - 1] != f->links)
            r.errors << QString("%1: its link count is %2, but %3 directory entr%4 name%5 it").arg(i == 0 ? QString("/") : path(i)).arg(f->links)
                            .arg(m_refs[n.fdn - 1]).arg(m_refs[n.fdn - 1] == 1 ? "y" : "ies").arg(m_refs[n.fdn - 1] == 1 ? "s" : "");
        QVector<qint64> data, indirect, beyond;
        QString why;
        if (!blocksOf(*f, &data, &indirect, &beyond, &why)) continue;              // the node's problem says so
        const QString who = i == 0 ? QString("/") : path(i);
        for (qint64 b : data) if (b) own(b, who);
        for (qint64 b : indirect) own(b, who + " (an indirect block)");
        for (qint64 b : beyond) own(b, who);
        if (!beyond.isEmpty())
            r.warnings << QString("%1: its block map holds %2 block%3 after the end of its size").arg(who).arg(beyond.size()).arg(beyond.size() == 1 ? "" : "s");
    }

    // the free list
    QVector<qint64> free;
    QString why;
    if (!freeList(&free, &why)) r.errors << (why.startsWith("the free list") ? why : "the free list: " + why);
    for (qint64 b : free) own(b, "the free list");
    if (free.size() != m_sir.freeBlocks)
        r.errors << QString("the SIR counts %1 free blocks, but its free list %3 %2").arg(m_sir.freeBlocks).arg(free.size())
                        .arg(why.isEmpty() ? "holds" : "holds, as far as it can be followed,");
    r.errors << twice.mid(0, 10);
    if (twice.size() > 10) r.errors << QString("... and %1 more block%2 in two places").arg(twice.size() - 10).arg(twice.size() == 11 ? "" : "s");

    // the fdns
    int freeFdns = 0;
    QVector<int> orphans;
    for (const Fdn &f : m_fdns) {
        if (!f.busy()) ++freeFdns;
        else if (!reached.contains(f.number)) orphans << f.number;
    }
    if (freeFdns != m_sir.freeFdns) r.errors << QString("the SIR counts %1 free fdns, but %2 are free").arg(m_sir.freeFdns).arg(freeFdns);
    for (int n : m_sir.listedFdns) {
        const Fdn *f = fdn(n);
        if (!f) r.errors << QString("the SIR's list of free fdns names fdn %1, beyond the fdn list").arg(n);
        else if (f->busy()) r.errors << QString("the SIR's list of free fdns names fdn %1, which is in use").arg(n);
    }
    if (!orphans.isEmpty()) {
        QStringList some;
        for (int n : orphans.mid(0, 8)) some << QString::number(n);
        r.warnings << QString("%1 fdn%2 in use but in no directory (%3%4)").arg(orphans.size()).arg(orphans.size() == 1 ? " is" : "s are")
                          .arg(some.join(", ")).arg(orphans.size() > 8 ? ", ..." : "");
    }

    // blocks that belong to nothing (only meaningful when everything could be followed)
    if (r.errors.isEmpty()) {
        qint64 lost = 0;
        for (qint64 b = m_sir.firstDataBlock(); b <= m_sir.lastBlock; ++b) if (!owner.contains(b)) ++lost;
        if (lost) r.warnings << QString("%1 block%2 of the volume %3 neither free nor in a file").arg(lost).arg(lost == 1 ? "" : "s").arg(lost == 1 ? "is" : "are");
    }
    r.info << QString("%1 files and %2 directories; %3 of %4 blocks free; %5 of %6 fdns free")
                  .arg(m_files).arg(m_dirs + 1).arg(m_sir.freeBlocks).arg(m_sir.volumeBlocks()).arg(m_sir.freeFdns).arg(m_fdns.size());
    return r;
}

} // namespace UniFlex
