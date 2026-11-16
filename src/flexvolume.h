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
// image as the window's Document and the console program's opened image alike (OpenedDisc: a FLEX disc, or a UniFLEX
// one, through uniflexvolume.h), detection by tiers (the FLEX and UniFLEX probes, and a recogniser that
// names the OS-9 discs this program can't open), and who this program is (appInfo, self). QtCore only: the console
// program uses all of it.
#pragma once

#include <memory>

#include <QDateTime>

#include <diskwrangler/app/appinfo.h>
#include <diskwrangler/app/document.h>
#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/disc.h>
#include <diskwrangler/fs/presets.h>
#include <diskwrangler/fs/volume.h>

#include "flexdisk.h"
#include "uniflexvolume.h"

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
// label and the volume number. Changes are FLEX's (Flex::Disk): a file written keeps the date and protection it brings
// (as FLEX's COPY does), today's date when it brings none; the random-access flag comes with a file and can't be
// changed afterwards (it decides whether the file has a map); a protected file can't be deleted, renamed or replaced.
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

private:
    const File *find(const dw::fs::Path &p, QString *err) const;
    bool fileName(const dw::fs::Path &dir, const QString &name, QString *out, QString *err) const;
    Disk *m_d;
    NameRules m_names;
};

// What the FLEX probe found, for the program to open the disc with (Candidate::data).
struct ProbeResult {
    Sir sir;
    bool exact = false;                          // the SIR's geometry is exactly the file's size
    int track0 = 0;                              // track 0 stored short, with this many sectors (0: a full track)
};

// The opened image: the container and the volume on it, one mount: "FLEX" (group "flex"), or "UniFLEX" (group
// "uniflex"). It is the window's Document and the console program's opened image alike, so both save
// through the same checks.
class OpenedDisc : public dw::app::Document {
public:
    OpenedDisc() = default;
    OpenedDisc(const OpenedDisc &) = delete;
    OpenedDisc &operator=(const OpenedDisc &) = delete;

    // Opens the file and attaches the reading detection found (the console program's open).
    bool open(const QString &path, const ProbeResult &r, QString *err);
    bool open(const QString &path, const UniFlex::ProbeResult &r, QString *err);
    // The window's open: the container opened and its state noted, no reading attached yet.
    bool load(const QString &path, QString *err);
    bool attach(const ProbeResult &r, QString *err);
    // A UniFLEX disc: the file as 512-byte blocks; read-only when it has errors or is shorter than its volume.
    bool attach(const UniFlex::ProbeResult &r, QString *err);
    bool writableGeometry() const { return m_exact; }
    bool checkOk() const { return m_checkOk; }                       // the disc's check found no errors when it opened
    static const char *const GeometryText;       // why a disc whose file isn't its geometry's size can't be written
    void detach();
    void noteFileState();

    dw::fs::Disc &disc() override { return m_disc; }
    const dw::fs::Disc &disc() const override { return m_disc; }
    dw::device::Container &container() { return m_img; }
    const dw::device::Container &container() const { return m_img; }
    Disk *disk() { return m_disk.get(); }                            // the FLEX disc, or null
    const Disk *disk() const { return m_disk.get(); }
    FlexVolume *volume() const;                                      // its volume, or null
    UniFlex::Fs *uniflex() { return m_ufs.get(); }                   // the UniFLEX disc, or null
    const UniFlex::Fs *uniflex() const { return m_ufs.get(); }

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
    std::unique_ptr<UniFlex::Fs> m_ufs;
    bool m_checkOk = true;
    bool m_exact = true;                         // the SIR's geometry is exactly the file's size
    QDateTime m_fileTime;
    qint64 m_fileSize = -1;
    dw::fs::Disc m_disc;
};

// FLEX Disk Wrangler as a program of the family (the core names only the first two; this one names itself).
dw::fs::OtherApp self();
dw::app::AppInfo appInfo(const QString &version);

// ----- new discs
// The presets: the shapes of the sample discs and TSC's formats (the 6809 FLEX Adaptation Guide, Appendix B;
// docs/disc-format.md), source "flex", each with extra "tracks", "sectors" and "track0" (track 0's sectors: fewer on a
// double-density disc, whose track 0 is single density). The image is always the full geometry, a short track 0
// padded as the sample EFORTH's is. Then the UniFLEX ones (fsId "uniflex"): TSC's four 8" floppy formats, extra
// "density" and "sides" the SIR's flags. addPresets puts them in the core's registry (once); presets() gives them in order.
void addPresets();
QVector<dw::fs::Preset> presets();
const dw::fs::Preset *findPreset(const QString &id);
// A volume name as stored: up to 8 printable ASCII characters (DISKNAME's rule: what is typed, kept), or why not.
bool volumeNameBytes(const QString &name, QByteArray *out, QString *err);
// A UniFLEX pack name: up to 14 characters, Latin-1, no NUL.
bool uniflexPackBytes(const QString &name, QByteArray *out, QString *err);
// A blank raw image at path (replacing anything there): the preset's shape laid out as NEWDISK does, the name, number
// and date in its SIR; with boot (sectors 1 and 2 of another disc, 512 bytes) its loader copied in, not linked. A
// UniFLEX preset: laid out as TSC's formatter does (UniFlex::formatDisc), name the pack name, number the file system
// number, the time now (the core's clock); no boot.
bool createBlank(const QString &path, const dw::fs::Preset &p, const QString &name, int number, const QDate &date,
                 const QByteArray &boot, QString *err);
// The boot loader of another FLEX disc image (sectors 1 and 2): refused if it isn't a FLEX disc or sector 1 is empty.
bool readBootLoader(const QString &imagePath, QByteArray *out, QString *err);

// ----- detection by tiers
// A FLEX disc in a raw file: the SIR's geometry fits the file and the directory can be read. Certain when the
// geometry is exactly the file's size and the directory's chain starts on the disc; Possible when the file is a
// little longer or shorter than the geometry. A double-density disc's single-density track 0 stored at its own size
// (storedTrack0, confirmed by endsTrack0) is exact too. data is a ProbeResult.
class FlexProbe : public dw::fs::Probe {
public:
    QString id() const override { return "flex"; }
    QVector<dw::fs::Candidate> probe(const dw::fs::ProbeContext &ctx) const override;
};

// Discs of another 6809 system that turn up beside FLEX ones, recognised so that the message says what they are:
// OS-9 (RBF: its identification sector, LSN 0). Not supported; no program of the family owns them. (UniFLEX discs have
// their own probe, uniflexvolume.h.)
class OtherSystemsProbe : public dw::fs::Probe {
public:
    QString id() const override { return "other"; }
    QVector<dw::fs::Candidate> probe(const dw::fs::ProbeContext &ctx) const override;
};

// Runs the probes on an opened container: FLEX, UniFLEX, OS-9.
dw::fs::Verdict verdict(dw::device::Container &img, const QString &path);

} // namespace Flex
