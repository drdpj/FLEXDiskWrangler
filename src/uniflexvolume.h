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
// uniflexvolume.h - a UniFLEX disc as a dw::fs::Volume (UniFlexVolume: read in series uniflex-1, written in uniflex-2) and its probe
// (UniFlexProbe). QtCore only: the console program uses it too.
#pragma once

#include <diskwrangler/fs/detection.h>
#include <diskwrangler/fs/volume.h>

#include "uniflexfs.h"

namespace UniFlex {

// UniFLEX names: 1-14 bytes, any but NUL and '/', case kept (and significant).
class NameRules : public dw::fs::NameRules {
public:
    bool valid(const QString &name, dw::fs::NodeKind kind, QString *why) const override;
    QString fromHost(const QString &hostName, dw::fs::NodeKind kind) const override;
    Qt::CaseSensitivity caseSensitivity() const override { return Qt::CaseSensitive; }
};

// The tree from the root. Attributes: the seven access bits (retro-files family "uniflex", docs/retro-files-uniflex.md)
// and the owner's user id travel with a file; the type (a device's numbers), links and fdn number are shown. The
// date is the fdn's time of last change. Volume attributes: the pack name, the file system name and number. Changes
// are made as UniFLEX makes them (uniflexwrite.h); a file brought in gets the time of the change, as UniFLEX's copy
// gives it, and keeps the access and owner it brings.
class UniFlexVolume : public dw::fs::Volume {
public:
    explicit UniFlexVolume(Fs *fs) : m_fs(fs) {}
    Fs *fs() const { return m_fs; }

    QString fsId() const override { return "uniflex"; }
    dw::fs::Caps caps() const override;
    QVector<dw::fs::AttrSpec> fileAttrs() const override;
    QVector<dw::fs::AttrSpec> volumeAttrs() const override;
    dw::fs::AttrValues volumeValues() const override;
    const dw::fs::NameRules &names() const override { return m_names; }
    dw::fs::VolumeInfo info() const override;
    bool marksInexactSizes() const override { return false; }

    bool list(const dw::fs::Path &dir, QVector<dw::fs::Entry> *out, QString *err) const override;
    bool stat(const dw::fs::Path &node, dw::fs::Entry *out, QString *err) const override;
    bool read(const dw::fs::Path &file, QByteArray *out, QString *err) const override;
    bool write(const dw::fs::Path &dir, const QString &name, const QByteArray &data, const dw::fs::AttrValues &attrs,
               const QDateTime &when, bool replace, QString *err) override;
    bool mkdir(const dw::fs::Path &dir, const QString &name, QString *err) override;
    bool remove(const dw::fs::Path &node, QString *err) override;
    bool rename(const dw::fs::Path &from, const dw::fs::Path &to, QString *err) override;
    bool setAttrs(const dw::fs::Path &node, const dw::fs::AttrValues &a, QString *err) override;
    bool setVolumeAttrs(const dw::fs::AttrValues &a, QString *err) override;
    dw::fs::Report check() const override { return m_fs->check(); }
    bool reload(QString *err) override { return m_fs->reload(err); }

    dw::fs::Entry entryOf(int node) const;

private:
    int node(const dw::fs::Path &p, QString *err) const;
    Fs *m_fs;
    NameRules m_names;
};

// What the probe found, for the program to open the disc with (Candidate::data).
struct ProbeResult {
    Sir sir;
    qint64 fileBytes = 0;
    bool exact = false;                          // the file holds exactly the SIR's volume and swap space
};

// A UniFLEX disc: a SIR in block 1 whose fdn list, volume, counts and lists are UniFLEX's, and an fdn 1
// that is a directory. Certain when its first entry is "." naming itself and the file is exactly the SIR's volume and
// swap space; Likely when the file holds at least the volume; Possible when it is cut short, or the root directory
// doesn't start with "." (a disc whose root was never written: it opens, and the check says so). A SIR that names a
// file system and a pack but describes none is named, unsupported (Possible). In an ImageDisk, DSK or TeleDisk file of
// 512-byte sectors the same tests run on its stored bytes ("the file" is then the disc in it). data is a ProbeResult.
class UniFlexProbe : public dw::fs::Probe {
public:
    QString id() const override { return "uniflex"; }
    QVector<dw::fs::Candidate> probe(const dw::fs::ProbeContext &ctx) const override;
};

} // namespace UniFlex
