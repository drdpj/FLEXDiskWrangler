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
#include "uniflexvolume.h"
#include "flexvolume.h"

#include <diskwrangler/app/clock.h>

#include "uniflexwrite.h"

using dw::fs::AttrSpec;
using dw::fs::Entry;
using dw::fs::Path;

namespace UniFlex {

// ------------------------------------------------------------------------------------------------ names

bool NameRules::valid(const QString &name, dw::fs::NodeKind, QString *why) const {
    if (name.isEmpty()) { *why = "a UniFLEX name has at least one character"; return false; }
    if (name.size() > NameLength) { *why = QString("a UniFLEX name has up to %1 characters").arg(NameLength); return false; }
    if (name == "." || name == "..") { *why = "'.' and '..' are a directory's own entries"; return false; }
    for (QChar c : name) {
        if (c == '/' || c.unicode() == 0) { *why = "'/' can't be in a UniFLEX name"; return false; }
        if (c.unicode() > 0xFF) { *why = QString("'%1' can't be in a UniFLEX name").arg(c); return false; }
    }
    return true;
}

QString NameRules::fromHost(const QString &hostName, dw::fs::NodeKind) const {
    QString n;
    for (QChar c : hostName) {
        if (n.size() == NameLength) break;
        n += (c == '/' || c.unicode() == 0 || c.unicode() > 0xFF) ? QChar('_') : c;
    }
    if (n.isEmpty() || n == "." || n == "..") n = "file";
    return n;
}

// ------------------------------------------------------------------------------------------------ the volume


// The time of a change as UniFLEX keeps it: seconds since 1980 GMT (the core's clock, so the tests can fix it).
static quint32 nowSeconds() {
    const qint64 s = dw::now().toSecsSinceEpoch() - 315532800;    // 1 January 1980, 00:00 GMT
    return s < 0 ? 0 : quint32(qMin<qint64>(s, 0xFFFFFFFF));
}

static BytePath bytesOf(const Path &p) {
    BytePath b;
    for (const QString &s : p.parts) b << s.toLatin1();
    return b;
}

// The seven access bits from attribute values; those not given come from base.
static quint8 accessOf(const dw::fs::AttrValues &a, quint8 base) {
    static const struct { const char *key; quint8 bit; } bits[] = {
        {"ownerRead", OwnerRead}, {"ownerWrite", OwnerWrite}, {"ownerExecute", OwnerExecute}, {"othersRead", OthersRead},
        {"othersWrite", OthersWrite}, {"othersExecute", OthersExecute}, {"setUserId", SetUserId}};
    quint8 r = base;
    for (const auto &b : bits)
        if (a.contains(b.key)) r = a.value(b.key).toBool() ? quint8(r | b.bit) : quint8(r & ~b.bit);
    return r;
}

static bool ownerOf(const dw::fs::AttrValues &a, int base, int *out, QString *err) {
    *out = base;
    if (!a.contains("owner")) return true;
    bool ok = false;
    const int o = a.value("owner").toString().trimmed().toInt(&ok);
    if (!ok || o < 0 || o > 65535) { *err = "a UniFLEX owner is a user id, 0 to 65535"; return false; }
    *out = o;
    return true;
}

// A name for the disc: the volume's rules, then Latin-1 bytes.
static bool nameBytes(const NameRules &rules, const QString &name, dw::fs::NodeKind kind, QByteArray *out, QString *err) {
    if (!rules.valid(name, kind, err)) return false;
    *out = name.toLatin1();
    return true;
}

dw::fs::Caps UniFlexVolume::caps() const {
    dw::fs::Caps c;
    c.dirs = true;
    c.rename = true;
    c.renameAcrossDirs = true;           // files only: a directory stays in its directory (uniflexwrite.h)
    c.title = true;
    c.dates = true;
    c.settableDates = false;
    return c;
}

QVector<AttrSpec> UniFlexVolume::fileAttrs() const {
    auto flag = [](const QString &key, const QString &label, const QString &letter) {
        AttrSpec a;
        a.key = key; a.label = label; a.type = AttrSpec::Bool; a.letter = letter; a.dirs = true;
        a.retroFamily = "uniflex"; a.retroKey = key;
        return a;
    };
    AttrSpec owner, links, fdn, type;
    owner.key = "owner"; owner.label = "Owner"; owner.type = AttrSpec::Int; owner.dirs = true; owner.sample = "65535";
    owner.retroFamily = "uniflex"; owner.retroKey = "owner";
    links.key = "links"; links.label = "Links"; links.type = AttrSpec::Int; links.editable = false; links.dirs = true; links.sample = "255";
    fdn.key = "fdn"; fdn.label = "Fdn"; fdn.type = AttrSpec::Int; fdn.editable = false; fdn.dirs = true; fdn.sample = "65535"; fdn.column = false;
    type.key = "type"; type.label = "Type"; type.type = AttrSpec::Text; type.editable = false; type.dirs = true; type.column = false;
    type.sample = "character device 255, 255";
    // owner's r w x in lower case, others' in upper case, as `ls` letters
    return {flag("ownerRead", "Owner read", "r"), flag("ownerWrite", "Owner write", "w"), flag("ownerExecute", "Owner execute", "x"),
            flag("othersRead", "Others read", "R"), flag("othersWrite", "Others write", "W"), flag("othersExecute", "Others execute", "X"),
            flag("setUserId", "Set user id on execution", "s"), owner, links, fdn, type};
}

QVector<AttrSpec> UniFlexVolume::volumeAttrs() const {
    AttrSpec label, fsName, number;
    label.key = "label"; label.label = "Pack name"; label.type = AttrSpec::Text; label.sample = "MMMMMMMMMMMMMM";
    fsName.key = "fsName"; fsName.label = "File system name"; fsName.type = AttrSpec::Text; fsName.sample = "MMMMMMMMMMMMMM";
    number.key = "number"; number.label = "File system number"; number.type = AttrSpec::Int; number.sample = "65535";
    return {label, fsName, number};
}

dw::fs::AttrValues UniFlexVolume::volumeValues() const {
    const Sir &s = m_fs->sir();
    return {{"label", s.packNameText()}, {"fsName", s.fsNameText()}, {"number", s.number}};
}

dw::fs::VolumeInfo UniFlexVolume::info() const {
    const Sir &s = m_fs->sir();
    dw::fs::VolumeInfo i;
    i.title = s.packNameText();
    i.total = (s.volumeBlocks() - s.firstDataBlock()) * BlockSize;
    i.free = s.freeBlocks * BlockSize;
    return i;
}

Entry UniFlexVolume::entryOf(int node) const {
    const Node &n = m_fs->nodes()[node];
    Entry e;
    e.name = n.name;
    const Fdn *f = m_fs->fdnOf(n);
    if (!f) { e.problem = n.problem; return e; }
    e.kind = f->isDir() ? dw::fs::NodeKind::Dir : dw::fs::NodeKind::File;
    e.size = f->isDir() || f->isDevice() ? 0 : f->size;
    e.modified = f->when();
    e.attrs = {{"ownerRead", bool(f->access & OwnerRead)}, {"ownerWrite", bool(f->access & OwnerWrite)},
               {"ownerExecute", bool(f->access & OwnerExecute)}, {"othersRead", bool(f->access & OthersRead)},
               {"othersWrite", bool(f->access & OthersWrite)}, {"othersExecute", bool(f->access & OthersExecute)},
               {"setUserId", bool(f->access & SetUserId)}, {"owner", f->owner}, {"links", int(f->links)},
               {"fdn", f->number}, {"type", f->typeText()}};
    e.problem = n.problem;
    return e;
}

int UniFlexVolume::node(const Path &p, QString *err) const {
    const int n = m_fs->find(p.parts);
    if (n < 0) *err = "no such file or directory: /" + p.toString();
    return n;
}

bool UniFlexVolume::list(const Path &dir, QVector<Entry> *out, QString *err) const {
    out->clear();
    const int at = node(dir, err);
    if (at < 0) return false;
    const Node &n = m_fs->nodes()[at];
    const Fdn *f = m_fs->fdnOf(n);
    if (!f || !f->isDir()) { *err = "not a directory: /" + dir.toString(); return false; }
    if (!n.problem.isEmpty()) { *err = printable("/" + dir.toString()) + ": " + n.problem; return false; }
    for (int k : n.kids) out->append(entryOf(k));
    return true;
}

bool UniFlexVolume::stat(const Path &p, Entry *out, QString *err) const {
    const int at = node(p, err);
    if (at < 0) return false;
    *out = entryOf(at);
    return true;
}

bool UniFlexVolume::read(const Path &file, QByteArray *out, QString *err) const {
    const int at = node(file, err);
    return at >= 0 && m_fs->readFile(m_fs->nodes()[at], out, err);
}

bool UniFlexVolume::write(const Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &attrs,
                          const QDateTime &, bool replace, QString *err) {
    QByteArray n;
    if (!nameBytes(m_names, name, dw::fs::NodeKind::File, &n, err)) return false;
    int owner;
    if (!ownerOf(attrs, 0, &owner, err)) return false;
    // The time is the time of the change, as UniFLEX's copy gives it; the access and owner come with the file.
    Writer w(m_fs, nowSeconds());
    return w.writeFile(bytesOf(dir), n, data, accessOf(attrs, DefaultFileAccess), owner, replace, err);
}

bool UniFlexVolume::mkdir(const Path &dir, const QString &name, QString *err) {
    QByteArray n;
    if (!nameBytes(m_names, name, dw::fs::NodeKind::Dir, &n, err)) return false;
    Writer w(m_fs, nowSeconds());
    return w.makeDir(bytesOf(dir), n, err);
}

bool UniFlexVolume::remove(const Path &node, QString *err) {
    Writer w(m_fs, nowSeconds());
    return w.remove(bytesOf(node), err);
}

bool UniFlexVolume::rename(const Path &from, const Path &to, QString *err) {
    QByteArray n;
    if (!nameBytes(m_names, to.name(), dw::fs::NodeKind::File, &n, err)) return false;
    Writer w(m_fs, nowSeconds());
    return w.rename(bytesOf(from), bytesOf(to.parent()), n, err);
}

bool UniFlexVolume::setAttrs(const Path &p, const dw::fs::AttrValues &a, QString *err) {
    const int at = node(p, err);
    if (at < 0) return false;
    const Fdn *f = m_fs->fdnOf(m_fs->nodes()[at]);
    if (!f) { *err = m_fs->nodes()[at].problem; return false; }
    for (const char *k : {"links", "fdn", "type"})
        if (a.contains(k) && a.value(k) != entryOf(at).attrs.value(k)) { *err = QString("the %1 can't be set").arg(k); return false; }
    const quint8 access = accessOf(a, f->access & 0x7F);
    int owner;
    if (!ownerOf(a, f->owner, &owner, err)) return false;
    const int oldOwner = f->owner;
    const quint8 oldAccess = f->access & 0x7F;
    if (access != oldAccess) {
        Writer w(m_fs, nowSeconds());
        if (!w.setAccess(bytesOf(p), access, err)) return false;
    }
    if (owner != oldOwner) {
        Writer w(m_fs, nowSeconds());
        if (!w.setOwner(bytesOf(p), owner, err)) return false;
    }
    return true;
}

bool UniFlexVolume::setVolumeAttrs(const dw::fs::AttrValues &a, QString *err) {
    const Sir &s = m_fs->sir();
    QByteArray fsName = s.fsName.left(s.fsName.indexOf('\0') < 0 ? NameLength : s.fsName.indexOf('\0'));
    QByteArray pack = s.packName.left(s.packName.indexOf('\0') < 0 ? NameLength : s.packName.indexOf('\0'));
    int number = s.number;
    auto text = [&](const char *key, const char *what, QByteArray *out) {
        if (!a.contains(key)) return true;
        const QString t = a.value(key).toString();
        if (t.size() > NameLength) { *err = QString("a UniFLEX %1 has up to %2 characters").arg(what).arg(NameLength); return false; }
        for (QChar c : t)
            if (c.unicode() == 0 || c.unicode() > 0xFF) { *err = QString("'%1' can't be in a UniFLEX %2").arg(c).arg(what); return false; }
        *out = t.toLatin1();
        return true;
    };
    if (!text("label", "pack name", &pack) || !text("fsName", "file system name", &fsName)) return false;
    if (a.contains("number")) {
        bool ok = false;
        number = a.value("number").toString().trimmed().toInt(&ok);
        if (!ok || number < 0 || number > 65535) { *err = "a UniFLEX file system number is 0 to 65535"; return false; }
    }
    Writer w(m_fs, nowSeconds());
    return w.setNames(fsName, pack, number, err);
}

// ------------------------------------------------------------------------------------------------ detection

// A NUL-padded name of printable characters, at least one long.
static bool padded(const QByteArray &b) {
    int n = 0;
    while (n < b.size() && b[n] != '\0') {
        if (uchar(b[n]) < 0x20 || uchar(b[n]) > 0x7E) return false;
        n++;
    }
    if (n == 0) return false;
    for (int i = n; i < b.size(); ++i) if (b[i] != '\0') return false;
    return true;
}

QVector<dw::fs::Candidate> UniFlexProbe::probe(const dw::fs::ProbeContext &ctx) const {
    if (!ctx.container) return {};
    if (ctx.container->kind() != dw::device::Kind::Raw) {
        dw::device::RawLayout l;
        if (!ctx.container->storedLayout(&l) || l.seclen != BlockSize || l.t0sectrk) return {};   // one density throughout (the kernel's driver)
    }
    const qint64 size = ctx.size();
    if (size < 3 * BlockSize) return {};
    const QByteArray sb = ctx.stored(qint64(SirBlock) * BlockSize, BlockSize);
    if (sb.size() < BlockSize) return {};
    ProbeResult r;
    r.sir = Sir::parse(sb);
    r.fileBytes = size;
    QString why;
    bool fs = sirPlausible(r.sir, size, &why), dot = false;
    if (fs) {
        // fdn 1 is the root directory, and its first block starts with "." naming fdn 1 (format's mkrtf)
        const Fdn root = Fdn::parse(ctx.stored(qint64(FirstFdnBlock) * BlockSize, FdnSize), RootFdn);
        const qint64 b = root.map[0];
        fs = root.mode == (Busy | Directory) && b >= r.sir.firstDataBlock() && b <= r.sir.lastBlock && (b + 1) * BlockSize <= size;
        if (!fs) why = "its fdn 1 isn't a root directory";
        else {
            const QByteArray first = ctx.stored(b * BlockSize, DirEntrySize);
            dot = first.size() == DirEntrySize && uchar(first[0]) == 0 && uchar(first[1]) == RootFdn && first[2] == '.' && first[3] == '\0';
        }
    }
    dw::fs::Candidate c;
    c.fsId = "uniflex";
    c.name = "UniFLEX";
    if (!fs) {
        // a SIR naming a file system and a pack, and nothing behind it
        if (!padded(r.sir.fsName) || !padded(r.sir.packName)) return {};
        c.description = QString("what looks like a UniFLEX disc (\"%1\"), but %2").arg(r.sir.fsNameText(), why);
        c.tier = dw::fs::Tier::Possible;
        c.supported = false;
        c.evidence << "block 1 names a UniFLEX file system and pack";
        return {c};
    }
    const qint64 blocks = size / BlockSize, want = r.sir.imageBlocks();
    r.exact = size % BlockSize == 0 && blocks == want;
    const QString name = !r.sir.packNameText().isEmpty() ? r.sir.packNameText() : r.sir.fsNameText();
    c.description = QString("UniFLEX disc%1, %2 blocks").arg(name.isEmpty() ? QString() : " " + name).arg(r.sir.volumeBlocks()) + Flex::containerPhrase(ctx.container->kind());
    c.tier = !dot ? dw::fs::Tier::Possible : r.exact ? dw::fs::Tier::Certain : blocks >= r.sir.volumeBlocks() ? dw::fs::Tier::Likely : dw::fs::Tier::Possible;
    c.evidence << QString("block 1 is a UniFLEX SIR: %1 fdn blocks, a volume of %2 blocks").arg(r.sir.fdnBlocks).arg(r.sir.volumeBlocks())
               << (dot ? QString("fdn 1 is the root directory") : QString("fdn 1 is a directory"));
    if (!dot) c.notes << "the root directory's first block doesn't start with its '.' entry";
    if (r.exact) c.evidence << (r.sir.swapSize ? QString("which with %1 blocks of swap space is exactly the file's size").arg(r.sir.swapSize)
                                               : QString("which is exactly the file's size"));
    else c.notes << QString("the file is %1 bytes; the SIR's volume%2 is %3").arg(size).arg(r.sir.swapSize ? " and swap space" : "").arg(want * BlockSize);
    c.safeToWrite = blocks >= r.sir.volumeBlocks();   // as OpenedDisc: read-only when the file is shorter than its volume
    c.data = r;
    return {c};
}

} // namespace UniFlex
