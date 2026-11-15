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

#include "../flexvolume.h"
#include "../hostpolicy.h"

using namespace dw::cli;

namespace FlexCli {

namespace {

class Image : public OpenedImage {
public:
    Flex::OpenedDisc od;
    Flex::ProbeResult probe;

    dw::fs::Disc &disc() override { return od.disc(); }
    bool save(bool backup, QString *err) override { return od.save(backup, err); }
    QString volName(int) const override { return "flex"; }
    QJsonObject app() const override {
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
        if (const Flex::Disk *disk = od.disk()) {
            o["directorySectors"] = disk->dirSectors().size();
            o["deletedEntries"] = disk->deletedEntries();
        }
        return o;
    }
    QString readOnlyWhy() const override { return QString(); }
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
        if (out.status == OpenOutcome::Unrecognised && out.message.isEmpty()) out.message = "not a FLEX disc";
        return out;
    }
    const dw::fs::Candidate &cand = v.ranked[out.chosen];
    if (!cand.supported) { out.status = OpenOutcome::Unsupported; out.message = "this is " + cand.description + ": FLEX Disk Wrangler can't open it"; return out; }
    std::unique_ptr<Image> img(new Image);
    img->probe = std::any_cast<Flex::ProbeResult>(cand.data);
    if (!img->od.open(path, img->probe, &err)) { out.status = OpenOutcome::Failed; out.message = err; return out; }
    out.image = std::move(img);
    return out;
}

bool Backend::create(const QString &, const NewOptions &, QString *err, int *exit) {
    *err = "this version of flexdw can't make new FLEX discs yet";
    *exit = ExitUnsupported;
    return false;
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
