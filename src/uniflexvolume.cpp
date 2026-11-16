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

const char *const UniFlexVolume::ReadOnlyText = "UniFLEX discs are read-only in this version of FLEX Disk Wrangler";

dw::fs::Caps UniFlexVolume::caps() const {
    dw::fs::Caps c;
    c.dirs = true;
    c.rename = false;
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
    label.key = "label"; label.label = "Pack name"; label.type = AttrSpec::Text; label.editable = false;
    fsName.key = "fsName"; fsName.label = "File system name"; fsName.type = AttrSpec::Text; fsName.editable = false;
    number.key = "number"; number.label = "File system number"; number.type = AttrSpec::Int; number.editable = false;
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

bool UniFlexVolume::write(const Path &, const QString &, const QByteArray &, const dw::fs::AttrValues &, const QDateTime &, bool, QString *err) {
    *err = ReadOnlyText;
    return false;
}
bool UniFlexVolume::mkdir(const Path &, const QString &, QString *err) { *err = ReadOnlyText; return false; }
bool UniFlexVolume::remove(const Path &, QString *err) { *err = ReadOnlyText; return false; }
bool UniFlexVolume::rename(const Path &, const Path &, QString *err) { *err = ReadOnlyText; return false; }
bool UniFlexVolume::setAttrs(const Path &, const dw::fs::AttrValues &, QString *err) { *err = ReadOnlyText; return false; }
bool UniFlexVolume::setVolumeAttrs(const dw::fs::AttrValues &, QString *err) { *err = ReadOnlyText; return false; }

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
    if (!ctx.container || ctx.container->kind() != dw::device::Kind::Raw) return {};
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
    c.description = QString("UniFLEX disc%1, %2 blocks").arg(name.isEmpty() ? QString() : " " + name).arg(r.sir.volumeBlocks());
    c.tier = !dot ? dw::fs::Tier::Possible : r.exact ? dw::fs::Tier::Certain : blocks >= r.sir.volumeBlocks() ? dw::fs::Tier::Likely : dw::fs::Tier::Possible;
    c.evidence << QString("block 1 is a UniFLEX SIR: %1 fdn blocks, a volume of %2 blocks").arg(r.sir.fdnBlocks).arg(r.sir.volumeBlocks())
               << (dot ? QString("fdn 1 is the root directory") : QString("fdn 1 is a directory"));
    if (!dot) c.notes << "the root directory's first block doesn't start with its '.' entry";
    if (r.exact) c.evidence << (r.sir.swapSize ? QString("which with %1 blocks of swap space is exactly the file's size").arg(r.sir.swapSize)
                                               : QString("which is exactly the file's size"));
    else c.notes << QString("the file is %1 bytes; the SIR's volume%2 is %3").arg(size).arg(r.sir.swapSize ? " and swap space" : "").arg(want * BlockSize);
    c.safeToWrite = false;                       // read-only in this version
    c.data = r;
    return {c};
}

} // namespace UniFlex
