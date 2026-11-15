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

static dw::device::RawLayout layoutOf(const Sir &s) {
    dw::device::RawLayout l;
    l.seclen = SectorSize;
    l.sectrk = s.sectors;                        // FLEX numbers a cylinder's sectors on both sides as one track
    l.cyls = s.lastTrack + 1;
    l.heads = 1;
    l.firstId = 1;
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
    m_checkOk = true;
    m_exact = true;
}

FlexVolume *OpenedDisc::volume() const {
    return m_disc.mounts().isEmpty() ? nullptr : static_cast<FlexVolume *>(m_disc.mounts()[0].vol.get());
}

bool OpenedDisc::attach(const ProbeResult &r, QString *err) {
    if (m_img.kind() != dw::device::Kind::Raw) { *err = "only raw FLEX images (.dsk) can be read so far"; return false; }
    const dw::device::RawLayout l = layoutOf(r.sir);
    if (!m_img.setRawLayout(l, err)) return false;
    detach();
    m_disc.setContainer(&m_img);
    auto disk = std::make_unique<Disk>();
    if (!disk->attach(dw::device::Linear(&m_img, dw::device::SideOrder(), l.bytes()), err)) return false;
    m_disk = std::move(disk);
    m_checkOk = m_disk->check().ok();
    m_exact = r.exact;
    m_disc.addMount({"FLEX", std::make_shared<FlexVolume>(m_disk.get()), false, QString(), "flex"});
    return true;
}

const char *const OpenedDisc::GeometryText = "the image file's size doesn't match the disc's geometry in its System Information Record";

QString OpenedDisc::readOnlyWhy(int mount) const {
    if (m_disc.mounts().isEmpty()) return "no disc";
    if (!m_checkOk) return "the disc has errors (Tools > Check disc)";
    if (!m_exact) return GeometryText;
    const dw::fs::Disc::Mount m = m_disc.mounts().value(mount);
    return m.readOnly ? m.readOnlyWhy : QString();
}

// The saved copy, before it replaces anything: the disc read from it again, checked, and holding as many files.
bool OpenedDisc::checkWritten(dw::device::Container &written, QString *err) const {
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
    if (m_disk) {
        const dw::fs::Report r = m_disk->check();
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

bool OpenedDisc::saveAs(const QString &path, dw::device::Kind kind, QString *err) {
    if (kind != dw::device::Kind::Raw) { *err = "FLEX discs are saved as raw images (.dsk) so far."; return false; }
    const bool ok = !m_disk || m_disk->check().ok();
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

// The shapes of the sample discs (docs/disc-format.md): FLEX numbers a cylinder's sectors on both sides as one track.
QVector<dw::fs::Preset> presets() {
    struct Shape { int tracks, sectors, heads; const char *what, *seen; };
    const Shape shapes[] = {
        {35, 10, 1, "5.25\" single-sided, single density, 35 tracks", "FLEX2_A, FLEX2_B, FL2_0SWT, FLEX2-01, ABASIC_1"},
        {40, 10, 1, "5.25\" single-sided, single density, 40 tracks", "SYS900, D_MAN_1, DMDR_SC1"},
        {35, 18, 1, "5.25\" single-sided, double density, 35 tracks", "EFORTH"},
        {40, 20, 2, "5.25\" double-sided, single density, 40 tracks", "DSTAR23"},
        {40, 36, 2, "5.25\" double-sided, double density, 40 tracks", "FLX505, FLX503TX"},
        {77, 15, 1, "8\" single-sided, single density, 77 tracks", "C009"},
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
        p.notes = QString("The shape of the sample discs %1.").arg(s.seen);
        p.extra["tracks"] = s.tracks;
        p.extra["sectors"] = s.sectors;
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

bool createBlank(const QString &path, const dw::fs::Preset &p, const QString &name, int number, const QDate &date,
                 const QByteArray &boot, QString *err) {
    const int tracks = p.extra.value("tracks").toInt(), sectors = p.extra.value("sectors").toInt();
    QByteArray nameBytes;
    if (!volumeNameBytes(name, &nameBytes, err)) return false;
    Sir s;
    s.lastTrack = tracks - 1;
    s.sectors = sectors;
    const dw::device::RawLayout l = layoutOf(s);
    dw::device::Container img;
    img.createBlank(dw::device::Kind::Raw, l, QString("Made by FLEX Disk Wrangler, %1").arg(p.id));
    dw::device::Linear lin(&img, dw::device::SideOrder(), l.bytes());
    if (!Disk::makeBlank(&lin, tracks, sectors, nameBytes, number, date, err)) return false;
    if (!boot.isEmpty()) {
        Disk d;
        if (!d.attach(lin, err) || !d.setBootSectors(boot, TS(), err)) return false;
    }
    return img.saveAs(path, dw::device::Kind::Raw, err);
}

bool readBootLoader(const QString &imagePath, QByteArray *out, QString *err) {
    dw::device::Container img;
    if (!img.open(imagePath, err)) return false;
    const dw::fs::Verdict v = verdict(img, imagePath);
    const dw::fs::Candidate *c = v.bestCandidate();
    if (!c || c->fsId != "flex") { *err = QFileInfo(imagePath).fileName() + " isn't a FLEX disc image"; return false; }
    const ProbeResult r = std::any_cast<ProbeResult>(c->data);
    const dw::device::RawLayout l = layoutOf(r.sir);
    if (!img.setRawLayout(l, err)) return false;
    Disk d;
    if (!d.attach(dw::device::Linear(&img, dw::device::SideOrder(), l.bytes()), err)) return false;
    if (!d.hasLoader()) { *err = QFileInfo(imagePath).fileName() + " has no boot loader (its track 0 sector 1 is empty)"; return false; }
    return d.bootSectors(out, err);
}

// ------------------------------------------------------------------------------------------------ detection

QVector<dw::fs::Candidate> FlexProbe::probe(const dw::fs::ProbeContext &ctx) const {
    if (!ctx.container || ctx.container->kind() != dw::device::Kind::Raw) return {};
    const qint64 size = ctx.size();
    if (size < qint64(FirstDirSector) * SectorSize) return {};
    ProbeResult r;
    r.sir = Sir::parse(ctx.stored(qint64(SirSector - 1) * SectorSize, SectorSize));
    QString why;
    if (!sirPlausible(r.sir, size, &why, &r.exact)) return {};
    // the directory's first sector must link to another directory sector on track 0, or end the chain
    const QByteArray dir = ctx.stored(qint64(FirstDirSector - 1) * SectorSize, SectorSize);
    const TS next{int(uchar(dir[0])), int(uchar(dir[1]))};
    const bool dirOk = next.isNull() || (next.t <= r.sir.lastTrack && next.s >= 1 && next.s <= r.sir.sectors);
    if (!dirOk) return {};
    dw::fs::Candidate c;
    c.fsId = "flex";
    c.name = "FLEX";
    const QString label = r.sir.labelText();
    c.description = QString("FLEX disc%1, %2 tracks of %3 sectors").arg(label.isEmpty() ? QString() : " " + label)
                        .arg(r.sir.lastTrack + 1).arg(r.sir.sectors);
    c.tier = r.exact ? dw::fs::Tier::Certain : dw::fs::Tier::Possible;
    c.evidence << QString("the SIR describes %1 tracks of %2 sectors").arg(r.sir.lastTrack + 1).arg(r.sir.sectors);
    if (r.exact) c.evidence << "which is exactly the file's size";
    else c.notes << QString("the file is %1 bytes; the SIR's geometry is %2").arg(size).arg(qint64(r.sir.lastTrack + 1) * r.sir.sectors * SectorSize);
    c.safeToWrite = r.exact;
    c.data = r;
    return {c};
}

static int u24(const QByteArray &b, int off) { return int(uchar(b[off])) << 16 | int(uchar(b[off + 1])) << 8 | int(uchar(b[off + 2])); }

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
    // UniFLEX: the SIR in block 1 (byte 512) holds the file system's name at 26 and the pack's at 40, 14 bytes each
    const QByteArray sir = ctx.stored(512, 64);
    if (out.isEmpty() && size >= 1024 && padded(sir.mid(26, 14)) && padded(sir.mid(40, 14))) {
        dw::fs::Candidate c;
        c.fsId = "uniflex";
        c.name = "UniFLEX";
        c.description = QString("a UniFLEX disc (\"%1\")").arg(QString::fromLatin1(sir.mid(26, 14)).remove(QChar('\0')));
        c.tier = dw::fs::Tier::Possible;
        c.supported = false;
        c.evidence << "block 1 names a UniFLEX file system and pack";
        out << c;
    }
    return out;
}

dw::fs::Verdict verdict(dw::device::Container &img, const QString &path) {
    dw::fs::ProbeContext ctx;
    ctx.path = path;
    ctx.suffix = QFileInfo(path).suffix().toLower();
    ctx.container = &img;
    const FlexProbe flex;
    const OtherSystemsProbe other;
    return dw::fs::detect(ctx, {&flex, &other});
}

} // namespace Flex
