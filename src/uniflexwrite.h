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
// uniflexwrite.h - changing a UniFLEX disc as TSC's 6809 kernel and utilities change it (series uniflex-2):
// docs/uniflex-format.md, "How UniFLEX writes", has every rule and where it comes from. The kernel's routines are
// followed step by step (aldb, frdb / mkfch, alfdn / filfdn, frfdn, mapfil, rmvfil, crfil, link, unlink, update), so
// blocks and fdns are taken and freed in UniFLEX's order and the free lists end as UniFLEX leaves them;
// tests/uniflex_check.py's Kernel is the independent model the tests hold this to, byte for byte.
//
// Writes go to the disc's Linear (staged in its container: nothing reaches the file until a save). A change that fails
// part way returns false with what it has staged; the caller's transaction (dw::fs::Disc::transact) undoes it. Every
// change ends by reading the disc again (Fs::reload). QtCore only.
#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "uniflexfs.h"

namespace UniFlex {

// A path from the root as the names' bytes ({} is the root).
using BytePath = QList<QByteArray>;

// The access a file brought in from the host gets when it brings none: rw-rw-, as UniFLEX's shell creates a file
// (seen in UniFLEX 3.14A: `echo hi >file`).
const quint8 DefaultFileAccess = OwnerRead | OwnerWrite | OthersRead | OthersWrite;

class Writer {
public:
    // now: the time of the change, seconds since 1980 GMT (every fdn written and the SIR get it, as UniFLEX's do).
    Writer(Fs *fs, quint32 now) : m_fs(fs), m_now(now) {}

    // copy / create: a new file in dir, or (replace) the file of that name emptied and written again, its fdn, access
    // and owner kept, as UniFLEX's create keeps them. access: the seven access bits; owner: the user id.
    bool writeFile(const BytePath &dir, const QByteArray &name, const QByteArray &data, quint8 access, int owner,
                   bool replace, QString *err);
    // crdir: the directory, then its . and .. (owner: the system manager).
    bool makeDir(const BytePath &dir, const QByteArray &name, QString *err);
    // kill: a file, or a directory with everything in it (depth first, in the directory's order; each directory as
    // kill +d removes it once empty).
    bool remove(const BytePath &path, QString *err);
    // rename / move: link to the new name (a file of that name removed first), then unlink the old. A directory
    // moved to another directory then has its .. unlinked and the new parent linked as its .. (UniFLEX's own rename
    // and move refuse directories; the kernel lets the system manager do this with link and unlink). Never into
    // itself or a directory inside it.
    bool rename(const BytePath &from, const BytePath &toDir, const QByteArray &toName, QString *err);
    // perms (the seven bits; "execute only", $80, kept) and owner.
    bool setAccess(const BytePath &path, quint8 access, QString *err);
    bool setOwner(const BytePath &path, int owner, QString *err);
    // The SIR's names (14 bytes, NUL-padded) and file system number. No UniFLEX utility changes them on a made disc;
    // the program writes the fields and the SIR's update time.
    bool setNames(const QByteArray &fsName, const QByteArray &packName, int number, QString *err);

private:
    struct Entry { int fdn = 0; QByteArray name; };     // name: 14 bytes as stored

    // the disc
    bool rd(qint64 block, QByteArray *out, QString *err) const;
    bool wr(qint64 block, int off, const QByteArray &bytes, QString *err);
    bool loadSir(QString *err);
    bool flushSir(QString *err);                         // update: the SIR with the time now, bytes 0-3 zero
    qint64 sir3(int off) const;
    int sir2(int off) const;
    int sir1(int off) const { return quint8(m_sir[off]); }
    void setSir3(int off, qint64 v);
    void setSir2(int off, int v);
    void setSir1(int off, int v) { m_sir[off] = char(v); }

    // blocks
    bool aldb(qint64 *b, QString *err);
    bool frdb(qint64 b, QString *err);
    // fdns
    bool getFdn(int n, Fdn *f, QString *err) const;
    bool putFdn(const Fdn &f, QString *err);             // updfdn: with the time now
    bool filfdn(QString *err);
    bool alfdn(int *n, QString *err);
    bool frfdn(int n, QString *err);
    // a file's blocks
    bool ind(qint64 block, int k, qint64 *out, QString *err) const;
    bool mapRead(const Fdn &f, qint64 i, qint64 *b, QString *err) const;
    bool mapWrite(Fdn *f, qint64 i, qint64 *b, QString *err);
    bool writeAt(Fdn *f, qint64 pos, const QByteArray &data, QString *err);
    bool frind(qint64 b, int level, QString *err);
    bool rmvfil(Fdn *f, QString *err);
    // directories
    bool entries(int dir, QVector<Entry> *out, QString *err) const;
    bool search(int dir, const QByteArray &name, int *slot, int *firstFree, QString *err) const;
    bool lookup(const BytePath &path, int *dir, int *fdn, QString *err) const;   // fdn 0: no such name
    bool enter(int dir, const QByteArray &name, int fdn, QString *err);
    // the system calls
    bool crfil(int dir, const QByteArray &name, quint8 mode, quint8 access, int owner, int *n, QString *err);
    bool link(int fdn, int dir, const QByteArray &name, QString *err);
    bool unlink(int dir, const QByteArray &name, QString *err);
    bool removeIn(int dir, const QByteArray &name, QString *err);
    bool finish(bool ok, QString *err);

    Fs *m_fs;
    quint32 m_now;
    QByteArray m_sir;
};

// A name as stored: up to 14 bytes, NUL-padded.
QByteArray storedName(const QByteArray &name);

// A new disc as TSC's formatter lays one out (unibug/format 1.09 with its 8" floppy part formatfd, UniFLEX's
// /etc/formatfd: docs/uniflex-format.md, "A new disc"): cylinders x heads x sectors blocks; a 33rd of them (rounded
// down: format's 3%, 100 / 3) for fdns; no swap space; the SIR (both times now, the names, the density and side flags), the
// root and /.badblocks (both timed now), the root's one block, the free lists. Block 0, where format writes TSC's
// bootstrap, is left empty: the program carries no TSC code.
QByteArray formatDisc(int cylinders, int heads, int sectors, quint8 density, quint8 sides, const QByteArray &fsName,
                      const QByteArray &packName, int number, quint32 now);

} // namespace UniFlex
