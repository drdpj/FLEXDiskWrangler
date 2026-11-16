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
// uniflexfs.h - the UniFLEX filesystem (TSC's Unix-like system for the 6809): the System Information Record, the file
// descriptor nodes (fdns), the directory tree, each file's block map and the free lists, read from a disc seen as one
// run of 512-byte blocks (a dw::device::Linear). QtCore only; no window, no core interface (uniflexvolume.h adapts
// it). docs/uniflex-format.md has the layout and where each fact comes from (TSC's kernel and formatter sources).
//
// Series uniflex-1 reads: nothing here writes to the disc.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QVector>

#include <diskwrangler/device/linear.h>
#include <diskwrangler/fs/volume.h>

namespace UniFlex {

const int BlockSize = 512;
const int SirBlock = 1;
const int FirstFdnBlock = 2;
const int FdnSize = 64;
const int FdnsPerBlock = BlockSize / FdnSize;
const int MapEntries = 13;                   // 10 direct, then single, double and triple indirect
const int DirectBlocks = 10;
const int PerIndirect = 128;                 // 3-byte block numbers in bytes 0-383 of an indirect block
const int DirEntrySize = 16;
const int NameLength = 14;
const int SirFreeMax = 100;                  // CDBLKS: free blocks listed in the SIR, and in each list block
const int SirFdnsMax = 50;                   // CFDN: free fdns listed in the SIR
const int RootFdn = 1;

// fmode (structures/fdn): 0 is a free fdn.
enum Mode : quint8 { Busy = 0x01, BlockDevice = 0x02, CharDevice = 0x04, Directory = 0x08 };
// facces
enum Access : quint8 {
    OwnerRead = 0x01, OwnerWrite = 0x02, OwnerExecute = 0x04,
    OthersRead = 0x08, OthersWrite = 0x10, OthersExecute = 0x20, SetUserId = 0x40
};

// A name or path for a message: control characters shown as \xNN.
QString printable(const QString &s);

// UniFLEX time: seconds since 1 January 1980, 00:00 GMT. 0 is no time (an invalid QDateTime).
QDateTime timeOf(quint32 seconds);

// The System Information Record, block 1 (structures/sir).
struct Sir {
    quint32 created = 0, updated = 0;        // scrtim, sutime
    int fdnBlocks = 0;                       // sszfdn
    qint64 lastBlock = 0;                    // ssizfr: the volume's last block number
    qint64 freeBlocks = 0;                   // sfreec
    int freeFdns = 0;                        // sfdnc
    QByteArray fsName, packName;             // sfname, spname: 14 bytes each as stored
    int number = 0;                          // sfnumb
    int flawed = 0;                          // sflawc
    quint8 density = 0, sides = 0;           // sdenf ($FF: a Winchester), ssidf
    qint64 swapStart = 0;                    // sswpbg
    int swapSize = 0;                        // sswpsz
    int listedFdnCount = 0;                  // snfdn
    QVector<int> listedFdns;                 // scfdn, the first min(snfdn, 50)
    int listedFreeCount = 0;                 // snfree
    QVector<qint64> listedFree;              // sfree, the first min(snfree, 100)

    static Sir parse(const QByteArray &block);
    int fdnCount() const { return fdnBlocks * FdnsPerBlock; }
    qint64 firstDataBlock() const { return FirstFdnBlock + fdnBlocks; }
    qint64 volumeBlocks() const { return lastBlock + 1; }
    // The blocks a full image holds: the volume, and the swap space after it if there is one.
    qint64 imageBlocks() const { return swapSize > 0 ? qMax(volumeBlocks(), swapStart + swapSize) : volumeBlocks(); }
    static QString nameText(const QByteArray &b);           // up to the first NUL, Latin-1
    QString fsNameText() const { return nameText(fsName); }
    QString packNameText() const { return nameText(packName); }
    bool winchester() const { return density == 0xFF; }
};

// Whether a SIR describes a file system a file of `bytes` bytes could hold: the fdn list, the volume, the counts and the
// lists within their limits. why: the reason it can't.
bool sirPlausible(const Sir &s, qint64 bytes, QString *why);

// A file descriptor node as stored (structures/fdn, from fmode on; the time at 48).
struct Fdn {
    int number = 0;
    quint8 mode = 0, access = 0, links = 0;
    int owner = 0;
    qint64 size = 0;
    qint64 map[MapEntries] = {};
    quint32 time = 0;

    static Fdn parse(const QByteArray &bytes, int number);
    bool busy() const { return mode != 0; }
    bool isDir() const { return (mode & Directory) != 0; }
    bool isDevice() const { return (mode & (BlockDevice | CharDevice)) != 0; }
    bool knownType() const { return mode == Busy || mode == (Busy | Directory) || mode == (Busy | BlockDevice) || mode == (Busy | CharDevice); }
    // A device file's numbers (crtsd: the major in fdn byte 9, the minor in byte 10).
    int major() const { return int(map[0] >> 16); }
    int minor() const { return int((map[0] >> 8) & 0xFF); }
    QString typeText() const;                // "file", "directory", "block device 3, 0" ...
    QString accessText() const;              // "rwxr-x", with "s" after it when the user id is set
    QDateTime when() const { return timeOf(time); }
};

// One name in the tree. The root is node 0 (no name, no parent).
struct Node {
    QString name;                            // the name's bytes, Latin-1 (case kept; any byte but NUL)
    int fdn = 0;
    int parent = -1;
    QVector<int> kids;                       // a directory's entries, . and .. left out, in the directory's order
    QString problem;                         // what stops it being read (a file) or listed (a directory)
};

class Fs {
public:
    // Reads the disc. Fails only if the SIR isn't a file system or the root can't be found; a disc with faults
    // attaches, and check() says what is wrong.
    bool attach(const dw::device::Linear &disc, QString *err);
    bool reload(QString *err);
    bool isAttached() const { return !m_disc.isNull(); }
    const dw::device::Linear &linear() const { return m_disc; }

    const Sir &sir() const { return m_sir; }
    qint64 imageBlocks() const { return m_disc.size() / BlockSize; }   // blocks the file holds
    bool shortImage() const { return imageBlocks() < m_sir.volumeBlocks(); }

    const QVector<Fdn> &fdns() const { return m_fdns; }                // fdn n at index n - 1
    const Fdn *fdn(int n) const { return n >= 1 && n <= m_fdns.size() ? &m_fdns[n - 1] : nullptr; }
    const QVector<Node> &nodes() const { return m_nodes; }
    const Node &root() const { return m_nodes[0]; }
    const Fdn *fdnOf(const Node &n) const { return fdn(n.fdn); }
    // The node at a path of names from the root (case-sensitive), or -1.
    int find(const QStringList &parts) const;
    QStringList pathOf(int node) const;
    int fileCount() const { return m_files; }
    int dirCount() const { return m_dirs; }

    bool readBlock(qint64 block, QByteArray *out, QString *err) const;
    // A file's blocks in order, as many as its size needs, 0 for a block never written; false (why) if its map names a
    // block outside the volume or past the end of the image, or its size is more than the volume. indirect: the map's
    // indirect blocks; beyond: data blocks the map holds after the size (the file still reads). Any may be null.
    bool blocksOf(const Fdn &f, QVector<qint64> *data, QVector<qint64> *indirect, QVector<qint64> *beyond, QString *why) const;
    // The file's bytes (a never-written block as zeros), cut at its size; a device has none. Fails (why, without the
    // name: the caller says which file) for a file whose map is broken, or a directory.
    bool readFile(const Node &n, QByteArray *out, QString *err) const;
    // The free list as the kernel would take it: every free block, the list blocks (links) included, in order; false
    // (why) where the chain leaves the volume, loops or holds a block that isn't a list.
    bool freeList(QVector<qint64> *blocks, QString *why) const;

    dw::fs::Report check() const;

private:
    bool readDir(const Fdn &f, QVector<QPair<int, QByteArray>> *entries, QString *why) const;
    bool blockOk(qint64 b, const char *what, QString *why) const;
    dw::device::Linear m_disc;
    Sir m_sir;
    QVector<Fdn> m_fdns;
    QVector<Node> m_nodes;
    QStringList m_treeErrors, m_treeWarnings;    // found while reading the tree: wrong . and .., names twice, odd names
    QVector<int> m_refs;                         // entries naming each fdn (. and .. included), index fdn - 1
    int m_files = 0, m_dirs = 0;
};

} // namespace UniFlex
