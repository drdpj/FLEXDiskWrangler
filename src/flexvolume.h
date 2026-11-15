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
// flexvolume.h - FLEX Disk Wrangler on DiskWranglerCore: a FLEX disc as a dw::fs::Volume (FlexVolume), the opened
// image as the window's Document and the console program's opened image alike (OpenedDisc), detection by tiers (the
// FLEX probe, and recognisers that name the OS-9 and UniFLEX discs this program can't open), and who this program is
// (appInfo, self). QtCore only: the console program uses all of it.
#pragma once

#include <memory>

#include <QDateTime>

#include <diskwrangler/app/appinfo.h>
#include <diskwrangler/app/document.h>
#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/volume.h>

#include "flexdisk.h"

namespace Flex {

// FLEX file names: a name of 1-8 characters and an extension of up to 3, letters, digits, '-' and '_', the name
// starting with a letter. Upper case on the disc.
class NameRules : public dw::fs::NameRules {
public:
    bool valid(const QString &name, dw::fs::NodeKind kind, QString *why) const override;
    QString fromHost(const QString &hostName, dw::fs::NodeKind kind) const override;
};

// One flat directory. Attributes: the four protection bits and the random-access flag (retro-files family "flex",
// docs/retro-files-flex.md) and the size in sectors; the date is the entry's modified date. Volume attributes: the
// label and the volume number. Series flex-1: every change is refused.
class FlexVolume : public dw::fs::Volume {
public:
    explicit FlexVolume(Disk *d) : m_d(d) {}
    Disk *disk() const { return m_d; }

    QString fsId() const override { return "flex"; }
    dw::fs::Caps caps() const override;
    QVector<dw::fs::AttrSpec> fileAttrs() const override;
    QVector<dw::fs::AttrSpec> volumeAttrs() const override;
    dw::fs::AttrValues volumeValues() const override;
    const dw::fs::NameRules &names() const override { return m_names; }
    dw::fs::VolumeInfo info() const override;

    bool list(const dw::fs::Path &dir, QVector<dw::fs::Entry> *out, QString *err) const override;
    bool read(const dw::fs::Path &file, QByteArray *out, QString *err) const override;
    bool write(const dw::fs::Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &attrs,
               const QDateTime &when, bool replace, QString *err) override;
    bool remove(const dw::fs::Path &node, QString *err) override;
    bool rename(const dw::fs::Path &from, const dw::fs::Path &to, QString *err) override;
    bool setAttrs(const dw::fs::Path &node, const dw::fs::AttrValues &a, QString *err) override;
    bool setVolumeAttrs(const dw::fs::AttrValues &a, QString *err) override;
    dw::fs::Report check() const override { return m_d->check(); }
    bool reload(QString *err) override { return m_d->reload(err); }

    static dw::fs::Entry entryOf(const File &f);
    static const char *const ReadOnlyText;       // why every change is refused in this version

private:
    const File *find(const dw::fs::Path &p, QString *err) const;
    Disk *m_d;
    NameRules m_names;
};

// What the FLEX probe found, for the program to open the disc with (Candidate::data).
struct ProbeResult {
    Sir sir;
    bool exact = false;                          // the SIR's geometry is exactly the file's size
};

// The opened image: the container and the FLEX volume on it (one mount, "FLEX"). It is the window's Document and
// the console program's opened image alike, so both save through the same checks.
class OpenedDisc : public dw::app::Document {
public:
    OpenedDisc() = default;
    OpenedDisc(const OpenedDisc &) = delete;
    OpenedDisc &operator=(const OpenedDisc &) = delete;

    // Opens the file and attaches the reading detection found (the console program's open).
    bool open(const QString &path, const ProbeResult &r, QString *err);
    // The window's open: the container opened and its state noted, no reading attached yet.
    bool load(const QString &path, QString *err);
    bool attach(const ProbeResult &r, QString *err);
    void detach();
    void noteFileState();

    dw::fs::Disc &disc() override { return m_disc; }
    const dw::fs::Disc &disc() const override { return m_disc; }
    dw::device::Container &container() { return m_img; }
    const dw::device::Container &container() const { return m_img; }
    Disk *disk() { return m_disk.get(); }
    const Disk *disk() const { return m_disk.get(); }
    FlexVolume *volume() const;

    bool isOpen() const override { return m_img.isOpen(); }
    QString path() const override { return m_img.path(); }
    bool dirty() const override { return m_img.hasPending(); }
    bool save(bool backup, QString *err) override;
    bool saveAs(const QString &path, dw::device::Kind kind, QString *err) override;
    QString readOnlyWhy(int mount) const override;

private:
    bool checkWritten(dw::device::Container &written, QString *err) const;
    dw::device::Container m_img;
    std::unique_ptr<Disk> m_disk;
    bool m_checkOk = true;
    QDateTime m_fileTime;
    qint64 m_fileSize = -1;
    dw::fs::Disc m_disc;
};

// FLEX Disk Wrangler as a program of the family (the core names only the first two; this one names itself).
dw::fs::OtherApp self();
dw::app::AppInfo appInfo(const QString &version);

// ----- detection by tiers
// A FLEX disc in a raw file: the SIR's geometry fits the file and the directory can be read. Certain when the
// geometry is exactly the file's size and the directory's chain starts on the disc; Possible when the file is a
// little longer or shorter than the geometry. data is a ProbeResult.
class FlexProbe : public dw::fs::Probe {
public:
    QString id() const override { return "flex"; }
    QVector<dw::fs::Candidate> probe(const dw::fs::ProbeContext &ctx) const override;
};

// Discs of the other 6809 systems that turn up beside FLEX ones, recognised so that the message says what they are:
// OS-9 (RBF: its identification sector, LSN 0) and UniFLEX (its SIR in block 1 names the file system and the pack).
// Not supported; no program of the family owns them.
class OtherSystemsProbe : public dw::fs::Probe {
public:
    QString id() const override { return "other"; }
    QVector<dw::fs::Candidate> probe(const dw::fs::ProbeContext &ctx) const override;
};

// Runs the probes on an opened container.
dw::fs::Verdict verdict(dw::device::Container &img, const QString &path);

} // namespace Flex
