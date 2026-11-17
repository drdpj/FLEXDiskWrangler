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
#include "flexvolume.h"

#include <QFileInfo>

#include <diskwrangler/app/clock.h>

#include "uniflexwrite.h"

using dw::fs::AttrSpec;
using dw::fs::Entry;
using dw::fs::Path;

namespace Flex {

// ------------------------------------------------------------------------------------------------ names

static bool nameChar(QChar c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'; }

bool NameRules::valid(const QString &name, dw::fs::NodeKind kind, QString *why) const {
    if (kind != dw::fs::NodeKind::File) { *why = "a FLEX disc has no folders"; return false; }
    const int dot = name.indexOf('.');
    const QString n = dot < 0 ? name : name.left(dot), x = dot < 0 ? QString() : name.mid(dot + 1);
    if (n.isEmpty() || n.size() > 8) { *why = "a FLEX name has 1 to 8 characters before the dot"; return false; }
    if (x.size() > 3 || x.contains('.')) { *why = "a FLEX extension has up to 3 characters"; return false; }
    if (!n[0].isLetter() || n[0].unicode() > 127) { *why = "a FLEX name starts with a letter"; return false; }
    for (QChar c : n + x)
        if (!nameChar(c)) { *why = QString("'%1' can't be in a FLEX name (letters, digits, - and _)").arg(c); return false; }
    return true;
}

QString NameRules::fromHost(const QString &hostName, dw::fs::NodeKind) const {
    const int dot = hostName.lastIndexOf('.');
    QString n = dot <= 0 ? hostName : hostName.left(dot), x = dot <= 0 ? QString() : hostName.mid(dot + 1);
    auto clean = [](const QString &s, int max) {
        QString o;
        for (QChar c : s.toUpper()) {
            if (o.size() == max) break;
            if (nameChar(c)) o += c;
            else if (c == ' ' || c == '.') o += '_';
        }
        return o;
    };
    n = clean(n, 8);
    x = clean(x, 3);
    if (n.isEmpty() || !n[0].isLetter()) n = ("F" + n).left(8);
    return x.isEmpty() ? n : n + "." + x;
}

// ------------------------------------------------------------------------------------------------ the volume

dw::fs::Caps FlexVolume::caps() const {
    dw::fs::Caps c;
    c.dirs = false;
    c.rename = true;
    c.title = true;
    c.dates = true;
    c.settableDates = false;
    c.maxEntriesPerDir = -1;                     // FLEX grows the directory from the free chain
    return c;
}

QVector<AttrSpec> FlexVolume::fileAttrs() const {
    auto flag = [](const QString &key, const QString &label, const QString &letter, const QString &retro) {
        AttrSpec a;
        a.key = key; a.label = label; a.type = AttrSpec::Bool; a.letter = letter;
        a.retroFamily = "flex"; a.retroKey = retro;
        return a;
    };
    AttrSpec sectors;
    sectors.key = "sectors"; sectors.label = "Sectors"; sectors.type = AttrSpec::Int; sectors.editable = false; sectors.sample = "00000";
    return {flag("write", "Write-protected", "W", "writeProtect"), flag("delete", "Delete-protected", "D", "deleteProtect"),
            flag("read", "Read-protected", "R", "readProtect"), flag("catalog", "Catalogue-protected", "C", "catalogProtect"),
            [&] { AttrSpec r = flag("random", "Random access", "", "random"); r.editable = false; return r; }(), sectors};
}

QVector<AttrSpec> FlexVolume::volumeAttrs() const {
    AttrSpec label, number;
    label.key = "label"; label.label = "Volume name"; label.type = AttrSpec::Text;
    number.key = "number"; number.label = "Volume number"; number.type = AttrSpec::Int;
    return {label, number};
}

dw::fs::AttrValues FlexVolume::volumeValues() const {
    return {{"label", m_d->sir().labelText()}, {"number", m_d->sir().volumeNumber}};
}

dw::fs::VolumeInfo FlexVolume::info() const {
    dw::fs::VolumeInfo i;
    i.title = m_d->sir().labelText();
    i.total = qint64(m_d->tracks() - 1) * m_d->sectorsPerTrack() * DataBytes;     // track 0 is the system's
    i.free = qint64(m_d->sir().freeCount) * DataBytes;
    i.entriesFree = m_d->freeSlots();
    return i;
}

Entry FlexVolume::entryOf(const File &f) {
    Entry e;
    e.name = f.name;
    e.kind = dw::fs::NodeKind::File;
    e.size = f.size();
    e.sizeExact = false;                         // FLEX knows a file's size only to a sector
    const QDate d = f.date();
    if (d.isValid()) e.modified = QDateTime(d, QTime(0, 0));
    e.attrs = {{"write", bool(f.attr & WriteProtect)}, {"delete", bool(f.attr & DeleteProtect)},
               {"read", bool(f.attr & ReadProtect)}, {"catalog", bool(f.attr & CatalogProtect)},
               {"random", f.isRandom()}, {"sectors", f.sectors}};
    e.problem = f.problem;
    return e;
}

bool FlexVolume::list(const Path &dir, QVector<Entry> *out, QString *err) const {
    out->clear();
    if (!dir.isRoot()) { *err = "a FLEX disc has no folders"; return false; }
    for (const File &f : m_d->files()) out->append(entryOf(f));
    return true;
}

const File *FlexVolume::find(const Path &p, QString *err) const {
    if (p.parts.size() != 1) { *err = "no such file: " + p.toString(); return nullptr; }
    const int i = m_d->indexOf(p.name());
    if (i < 0) { *err = "no such file: " + p.name(); return nullptr; }
    return &m_d->files()[i];
}

bool FlexVolume::read(const Path &file, QByteArray *out, QString *err) const {
    const File *f = find(file, err);
    return f && m_d->readFile(*f, out, err);
}

bool FlexVolume::fileName(const Path &dir, const QString &name, QString *out, QString *err) const {
    if (!dir.isRoot()) { *err = "a FLEX disc has no folders"; return false; }
    if (!m_names.valid(name, dw::fs::NodeKind::File, err)) { *err = name + ": " + *err; return false; }
    *out = name.toUpper();                       // FLEX names are upper case on the disc
    return true;
}

static quint8 protectionOf(const dw::fs::AttrValues &a, quint8 was) {
    quint8 p = was;
    auto bit = [&](const char *key, quint8 b) {
        if (!a.contains(key)) return;
        if (a.value(key).toBool()) p |= b;
        else p &= quint8(~b);
    };
    bit("write", WriteProtect);
    bit("delete", DeleteProtect);
    bit("read", ReadProtect);
    bit("catalog", CatalogProtect);
    return p;
}

// A file copied in keeps its protection and date, as FLEX's COPY keeps them; with no date, today's (FLEX's own for a
// new file). Replacing a file deletes it first, as FLEX would (so a protected one is refused).
bool FlexVolume::write(const Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &attrs,
                       const QDateTime &when, bool replace, QString *err) {
    QString n;
    if (!fileName(dir, name, &n, err)) return false;
    if (data.isEmpty()) { *err = n + " is empty: FLEX can't hold an empty file"; return false; }
    if (m_d->indexOf(n) >= 0) {
        if (!replace) { *err = n + " already exists"; return false; }
        if (!m_d->deleteFile(n, err)) return false;
    }
    const QDate date = when.isValid() ? when.date() : dw::now().date();
    return m_d->writeFile(n, data, protectionOf(attrs, 0), attrs.value("random").toBool(), date, err);
}

bool FlexVolume::remove(const Path &node, QString *err) {
    const File *f = find(node, err);
    return f && m_d->deleteFile(f->name, err);
}

bool FlexVolume::rename(const Path &from, const Path &to, QString *err) {
    const File *f = find(from, err);
    if (!f) return false;
    QString n;
    if (!fileName(to.parent(), to.name(), &n, err)) return false;
    if (n == f->name) return true;
    return m_d->renameFile(f->name, n, err);
}

bool FlexVolume::setAttrs(const Path &node, const dw::fs::AttrValues &a, QString *err) {
    const File *f = find(node, err);
    if (!f) return false;
    if (a.contains("random") && a.value("random").toBool() != f->isRandom()) {
        *err = f->name + ": whether a file is random-access can't be changed (it decides whether the file has a sector map)";
        return false;
    }
    if (a.contains("sectors") && a.value("sectors").toInt() != f->sectors) { *err = f->name + ": the number of sectors can't be set"; return false; }
    const quint8 p = protectionOf(a, f->attr);
    if (p == f->attr) return true;
    return m_d->setProtection(f->name, p, err);
}

bool FlexVolume::setVolumeAttrs(const dw::fs::AttrValues &a, QString *err) {
    if (a.contains("label")) {
        QByteArray b;
        if (!volumeNameBytes(a.value("label").toString(), &b, err)) return false;
        if (!m_d->setVolumeName(b, err)) return false;
    }
    if (a.contains("number")) {
        bool ok = false;
        const int n = a.value("number").toString().trimmed().toInt(&ok);
        if (!ok) { *err = "a FLEX volume number is 0 to 65535"; return false; }
        if (!m_d->setVolumeNumber(n, err)) return false;
    }
    return true;
}

// ------------------------------------------------------------------------------------------------ the opened disc

// track0: a short track 0 stored at its own size (0: a full track).
static dw::device::RawLayout layoutOf(const Sir &s, int track0 = 0);

// Makes the disc's bytes readable as FLEX's sectors in order: a raw file gets the SIR's layout; any other container
// gives its stored bytes, cylinder 0 at its own size (Container::setOwnTrack0). bytes: how many.
static bool flexLinear(dw::device::Container *img, const ProbeResult &r, qint64 *bytes, QString *err) {
    if (img->kind() == dw::device::Kind::Raw) {
        const dw::device::RawLayout l = layoutOf(r.sir, r.track0);
        if (!img->setRawLayout(l, err)) return false;
        *bytes = l.bytes();
        return true;
    }
    img->setOwnTrack0(true);
    return (*bytes = img->storedSize(err)) > 0;
}

static dw::device::RawLayout layoutOf(const Sir &s, int track0) {
    dw::device::RawLayout l;
    l.seclen = SectorSize;
    l.sectrk = s.sectors;                        // FLEX numbers a cylinder's sectors on both sides as one track
    l.cyls = s.lastTrack + 1;
    l.heads = 1;
    l.firstId = 1;
    if (track0 > 0) {
        l.t0sectrk = track0;
        l.t0seclen = SectorSize;
    }
    return l;
}

bool OpenedDisc::open(const QString &path, const ProbeResult &r, QString *err) {
    if (!m_img.open(path, err)) return false;
    noteFileState();
    return attach(r, err);
}

bool OpenedDisc::load(const QString &path, QString *err) {
    dw::device::Container img;
    if (!img.open(path, err)) return false;
    detach();
    m_img = img;
    noteFileState();
    return true;
}

void OpenedDisc::noteFileState() {
    const QFileInfo fi(m_img.path());
    m_fileTime = fi.lastModified();
    m_fileSize = fi.exists() ? fi.size() : -1;
}

void OpenedDisc::detach() {
    m_disc.clearMounts();
    m_disk.reset();
    m_ufs.reset();
    m_checkOk = true;
    m_exact = true;
}

FlexVolume *OpenedDisc::volume() const {
    return m_disk && !m_disc.mounts().isEmpty() ? static_cast<FlexVolume *>(m_disc.mounts()[0].vol.get()) : nullptr;
}

bool OpenedDisc::open(const QString &path, const UniFlex::ProbeResult &r, QString *err) {
    if (!m_img.open(path, err)) return false;
    noteFileState();
    return attach(r, err);
}

// The file as it is, in 512-byte blocks (a part block at its end padded): the Fs knows which blocks the volume needs.
// In an ImageDisk, DSK or TeleDisk file: its stored bytes, block n at n x 512 (cylinder by cylinder, side 0 then side 1,
// sectors in ID order: the kernel's driver numbers side 1's sectors on from side 0's, or from 1 again on an OMTI disc).
bool OpenedDisc::attach(const UniFlex::ProbeResult &r, QString *err) {
    qint64 bytes = 0;
    if (m_img.kind() == dw::device::Kind::Raw) {
        dw::device::RawLayout l;
        l.seclen = UniFlex::BlockSize;
        l.sectrk = 1;
        l.cyls = int((r.fileBytes + UniFlex::BlockSize - 1) / UniFlex::BlockSize);
        l.heads = 1;
        l.firstId = 1;
        if (!m_img.setRawLayout(l, err)) return false;
        bytes = l.bytes();
    } else {
        m_img.setOwnTrack0(true);
        if (!(bytes = m_img.storedSize(err))) return false;
    }
    detach();
    m_disc.setContainer(&m_img);
    auto fs = std::make_unique<UniFlex::Fs>();
    if (!fs->attach(dw::device::Linear(&m_img, dw::device::SideOrder(), bytes), err)) return false;
    m_ufs = std::move(fs);
    m_checkOk = m_ufs->check().ok();
    m_exact = r.exact;
    m_disc.addMount({"UniFLEX", std::make_shared<UniFlex::UniFlexVolume>(m_ufs.get()), false, QString(), "uniflex"});
    return true;
}

// A raw file gets the SIR's layout. An ImageDisk, DSK or TeleDisk file records where each sector is: its stored bytes,
// cylinder 0 at its own size, are FLEX's sectors in order (stored0 below).
bool OpenedDisc::attach(const ProbeResult &r, QString *err) {
    qint64 bytes = 0;
    if (!flexLinear(&m_img, r, &bytes, err)) return false;
    detach();
    m_disc.setContainer(&m_img);
    auto disk = std::make_unique<Disk>();
    if (!disk->attach(dw::device::Linear(&m_img, dw::device::SideOrder(), bytes), err)) return false;
    m_disk = std::move(disk);
    m_checkOk = m_disk->check().ok();
    m_exact = r.exact;
    m_disc.addMount({"FLEX", std::make_shared<FlexVolume>(m_disk.get()), false, QString(), "flex"});
    return true;
}

QString OpenedDisc::geometryWhy() const {
    return m_img.kind() == dw::device::Kind::Raw ? QString(GeometryText)
                                                 : "the disc in the image file doesn't match the geometry in its System Information Record";
}

const char *const OpenedDisc::TeleDiskText = "TeleDisk images are read-only here: use Save As to make an ImageDisk, DSK or raw copy";
const char *const OpenedDisc::GeometryText = "the image file's size doesn't match the disc's geometry in its System Information Record";

QString OpenedDisc::readOnlyWhy(int mount) const {
    if (m_disc.mounts().isEmpty()) return "no disc";
    if (m_img.readOnlyContainer()) return TeleDiskText;
    if (!m_checkOk) return "the disc has errors (Tools > Check disc)";
    if (m_ufs) return m_ufs->shortImage() ? "the image file is shorter than the UniFLEX volume in its System Information Record" : QString();
    if (!m_exact) return geometryWhy();
    const dw::fs::Disc::Mount m = m_disc.mounts().value(mount);
    return m.readOnly ? m.readOnlyWhy : QString();
}

// The saved copy, before it replaces anything: the disc read from it again, checked, and holding as many files.
bool OpenedDisc::checkWritten(dw::device::Container &written, QString *err) const {
    if (m_ufs) {
        UniFlex::Fs f;
        QString e;
        if (!f.attach(dw::device::Linear(&written, dw::device::SideOrder(), m_ufs->linear().size()), &e)) { *err = "the saved copy can't be read back: " + e; return false; }
        const dw::fs::Report r = f.check();
        if (!r.ok()) { *err = "the saved copy is not consistent:\n\n" + r.errors.mid(0, 10).join("\n"); return false; }
        if (f.nodes().size() != m_ufs->nodes().size()) { *err = "the saved copy does not hold the same files"; return false; }
        return true;
    }
    if (!m_disk) return true;
    Disk d;
    QString e;
    if (!d.attach(dw::device::Linear(&written, dw::device::SideOrder(), m_disk->linear().size()), &e)) { *err = "the saved copy can't be read back: " + e; return false; }
    const dw::fs::Report r = d.check();
    if (!r.ok()) { *err = "the saved copy is not consistent:\n\n" + r.errors.mid(0, 10).join("\n"); return false; }
    if (d.files().size() != m_disk->files().size()) { *err = "the saved copy does not hold the same files"; return false; }
    return true;
}

bool OpenedDisc::save(bool backup, QString *err) {
    if (m_disk || m_ufs) {
        const dw::fs::Report r = m_disk ? m_disk->check() : m_ufs->check();
        if (!r.ok()) { *err = "the disc would not be consistent.\n\n" + r.errors.mid(0, 10).join("\n"); return false; }
    }
    const QFileInfo fi(m_img.path());
    if (m_fileSize >= 0 && fi.exists() && (fi.lastModified() != m_fileTime || fi.size() != m_fileSize)) {
        *err = "the image file has been changed by something else since it was opened. Use Save As, or Revert.";
        return false;
    }
    dw::device::Container::SaveOptions o;
    o.backup = backup;
    o.verify = [this](dw::device::Container &written, QString *e) { return checkWritten(written, e); };
    if (!m_disc.commit(o, err)) return false;
    noteFileState();
    return true;
}

// A raw file holds no sides, densities or sector IDs, so it is saved as raw only; a disc in an ImageDisk, DSK or TeleDisk
// file can be saved in any of them (and as raw: track 0 then at its own size).
bool OpenedDisc::canSaveAs(dw::device::Kind kind, QString *err) const {
    if (kind == dw::device::Kind::Td0) { *err = "TeleDisk files can be read but not written"; return false; }
    if (m_img.kind() == dw::device::Kind::Raw && kind != dw::device::Kind::Raw) {
        *err = "a raw image doesn't say how its sectors lie on the disc (sides, density, sector numbers), so it can only be saved as a raw image";
        return false;
    }
    return true;
}

// The disc as its file holds it (no staged changes) written to out in another container, read back and checked
// before it is put in place: flexdw's convert.
bool OpenedDisc::convertTo(const QString &out, dw::device::Kind kind, QString *err) const {
    if (!canSaveAs(kind, err)) return false;
    dw::device::Container x = m_img;
    x.discardPending();
    const bool uf = m_ufs != nullptr;
    const qint64 size = uf ? m_ufs->linear().size() : m_disk ? m_disk->linear().size() : 0;
    int files = -1;
    bool ok = true;
    auto readBack = [uf, size](dw::device::Container &c, int *n, bool *good, QString *e) {
        if (uf) {
            UniFlex::Fs f;
            if (!f.attach(dw::device::Linear(&c, dw::device::SideOrder(), size), e)) return false;
            *n = f.nodes().size();
            *good = f.check().ok();
        } else {
            Disk d;
            if (!d.attach(dw::device::Linear(&c, dw::device::SideOrder(), size), e)) return false;
            *n = d.files().size();
            *good = d.check().ok();
        }
        return true;
    };
    if (size && !readBack(x, &files, &ok, err)) return false;
    auto verify = [&](dw::device::Container &w, QString *e) {
        if (!size) return true;
        int n = -1;
        bool good = true;
        if (!readBack(w, &n, &good, e)) { *e = "the copy can't be read back: " + *e; return false; }
        if (ok && !good) { *e = "the copy is not consistent"; return false; }
        if (n != files) { *e = "the copy does not hold the same files"; return false; }
        return true;
    };
    return x.saveAs(out, kind, err, verify);
}

bool OpenedDisc::saveAs(const QString &path, dw::device::Kind kind, QString *err) {
    if (!canSaveAs(kind, err)) { err->replace(0, 1, err->left(1).toUpper()); *err += "."; return false; }
    const bool ok = m_ufs ? m_ufs->check().ok() : !m_disk || m_disk->check().ok();
    auto verify = [this, ok](dw::device::Container &written, QString *e) { return !ok || checkWritten(written, e); };
    if (!m_img.saveAs(path, kind, err, verify)) return false;
    noteFileState();
    return true;
}

// ------------------------------------------------------------------------------------------------ who this is

dw::fs::OtherApp self() {
    return {"flexdiskwrangler", "FLEX Disk Wrangler", "FLEX Disk Wrangler", "https://github.com/drdpj/FLEXDiskWrangler/releases"};
}

dw::app::AppInfo appInfo(const QString &version) {
    dw::app::AppInfo a;
    a.name = "FLEX Disk Wrangler";
    a.id = "flexdiskwrangler";
    a.version = version;
    a.organisation = "flexdiskwrangler";
    a.releases = self().releases;
    return a;
}

// ------------------------------------------------------------------------------------------------ new discs

// The shapes of the sample discs and TSC's formats (docs/disc-format.md): FLEX numbers a cylinder's sectors on both
// sides as one track. A double-density disc's track 0 is single density (the Adaptation Guide, Appendix B, note 1),
// with fewer sectors; the image pads it to the full track, as the sample EFORTH's is.
QVector<dw::fs::Preset> presets() {
    struct Shape { int tracks, sectors, track0, heads; const char *what, *seen; };
    const Shape shapes[] = {
        {35, 10, 10, 1, "5.25\" single-sided, single density, 35 tracks", "TSC's 5.25\" distribution format; the shape of the sample discs FLEX2_A, FLEX2_B, FL2_0SWT, FLEX2-01, ABASIC_1"},
        {40, 10, 10, 1, "5.25\" single-sided, single density, 40 tracks", "A TSC format; the shape of the sample discs SYS900, D_MAN_1, DMDR_SC1"},
        {35, 18, 10, 1, "5.25\" single-sided, double density, 35 tracks", "The shape of the sample disc EFORTH (track 0 single density, 10 sectors)"},
        {35, 20, 20, 2, "5.25\" double-sided, single density, 35 tracks", "A TSC format"},
        {40, 20, 20, 2, "5.25\" double-sided, single density, 40 tracks", "A TSC format; the shape of the sample disc DSTAR23"},
        {40, 36, 36, 2, "5.25\" double-sided, double density, 40 tracks", "The shape of the sample discs FLX505, FLX503TX (track 0 double density too)"},
        {77, 15, 15, 1, "8\" single-sided, single density, 77 tracks", "TSC's 8\" distribution format; the shape of the sample disc C009"},
        {77, 26, 15, 1, "8\" single-sided, double density, 77 tracks", "A TSC format (track 0 single density, 15 sectors)"},
        {77, 30, 30, 2, "8\" double-sided, single density, 77 tracks", "A TSC format"},
        {77, 52, 30, 2, "8\" double-sided, double density, 77 tracks", "A TSC format (track 0 single density, 30 sectors)"},
    };
    QVector<dw::fs::Preset> out;
    for (const Shape &s : shapes) {
        dw::fs::Preset p;
        p.id = QString("flex-%1x%2").arg(s.tracks).arg(s.sectors);
        const qint64 bytes = qint64(s.tracks) * s.sectors * SectorSize;
        p.name = QString("FLEX, %1 tracks of %2 sectors (%3, %4K)").arg(s.tracks).arg(s.sectors).arg(s.what).arg(bytes / 1024.0, 0, 'f', bytes % 1024 ? 1 : 0);
        p.fsId = "flex";
        p.source = "flex";
        p.cylinders = s.tracks;
        p.heads = s.heads;
        p.sectors = s.sectors / s.heads;
        p.sectorSize = SectorSize;
        p.bytes = quint64(bytes);
        p.containers = QStringList{"raw"};
        p.suffix = "dsk";
        p.doubleSided = s.heads == 2;
        p.titleWhat = "Volume name";
        p.titleMax = VolumeNameLength;
        p.notes = QString("%1.").arg(s.seen);
        if (s.track0 < s.sectors)
            p.notes += QString(" The image pads track 0 to %1 sectors; the directory ends at sector %2.").arg(s.sectors).arg(s.track0);
        p.extra["tracks"] = s.tracks;
        p.extra["sectors"] = s.sectors;
        p.extra["track0"] = s.track0;
        // the disc it is (TSC's 6809 FLEX Adaptation Guide, Appendix B): single density 10 sectors a side on 5.25"
        // discs and 15 on 8", double density 18 and 26; 250 kbit/s on 5.25" discs, 500 on 8"; a double-density disc's
        // track 0 single density, unless (40 x 36, the sample discs' shape) it is double density too
        const int perSide = s.sectors / s.heads;
        const bool eight = s.tracks == 77;
        p.extra["fm"] = perSide == (eight ? 15 : 10);
        p.extra["rate"] = eight ? 500 : 250;
        p.containers = QStringList{"raw", "imd", "edsk"};
        if (s.track0 == s.sectors) p.containers << "dsk";            // a standard DSK file's tracks are all one size
        out << p;
    }
    // UniFLEX: the 8" floppies TSC's formatter makes (formatfd's FD-SS, FD-SD, FD-DS, FD-DD: 77 cylinders, 8 blocks of
    // 512 a side single density, 16 double), laid out as it lays them out, with no swap space and no bootstrap.
    struct UShape { int heads, sectors; quint8 density, sides; const char *id, *what, *seen; };
    const UShape ushapes[] = {
        {1, 8, 0, 0, "uniflex-ss-sd", "8\" single-sided, single density", "The shape of the sample sssd8inch"},
        {1, 16, 1, 0, "uniflex-ss-dd", "8\" single-sided, double density", "The shape of the sample ssdd8inch"},
        {2, 8, 0, 1, "uniflex-ds-sd", "8\" double-sided, single density", "The shape of the sample dssd8inch"},
        {2, 16, 1, 1, "uniflex-ds-dd", "8\" double-sided, double density", "The shape of the sample dsdd8inch (and, with swap space, fd0)"},
    };
    for (const UShape &u : ushapes) {
        dw::fs::Preset p;
        p.id = u.id;
        const qint64 blocks = qint64(77) * u.heads * u.sectors;
        p.name = QString("UniFLEX, %1 blocks (%2, %3K)").arg(blocks).arg(u.what).arg(blocks / 2);
        p.fsId = "uniflex";
        p.source = "flex";
        p.cylinders = 77;
        p.heads = u.heads;
        p.sectors = u.sectors;
        p.sectorSize = UniFlex::BlockSize;
        p.bytes = quint64(blocks * UniFlex::BlockSize);
        p.containers = QStringList{"raw"};
        p.suffix = "dsk";
        p.doubleSided = u.heads == 2;
        p.titleWhat = "Pack name";
        p.titleMax = UniFlex::NameLength;
        p.notes = QString("%1, laid out as TSC's formatter lays a disc out: %2 blocks of fdns, no swap space, no bootstrap.")
                      .arg(u.seen).arg(blocks / 33);
        p.extra["tracks"] = 77;
        p.extra["sectors"] = u.heads * u.sectors;              // a cylinder's blocks, as a FLEX preset counts its track
        p.extra["track0"] = u.heads * u.sectors;
        p.extra["density"] = u.density;
        p.extra["sides"] = u.sides;
        p.extra["fm"] = u.density == 0;                         // one density throughout (TSC's floppy driver)
        p.extra["rate"] = 500;
        p.containers = QStringList{"raw", "imd", "edsk", "dsk"};
        out << p;
    }
    return out;
}

void addPresets() {
    dw::fs::presets().removeSource("flex");
    for (const dw::fs::Preset &p : presets()) dw::fs::presets().add(p);
}

const dw::fs::Preset *findPreset(const QString &id) {
    addPresets();
    const dw::fs::Preset *p = dw::fs::presets().find(id);
    return p && p->source == "flex" ? p : nullptr;
}

bool volumeNameBytes(const QString &name, QByteArray *out, QString *err) {
    if (name.size() > VolumeNameLength) { *err = QString("a FLEX volume name has up to %1 characters").arg(VolumeNameLength); return false; }
    for (QChar c : name)
        if (c.unicode() < 0x20 || c.unicode() > 0x7E) { *err = QString("'%1' can't be in a FLEX volume name (printable ASCII only)").arg(c); return false; }
    *out = name.toLatin1();
    return true;
}

bool uniflexPackBytes(const QString &name, QByteArray *out, QString *err) {
    if (name.size() > UniFlex::NameLength) { *err = QString("a UniFLEX pack name has up to %1 characters").arg(UniFlex::NameLength); return false; }
    for (QChar c : name)
        if (c.unicode() == 0 || c.unicode() > 0xFF) { *err = QString("'%1' can't be in a UniFLEX pack name").arg(c); return false; }
    *out = name.toLatin1();
    return true;
}

// A new disc's bytes (as its raw image holds them: a FLEX disc's short track 0 padded) written to path as kind. A raw
// image is the bytes. An ImageDisk or DSK file is the disc itself: p's cylinders and sides, side 1's sectors numbered on
// from side 0's (as FLEX and UniFLEX's driver number them), FM or MFM at the preset's rate, and a FLEX disc's
// single-density track 0 its own shape, its padding left out.
static bool writeNew(const QString &path, const dw::fs::Preset &p, const QByteArray &bytes, dw::device::Kind kind, QString *err) {
    using dw::device::Kind;
    const QString comment = QString("Made by FLEX Disk Wrangler, %1").arg(p.id);
    if (!p.containers.contains(kind == Kind::Raw ? "raw" : kind == Kind::Imd ? "imd" : kind == Kind::Edsk ? "edsk" : kind == Kind::Dsk ? "dsk" : "?")) {
        *err = QString("a %1 disc can't be made as %2").arg(p.id, dw::device::kindName(kind));
        return false;
    }
    if (kind == Kind::Raw) {
        dw::device::RawLayout l;
        l.seclen = p.sectorSize; l.sectrk = 1; l.cyls = int(bytes.size() / p.sectorSize); l.heads = 1; l.firstId = 1;
        dw::device::Container img;
        img.createBlank(Kind::Raw, l, comment);
        dw::device::Linear lin(&img, dw::device::SideOrder(), l.bytes());
        return lin.write(0, bytes, err) && img.saveAs(path, Kind::Raw, err);
    }
    const bool fm = p.extra.value("fm").toBool();
    const int rate = p.extra.value("rate").toInt();
    auto mode = [rate](bool f) { return rate == 500 ? (f ? 0 : 3) : (f ? 2 : 5); };     // ImageDisk's modes
    const int perTrack = p.extra.value("sectors").toInt(), track0 = p.extra.value("track0", perTrack).toInt();
    dw::device::RawLayout l;
    l.seclen = p.sectorSize; l.sectrk = p.sectors; l.cyls = p.cylinders; l.heads = p.heads; l.firstId = 1; l.mode = mode(fm);
    dw::device::Container img;
    img.createBlank(kind, l, comment);
    for (int c = 0; c < p.cylinders; ++c)
        for (int h = 0; h < p.heads; ++h) {
            const int n = c == 0 ? track0 / p.heads : p.sectors;
            if (!img.reshapeTrack(c, h, n, p.sectorSize, 1 + h * n, mode(fm || (c == 0 && n < p.sectors)), err)) return false;
        }
    img.setOwnTrack0(true);
    QByteArray stored = bytes;
    if (track0 < perTrack) stored.remove(track0 * p.sectorSize, (perTrack - track0) * p.sectorSize);   // track 0's padding
    dw::device::Linear lin(&img);
    if (lin.size() != stored.size()) { *err = "the new disc doesn't fit its shape"; return false; }
    return lin.write(0, stored, err) && img.saveAs(path, kind, err);
}

// A new UniFLEX disc (UniFlex::formatDisc), timed by the core's clock.
static bool createUniflex(const QString &path, const dw::fs::Preset &p, const QString &name, int number, dw::device::Kind kind, QString *err) {
    QByteArray pack;
    if (!uniflexPackBytes(name, &pack, err)) return false;
    if (number < 0 || number > 65535) { *err = "a UniFLEX file system number is 0 to 65535"; return false; }
    const qint64 secs = dw::now().toSecsSinceEpoch() - 315532800;    // seconds since 1980 GMT
    const QByteArray bytes = UniFlex::formatDisc(p.cylinders, p.heads, p.sectors, quint8(p.extra.value("density").toInt()),
                                                 quint8(p.extra.value("sides").toInt()), QByteArray(), pack, number,
                                                 secs < 0 ? 0 : quint32(secs));
    dw::device::RawLayout l;
    l.seclen = UniFlex::BlockSize;
    l.sectrk = 1;
    l.cyls = int(bytes.size() / UniFlex::BlockSize);
    l.heads = 1;
    l.firstId = 1;
    return writeNew(path, p, bytes, kind, err);
}

bool createBlank(const QString &path, const dw::fs::Preset &p, const QString &name, int number, const QDate &date,
                 const QByteArray &boot, QString *err, dw::device::Kind kind) {
    if (p.fsId == "uniflex") {
        if (!boot.isEmpty()) { *err = "a FLEX boot loader can't go on a UniFLEX disc"; return false; }
        return createUniflex(path, p, name, number, kind, err);
    }
    const int tracks = p.extra.value("tracks").toInt(), sectors = p.extra.value("sectors").toInt();
    const int track0 = p.extra.value("track0", sectors).toInt();
    QByteArray nameBytes;
    if (!volumeNameBytes(name, &nameBytes, err)) return false;
    Sir s;
    s.lastTrack = tracks - 1;
    s.sectors = sectors;
    const dw::device::RawLayout l = layoutOf(s);
    dw::device::Container img;
    img.createBlank(dw::device::Kind::Raw, l, QString("Made by FLEX Disk Wrangler, %1").arg(p.id));
    dw::device::Linear lin(&img, dw::device::SideOrder(), l.bytes());
    if (!Disk::makeBlank(&lin, tracks, sectors, track0, nameBytes, number, date, err)) return false;
    if (!boot.isEmpty()) {
        Disk d;
        if (!d.attach(lin, err) || !d.setBootSectors(boot, TS(), err)) return false;
    }
    QByteArray bytes;
    return lin.read(0, lin.size(), &bytes, err) && writeNew(path, p, bytes, kind, err);
}

bool readBootLoader(const QString &imagePath, QByteArray *out, QString *err) {
    dw::device::Container img;
    if (!img.open(imagePath, err)) return false;
    const dw::fs::Verdict v = verdict(img, imagePath);
    const dw::fs::Candidate *c = v.bestCandidate();
    if (!c || c->fsId != "flex") { *err = QFileInfo(imagePath).fileName() + " isn't a FLEX disc image"; return false; }
    const ProbeResult r = std::any_cast<ProbeResult>(c->data);
    qint64 bytes = 0;
    if (!flexLinear(&img, r, &bytes, err)) return false;
    Disk d;
    if (!d.attach(dw::device::Linear(&img, dw::device::SideOrder(), bytes), err)) return false;
    if (!d.hasLoader()) { *err = QFileInfo(imagePath).fileName() + " has no boot loader (its track 0 sector 1 is empty)"; return false; }
    return d.bootSectors(out, err);
}

// ------------------------------------------------------------------------------------------------ detection

QString containerPhrase(dw::device::Kind k) {
    switch (k) {
    case dw::device::Kind::Imd: return " in an ImageDisk file";
    case dw::device::Kind::Dsk: return " in a DSK file";
    case dw::device::Kind::Edsk: return " in an extended DSK file";
    case dw::device::Kind::Td0: return " in a TeleDisk file (read-only)";
    default: return QString();
    }
}

// FLEX asks the controller for a sector by its ID, numbering a cylinder's sectors on both sides as one track from 1
// (TSC's 6809 FLEX Adaptation Guide). Empty when every cylinder's IDs, side 0's then side 1's, run 1, 2, 3 ...; else
// how they are numbered. (The stored bytes are in that order whatever the IDs, so this is only said.)
static QString idNumbering(const dw::device::Container &c) {
    bool flex = true, eachSide = true;           // eachSide: every side numbered from 1 instead
    for (int cyl = 0; cyl < c.cylinders(); ++cyl) {
        int next = 1;
        for (int h = 0; h < c.heads(); ++h)
            for (int i = 0; i < c.sectorCount(cyl, h); ++i, ++next) {
                const int id = c.sector(cyl, h, i)->r;
                if (id != next) flex = false;
                if (id != i + 1) eachSide = false;
            }
    }
    if (flex) return QString();
    if (eachSide) return "each side's sectors are numbered from 1, where FLEX numbers side 1's on from side 0's";
    return "its sector IDs aren't numbered 1, 2, 3 ... on each cylinder as FLEX numbers them";
}

QVector<dw::fs::Candidate> FlexProbe::probe(const dw::fs::ProbeContext &ctx) const {
    if (!ctx.container) return {};
    const bool raw = ctx.container->kind() == dw::device::Kind::Raw;
    if (!raw && !ctx.container->ownTrack0()) return {};             // Flex::verdict turns it on
    const qint64 size = ctx.size();
    if (size < qint64(FirstDirSector) * SectorSize) return {};
    ProbeResult r;
    r.sir = Sir::parse(ctx.stored(qint64(SirSector - 1) * SectorSize, SectorSize));
    QString why;
    if (!sirPlausible(r.sir, size, &why, &r.exact)) return {};
    if (const int t0 = storedTrack0(r.sir, size))
        if (endsTrack0(ctx.stored(qint64(t0 - 1) * SectorSize, SectorSize))) {
            r.track0 = t0;
            r.exact = true;
        }
    // the directory's first sector must link to another directory sector on track 0, or end the chain
    const QByteArray dir = ctx.stored(qint64(FirstDirSector - 1) * SectorSize, SectorSize);
    const TS next{int(uchar(dir[0])), int(uchar(dir[1]))};
    const int lastOnTrack = next.t == 0 && r.track0 ? r.track0 : r.sir.sectors;
    const bool dirOk = next.isNull() || (next.t <= r.sir.lastTrack && next.s >= 1 && next.s <= lastOnTrack);
    if (!dirOk) return {};
    dw::fs::Candidate c;
    c.fsId = "flex";
    c.name = "FLEX";
    const QString label = r.sir.labelText();
    c.description = QString("FLEX disc%1, %2 tracks of %3 sectors").arg(label.isEmpty() ? QString() : " " + label)
                        .arg(r.sir.lastTrack + 1).arg(r.sir.sectors) + (r.track0 ? QString(" (track 0: %1)").arg(r.track0) : QString()) +
                    containerPhrase(ctx.container->kind());
    c.tier = r.exact ? dw::fs::Tier::Certain : dw::fs::Tier::Possible;
    c.evidence << QString("the SIR describes %1 tracks of %2 sectors").arg(r.sir.lastTrack + 1).arg(r.sir.sectors);
    const QString what = raw ? "the file's size" : "the disc in the file";
    if (r.track0) c.evidence << QString("which is exactly %1 with track 0 %2 its own %3 sectors "
                                        "(a double-density disc's single-density track 0), its directory ending there")
                                    .arg(what, raw ? "stored at" : "of").arg(r.track0);
    else if (r.exact) c.evidence << "which is exactly " + what;
    else c.notes << QString("the %1 %2 bytes; the SIR's geometry is %3").arg(raw ? "file is" : "disc in the file holds")
                      .arg(size).arg(qint64(r.sir.lastTrack + 1) * r.sir.sectors * SectorSize);
    if (!raw) {
        const QString ids = idNumbering(*ctx.container);
        if (ids.isEmpty()) c.evidence << "its sector IDs are numbered as FLEX numbers them";
        else c.notes << ids;
    }
    c.safeToWrite = r.exact;
    c.data = r;
    return {c};
}

static int u24(const QByteArray &b, int off) { return int(uchar(b[off])) << 16 | int(uchar(b[off + 1])) << 8 | int(uchar(b[off + 2])); }

QVector<dw::fs::Candidate> OtherSystemsProbe::probe(const dw::fs::ProbeContext &ctx) const {
    if (!ctx.container) return {};
    const qint64 size = ctx.size();
    const QByteArray lsn0 = ctx.stored(0, 256);
    QVector<dw::fs::Candidate> out;
    // OS-9 RBF: DD.TOT (sectors), DD.TKS, DD.MAP (bytes in the allocation map), DD.BIT (sectors per cluster), DD.DIR
    const int tot = u24(lsn0, 0), mapBytes = int(uchar(lsn0[4])) << 8 | uchar(lsn0[5]);
    const int bit = int(uchar(lsn0[6])) << 8 | uchar(lsn0[7]), dir = u24(lsn0, 8);
    if (tot > 0 && uchar(lsn0[3]) > 0 && bit > 0 && (bit & (bit - 1)) == 0 && mapBytes == (tot / bit + 7) / 8 && dir > 0 && dir < tot &&
        qAbs(qint64(tot) * 256 - size) <= 256 * 64) {
        QByteArray name = lsn0.mid(0x1F, 32);
        QString vol;
        for (char ch : name) { vol += QChar(uchar(ch) & 0x7F); if (uchar(ch) & 0x80) break; }
        dw::fs::Candidate c;
        c.fsId = "os9";
        c.name = "OS-9";
        c.description = QString("an OS-9 disc%1 (%2 sectors)").arg(vol.trimmed().isEmpty() ? QString() : " \"" + vol.trimmed() + "\"").arg(tot);
        c.tier = dw::fs::Tier::Likely;
        c.supported = false;
        c.evidence << "its first sector is an OS-9 identification sector";
        out << c;
    }
    return out;
}

dw::fs::Verdict verdict(dw::device::Container &img, const QString &path) {
    dw::fs::ProbeContext ctx;
    ctx.path = path;
    ctx.suffix = QFileInfo(path).suffix().toLower();
    ctx.container = &img;
    img.setOwnTrack0(true);          // a double-density disc's single-density track 0 (no effect on a raw file)
    const FlexProbe flex;
    const UniFlex::UniFlexProbe uniflex;
    const OtherSystemsProbe other;
    return dw::fs::detect(ctx, {&flex, &uniflex, &other});
}

} // namespace Flex
