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
#include "backend.h"

#include <diskwrangler/app/clock.h>

#include "../flexvolume.h"
#include "../hostpolicy.h"

using namespace dw::cli;

namespace FlexCli {

namespace {

class Image : public OpenedImage {
public:
    Flex::OpenedDisc od;
    Flex::ProbeResult probe;
    UniFlex::ProbeResult ufProbe;

    dw::fs::Disc &disc() override { return od.disc(); }
    bool save(bool backup, QString *err) override { return od.save(backup, err); }
    QString volName(int) const override { return od.uniflex() ? "uniflex" : "flex"; }
    QJsonObject app() const override {
        if (const UniFlex::Fs *fs = od.uniflex()) return uniflexApp(*fs);
        QJsonObject o;
        const Flex::Sir &s = probe.sir;
        o["label"] = s.labelText();
        o["volumeNumber"] = s.volumeNumber;
        o["tracks"] = s.lastTrack + 1;
        o["sectorsPerTrack"] = s.sectors;
        o["freeSectors"] = s.freeCount;
        const QDate d = s.date();
        o["date"] = d.isValid() ? d.toString(Qt::ISODate) : QString();
        o["exactSize"] = probe.exact;
        o["track0Stored"] = probe.track0 ? probe.track0 : s.sectors;   // track 0's sectors in the image
        if (const Flex::Disk *disk = od.disk()) {
            o["directorySectors"] = disk->dirSectors().size();
            o["deletedEntries"] = disk->deletedEntries();
        }
        return o;
    }
    QJsonObject uniflexApp(const UniFlex::Fs &fs) const {
        const UniFlex::Sir &s = fs.sir();
        QJsonObject o;
        auto time = [](quint32 t) { const QDateTime d = UniFlex::timeOf(t); return d.isValid() ? d.toString(Qt::ISODate) : QString(); };
        o["packName"] = s.packNameText();
        o["fsName"] = s.fsNameText();
        o["fsNumber"] = s.number;
        o["created"] = time(s.created);
        o["updated"] = time(s.updated);
        o["volumeBlocks"] = s.volumeBlocks();
        o["fdnBlocks"] = s.fdnBlocks;
        o["fdns"] = s.fdnCount();
        o["freeBlocks"] = s.freeBlocks;
        o["freeFdns"] = s.freeFdns;
        o["swapStart"] = s.swapStart;
        o["swapBlocks"] = s.swapSize;
        o["winchester"] = s.winchester();
        o["density"] = s.density;
        o["sides"] = s.sides;
        o["imageBlocks"] = fs.imageBlocks();
        o["exactSize"] = ufProbe.exact;
        o["files"] = fs.fileCount();
        o["directories"] = fs.dirCount() + 1;
        return o;
    }
    // The check's errors are flexdw's own (exit 4); a file whose size isn't its SIR's geometry can't be written, nor
    // can a UniFLEX disc (read-only in this version)
    QString readOnlyWhy() const override {
        if (od.uniflex()) return UniFlex::UniFlexVolume::ReadOnlyText;
        return od.writableGeometry() ? QString() : Flex::OpenedDisc::GeometryText;
    }
};

} // namespace

dw::fs::OtherApp Backend::self() const { return Flex::self(); }

OpenOutcome Backend::open(const QString &path, const OpenOptions &o) {
    OpenOutcome out;
    QString err;
    dw::device::Container c;
    if (!c.open(path, &err)) { out.status = OpenOutcome::Failed; out.message = path + ": " + err; return out; }
    out.container = containerInfo(path, c);
    const dw::fs::Verdict v = Flex::verdict(c, path);
    resolveVerdict(v, o.fs, &out);
    if (out.status != OpenOutcome::Ok) {
        if (out.status == OpenOutcome::Unrecognised && out.message.isEmpty()) out.message = "not a FLEX or UniFLEX disc";
        return out;
    }
    const dw::fs::Candidate &cand = v.ranked[out.chosen];
    if (!cand.supported) { out.status = OpenOutcome::Unsupported; out.message = "this is " + cand.description + ": FLEX Disk Wrangler can't open it"; return out; }
    std::unique_ptr<Image> img(new Image);
    bool opened;
    if (cand.fsId == "uniflex") {
        img->ufProbe = std::any_cast<UniFlex::ProbeResult>(cand.data);
        opened = img->od.open(path, img->ufProbe, &err);
    } else {
        img->probe = std::any_cast<Flex::ProbeResult>(cand.data);
        opened = img->od.open(path, img->probe, &err);
    }
    if (!opened) { out.status = OpenOutcome::Failed; out.message = err; return out; }
    out.image = std::move(img);
    return out;
}

QJsonArray Backend::formats() const {
    QJsonArray a;
    for (const dw::fs::Preset &p : Flex::presets()) {
        QJsonObject o;
        o["id"] = p.id;
        o["name"] = p.name;
        o["description"] = p.notes;
        o["tracks"] = p.extra.value("tracks").toInt();
        o["sectorsPerTrack"] = p.extra.value("sectors").toInt();
        o["track0Sectors"] = p.extra.value("track0").toInt();            // the directory ends at its last sector
        o["doubleSided"] = p.doubleSided;
        o["bytes"] = qint64(p.bytes);
        a.append(o);
    }
    return a;
}

// A blank disc as NEWDISK lays one out (Flex::createBlank), named by --title, number 0, dated today; no boot loader.
bool Backend::create(const QString &file, const NewOptions &n, QString *err, int *exit) {
    const dw::fs::Preset *p = Flex::findPreset(n.format);
    if (!p) { *err = QString("unknown format '%1' (flexdw new --list)").arg(n.format); *exit = ExitUsage; return false; }
    QByteArray name;
    if (!Flex::volumeNameBytes(n.title, &name, err)) { *exit = ExitUsage; return false; }
    if (!Flex::createBlank(file, *p, n.title, 0, dw::now().date(), QByteArray(), err)) { *exit = ExitSave; return false; }
    return true;
}

std::shared_ptr<dw::xfer::HostPolicy> Backend::hostPolicy(const GetOptions &, QString *, int *) {
    return std::make_shared<Flex::HostPolicy>();
}

bool Backend::readHost(const QStringList &paths, const PutOptions &, QVector<dw::TransferItem> *items, QStringList *, QString *err, int *exit) {
    QVector<dw::xfer::HostReadError> errors;
    const QVector<dw::TransferItem> got = dw::xfer::readHost(paths, Flex::hostRead(), &errors);
    QVector<dw::xfer::HostReadError> reading, tooLarge;
    for (const dw::xfer::HostReadError &e : errors) (e.kind == dw::xfer::HostReadError::TooLarge ? tooLarge : reading) << e;
    if (hostReadFailed(reading, err, exit)) return false;
    if (!tooLarge.isEmpty()) { *err = tooLarge.first().path + ": too large for a FLEX disc"; *exit = ExitFailed; return false; }
    *items += got;
    return true;
}

} // namespace FlexCli
