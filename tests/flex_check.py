#!/usr/bin/env python3
# FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# flex_check.py - an independent FLEX reader and test-disc maker, written from the format description in
# docs/disc-format.md without the program's code, to check FLEX Disk Wrangler against.
#
#   flex_check.py make DIR               synthetic test discs (deterministic), described in make() below
#   flex_check.py read IMAGE             this reader's view of a disc, as JSON
#   flex_check.py compare FLEXDW IMAGE.. flexdw's ls / get / check against this reader; exit 1 on any difference
#   flex_check.py writes FLEXDW SYN [SAMPLES]
#                                        changes made by flexdw (new, put, copy, rm, mv, attr, title) against this
#                                        file's own model of how FLEX writes (Fms, from docs/disc-format.md, "How FLEX
#                                        writes"): the images byte for byte after every step, and the same refusals
#
# Run with python3 -I (the images are untrusted data).
import hashlib
import json
import os
import random
import struct
import subprocess
import sys
import tempfile
import time

SEC = 256


# ----------------------------------------------------------------------------------------------- reading

class Disc:
    def __init__(self, data):
        self.d = data
        sir = data[2 * SEC:3 * SEC]
        self.label = sir[0x10:0x1B]
        self.volnum = struct.unpack('>H', sir[0x1B:0x1D])[0]
        self.first_free = (sir[0x1D], sir[0x1E])
        self.last_free = (sir[0x1F], sir[0x20])
        self.free_count = struct.unpack('>H', sir[0x21:0x23])[0]
        self.date = (sir[0x23], sir[0x24], sir[0x25])
        self.last_track = sir[0x26]
        self.spt = sir[0x27]

    def geometry_ok(self):
        return self.spt >= 5 and self.last_track >= 1 and (self.last_track + 1) * self.spt * SEC <= len(self.d) + self.spt * SEC

    def ok_ts(self, ts):
        return 0 <= ts[0] <= self.last_track and 1 <= ts[1] <= self.spt

    def sector(self, ts):
        off = (ts[0] * self.spt + ts[1] - 1) * SEC
        b = self.d[off:off + SEC]
        return b + bytes(SEC - len(b))

    def walk(self, start, limit=None, stop_at=None):
        """The chain from start: (sectors, fault or None). Stops at (0,0), at stop_at (included), or after limit."""
        out, seen, ts = [], set(), start
        while ts != (0, 0):
            if not self.ok_ts(ts):
                return out, 'leaves the disc at %d/%d' % ts
            if ts in seen:
                return out, 'loops back to %d/%d' % ts
            seen.add(ts)
            out.append(ts)
            if stop_at is not None and ts == stop_at:
                return out, None
            if limit is not None and len(out) > limit:
                return out, 'runs past its count'
            b = self.sector(ts)
            ts = (b[0], b[1])
        return out, None

    def directory(self):
        secs, fault = self.walk((0, 5))
        files, deleted = [], 0
        for ts in secs:
            b = self.sector(ts)
            for i in range(10):
                e = b[16 + i * 24:40 + i * 24]
                if e[0] == 0:
                    continue
                if e[0] & 0x80:
                    deleted += 1
                    continue
                name = e[0:8].split(b'\0')[0].decode('latin-1')
                ext = e[8:11].split(b'\0')[0].decode('latin-1')
                files.append({
                    'name': name + ('.' + ext if ext else ''),
                    'attr': e[11], 'start': (e[13], e[14]), 'end': (e[15], e[16]),
                    'sectors': struct.unpack('>H', e[17:19])[0], 'random': e[19] != 0,
                    'date': (e[21], e[22], e[23]),
                })
        return secs, fault, files, deleted

    def file_chain(self, f):
        """The file's sectors, or a fault: it must reach its end sector in exactly its count."""
        secs, fault = self.walk(f['start'], limit=f['sectors'], stop_at=f['end'])
        if fault:
            return secs, fault
        if not secs or secs[-1] != f['end']:
            return secs, "doesn't reach its end sector"
        if len(secs) != f['sectors']:
            return secs, 'has %d sectors, not %d' % (len(secs), f['sectors'])
        return secs, None

    def file_data(self, f):
        secs, fault = self.file_chain(f)
        if fault:
            return None
        skip = 2 if f['random'] else 0
        return b''.join(self.sector(ts)[4:] for ts in secs[skip:])

    def errors(self):
        """Faults that make the disc unsafe to change: chains that break, loop or overlap, and a free count that's wrong."""
        errs = []
        owner = {}

        def claim(ts, who):
            if ts in owner:
                errs.append('%d/%d in both %s and %s' % (ts + (owner[ts], who)))
            else:
                owner[ts] = who
        dsecs, dfault, files, _ = self.directory()
        if dfault:
            errs.append('directory chain ' + dfault)
        for ts in dsecs:
            claim(ts, 'directory')
        names = set()
        for f in files:
            if f['name'].upper() in names:
                errs.append(f['name'] + ' twice')
            names.add(f['name'].upper())
            secs, fault = self.file_chain(f)
            if fault:
                errs.append(f['name'] + ' ' + fault)
                continue
            for ts in secs:
                claim(ts, f['name'])
        fsecs, ffault = self.walk(self.first_free)
        if ffault:
            errs.append('free chain ' + ffault)
        else:
            if len(fsecs) != self.free_count:
                errs.append('free count %d, chain %d' % (self.free_count, len(fsecs)))
            if fsecs and fsecs[-1] != self.last_free:
                errs.append('free chain ends elsewhere')
        for ts in fsecs:
            claim(ts, 'free')
        return errs


def read(path):
    with open(path, 'rb') as fh:
        d = Disc(fh.read())
    if not d.geometry_ok():
        return {'flex': False}
    _, _, files, deleted = d.directory()
    out = []
    for f in files:
        data = d.file_data(f)
        out.append({
            'name': f['name'], 'sectors': f['sectors'], 'random': f['random'],
            'write': bool(f['attr'] & 0x80), 'delete': bool(f['attr'] & 0x40),
            'read': bool(f['attr'] & 0x20), 'catalog': bool(f['attr'] & 0x10),
            'date': '%02d-%02d-%02d' % f['date'],
            'size': max(0, f['sectors'] - (2 if f['random'] else 0)) * 252,
            'sha256': hashlib.sha256(data).hexdigest() if data is not None else None,
        })
    return {'flex': True, 'tracks': d.last_track + 1, 'sectors': d.spt, 'label': d.label.split(b'\0')[0].decode('latin-1').strip(),
            'files': out, 'deleted': deleted, 'errors': d.errors()}


# ----------------------------------------------------------------------------------------------- making

class Maker:
    """A FLEX disc built the way FLEX lays one out: boot sectors, the SIR, directory sectors to the end of track 0,
    files taken from the start of the free chain, the rest of the disc in the free chain."""

    def __init__(self, tracks, spt, label=b'TEST', volnum=1, date=(11, 15, 26)):
        self.t, self.s = tracks, spt
        self.d = bytearray(tracks * spt * SEC)
        self.free = [(t, s) for t in range(1, tracks) for s in range(1, spt + 1)]
        self.dir = [(0, s) for s in range(5, spt + 1)]
        self.entries = []
        self.label, self.volnum, self.date = label, volnum, date

    def off(self, ts):
        return (ts[0] * self.s + ts[1] - 1) * SEC

    def put(self, ts, link, rec, data):
        o = self.off(ts)
        self.d[o:o + 2] = bytes(link)
        self.d[o + 2:o + 4] = struct.pack('>H', rec)
        self.d[o + 4:o + SEC] = data.ljust(252, b'\0')[:252]

    def add(self, name, ext, data, attr=0, random_=False, date=(1, 2, 83)):
        chunks = [data[i:i + 252] for i in range(0, max(len(data), 1), 252)]
        n = len(chunks) + (2 if random_ else 0)
        secs, self.free = self.free[:n], self.free[n:]
        recs = ([0, 0] if random_ else []) + list(range(1, len(chunks) + 1))
        bodies = ([b'', b''] if random_ else []) + chunks
        if random_:
            runs, k = [], 2
            while k < n:                                 # (track, sector, count) runs of consecutive data sectors
                start, cnt = secs[k], 1
                while k + cnt < n and secs[k + cnt] == self.next(secs[k + cnt - 1]) and cnt < 255:
                    cnt += 1
                runs.append(bytes([start[0], start[1], cnt]))
                k += cnt
            m = b''.join(runs)
            bodies[0], bodies[1] = m[:252], m[252:504]
        for i, ts in enumerate(secs):
            self.put(ts, secs[i + 1] if i + 1 < n else (0, 0), recs[i], bodies[i])
        e = name.ljust(8, b'\0') + ext.ljust(3, b'\0') + bytes([attr, 0]) + bytes(secs[0]) + bytes(secs[-1]) + \
            struct.pack('>H', n) + bytes([2 if random_ else 0, 0]) + bytes(date)
        self.entries.append(e)
        return secs

    def next(self, ts):
        return (ts[0], ts[1] + 1) if ts[1] < self.s else (ts[0] + 1, 1)

    def deleted(self, name, ext):
        e = b'\xff' + name[1:].ljust(7, b'\0') + ext.ljust(3, b'\0') + bytes(13)
        self.entries.append(e)

    def finish(self):
        for i, ts in enumerate(self.free):
            self.put(ts, self.free[i + 1] if i + 1 < len(self.free) else (0, 0), 0, b'')
        for i, ts in enumerate(self.dir):
            ents = b''.join(self.entries[i * 10:(i + 1) * 10])
            o = self.off(ts)
            self.d[o:o + SEC] = bytes(self.dir[i + 1] if i + 1 < len(self.dir) else (0, 0)) + bytes(14) + ents.ljust(240, b'\0')
        sir = bytearray(SEC)
        sir[0x10:0x1B] = self.label.ljust(11, b'\0')
        sir[0x1B:0x1D] = struct.pack('>H', self.volnum)
        sir[0x1D:0x1F] = bytes(self.free[0]) if self.free else b'\0\0'
        sir[0x1F:0x21] = bytes(self.free[-1]) if self.free else b'\0\0'
        sir[0x21:0x23] = struct.pack('>H', len(self.free))
        sir[0x23:0x26] = bytes(self.date)
        sir[0x26], sir[0x27] = self.t - 1, self.s
        self.d[2 * SEC:3 * SEC] = sir
        return self.d


def flex_text(lines):
    """FLEX text: CR line ends, runs of 2 or more spaces as TAB + count."""
    out = bytearray()
    for line in lines:
        i = 0
        while i < len(line):
            j = i
            while j < len(line) and line[j] == ' ':
                j += 1
            if j - i >= 2:
                out += bytes([9, j - i])
                i = j
            else:
                out += line[i].encode('latin-1')
                i += 1
        out += b'\r'
    return bytes(out)


def make(outdir):
    os.makedirs(outdir, exist_ok=True)
    rnd = random.Random(1979)                            # deterministic
    text = flex_text(['* A FLEX text file', '        LDA     #$41', '        JSR     PUTCHR    print it'] * 40)
    binary = bytes(rnd.randrange(256) for _ in range(3000))

    def write(name, data):
        with open(os.path.join(outdir, name), 'wb') as fh:
            fh.write(data)

    # basic.dsk: 40 x 10 (5.25" single-sided, single density); text, binary, random, protected, deleted, dated
    m = Maker(40, 10, b'BASIC', 42, (11, 15, 26))
    m.add(b'README', b'TXT', text, date=(3, 20, 84))
    m.add(b'PROG', b'CMD', binary, attr=0x80 | 0x40, date=(12, 31, 99))
    m.deleted(b'OLDFILE', b'BAK')
    m.add(b'DATA', b'RAN', bytes(range(256)) * 10, random_=True, date=(6, 1, 1))
    m.add(b'EMPTY', b'', b'', date=(0, 0, 0))
    m.add(b'HIDDEN', b'SYS', b'secret' * 100, attr=0x20 | 0x10)
    write('basic.dsk', m.finish())

    # big.dsk: 40 x 36 (5.25" double-sided, double density); 45 files, so the directory spans 5 sectors
    m = Maker(40, 36, b'BIGDISC', 505, (1, 25, 83))
    for i in range(45):
        m.add(b'FILE%02d' % i, b'DAT', bytes([i]) * (100 + i * 97), date=(1 + i % 12, 1 + i % 28, 80 + i % 10))
    write('big.dsk', m.finish())

    # empty.dsk: 77 x 15 (8" single density), nothing on it
    write('empty.dsk', Maker(77, 15, b'EMPTY', 0).finish())

    def fault(name, change):
        m = Maker(35, 10, b'FAULT', 1)
        m.add(b'ONE', b'TXT', text)
        m.add(b'TWO', b'BIN', binary)
        d = m.finish()
        change(m, d)
        write(name, d)

    def setlink(m, d, ts, to):
        o = m.off(ts)
        d[o:o + 2] = bytes(to)

    # each makes check() fail with an error
    fault('bad-freeloop.dsk', lambda m, d: setlink(m, d, m.free[50], m.free[10]))
    fault('bad-freecount.dsk', lambda m, d: d.__setitem__(slice(2 * SEC + 0x21, 2 * SEC + 0x23), struct.pack('>H', len(m.free) + 3)))
    fault('bad-chain.dsk', lambda m, d: setlink(m, d, (1, 3), (0, 0)))           # ONE.TXT ends early
    fault('bad-crosslink.dsk', lambda m, d: setlink(m, d, m.free[0], (1, 2)))     # the free chain runs into ONE.TXT
    fault('bad-dirloop.dsk', lambda m, d: setlink(m, d, (0, 6), (0, 5)))

    # full.dsk: 40 x 10, its 6 directory sectors full (60 files of one sector): a new file makes the directory grow
    m = Maker(40, 10, b'FULL', 60, (11, 15, 26))
    for i in range(60):
        m.add(b'F%02d' % i, b'TXT', b'file %d\r' % i, date=(1, 1 + i % 28, 85))
    write('full.dsk', m.finish())

    # frag.dsk: 40 x 36, empty, its free chain through every odd sector, then every even one: no two free sectors in a
    # row follow each other, so a random file's map needs a run for every data sector
    m = Maker(40, 36, b'FRAG', 2, (11, 15, 26))
    m.free = sorted(m.free, key=lambda ts: (ts[1] % 2 == 0, ts[0], ts[1]))
    write('frag.dsk', m.finish())

    # random.dsk: 40 x 36, two random files: RAN300.DAT (300 data sectors in a row: its map has runs of 255 and 45) and
    # RAN200.DAT (200), to copy onto other discs
    own = random.Random(1976)                            # its own, so that the discs made before it stay as they were
    m = Maker(40, 36, b'RANDOM', 3, (11, 15, 26))
    m.add(b'RAN300', b'DAT', bytes(own.randrange(256) for _ in range(300 * 252)), random_=True, date=(7, 4, 76))
    m.add(b'RAN200', b'DAT', bytes(own.randrange(256) for _ in range(200 * 252)), random_=True, attr=0x80, date=(2, 29, 80))
    write('random.dsk', m.finish())

    # short.dsk: basic.dsk less its last track (FLEX's geometry is longer than the file: a "possible" reading)
    with open(os.path.join(outdir, 'basic.dsk'), 'rb') as fh:
        write('short.dsk', fh.read()[:39 * 10 * SEC])

    # os9.dsk: an OS-9 identification sector (1232 sectors of 256 bytes, 16 to a track, volume "SAMPLE")
    lsn0 = bytearray(SEC)
    lsn0[0:3] = (1232).to_bytes(3, 'big')
    lsn0[3] = 16
    lsn0[4:6] = (154).to_bytes(2, 'big')
    lsn0[6:8] = (1).to_bytes(2, 'big')
    lsn0[8:11] = (2).to_bytes(3, 'big')
    lsn0[0x1F:0x25] = b'SAMPL' + bytes([ord('E') | 0x80])
    write('os9.dsk', bytes(lsn0) + bytes(1231 * SEC))

    # uniflex.dsk: a UniFLEX SIR in block 1 naming the file system and the pack
    uf = bytearray(2464 * 512)
    uf[512 + 26:512 + 33] = b'uniflex'
    uf[512 + 40:512 + 50] = b'uniflex107'
    write('uniflex.dsk', bytes(uf))

    # noise.bin: random bytes, nothing at all
    write('noise.bin', bytes(rnd.randrange(256) for _ in range(80 * SEC)))



# ----------------------------------------------------------------------------------------------- writing, as FLEX does

class Refused(Exception):
    pass


class Fms:
    """How FLEX changes a disc, from docs/disc-format.md ("How FLEX writes"), written without the program's code: the
    state each change leaves, byte for byte. A change FLEX (or the program) refuses raises Refused and leaves nothing."""

    def __init__(self, data):
        self.d = bytearray(data)
        self.spt = self.d[2 * SEC + 0x27]
        self.lt = self.d[2 * SEC + 0x26]

    def off(self, ts):
        return (ts[0] * self.spt + ts[1] - 1) * SEC

    def sec(self, ts):
        return bytearray(self.d[self.off(ts):self.off(ts) + SEC])

    def wsec(self, ts, b):
        self.d[self.off(ts):self.off(ts) + SEC] = b

    def free(self):
        s = self.d[2 * SEC:3 * SEC]
        return (s[0x1D], s[0x1E]), (s[0x1F], s[0x20]), struct.unpack('>H', s[0x21:0x23])[0]

    def setfree(self, first, last, count):
        o = 2 * SEC
        self.d[o + 0x1D:o + 0x23] = bytes(first) + bytes(last) + struct.pack('>H', count & 0xFFFF)

    def take(self):
        """The first free sector; its link becomes the first free one, the count goes down; an empty chain is 0/0/0."""
        first, last, count = self.free()
        if first == (0, 0):
            raise Refused('disc full')
        b = self.sec(first)
        nxt = (b[0], b[1])
        if nxt == (0, 0):
            self.setfree((0, 0), (0, 0), 0)
        else:
            self.setfree(nxt, last, count - 1)
        return first

    def dirsecs(self):
        out, ts = [], (0, 5)
        while ts != (0, 0) and ts not in out:
            out.append(ts)
            b = self.sec(ts)
            ts = (b[0], b[1])
        return out

    def slots(self):
        for ts in self.dirsecs():
            b = self.sec(ts)
            for i in range(10):
                yield ts, i, bytes(b[16 + i * 24:40 + i * 24])

    @staticmethod
    def name_of(e):
        n = e[0:8].split(b'\0')[0].decode('latin-1')
        x = e[8:11].split(b'\0')[0].decode('latin-1')
        return n + ('.' + x if x else '')

    def find(self, name):
        for ts, i, e in self.slots():
            if e[0] != 0 and not e[0] & 0x80 and self.name_of(e).upper() == name.upper():
                return ts, i, e
        return None

    def set_entry(self, ts, i, e):
        b = self.sec(ts)
        b[16 + i * 24:40 + i * 24] = e
        self.wsec(ts, b)

    @staticmethod
    def stored(name):
        n, _, x = name.upper().partition('.')
        return n.encode('latin-1').ljust(8, b'\0') + x.encode('latin-1').ljust(3, b'\0')

    def nxt(self, ts):
        return (ts[0], ts[1] + 1) if ts[1] < self.spt else (ts[0] + 1, 1)

    def put(self, name, data, attr=0, random_=False, date=(0, 0, 0)):
        if not data:
            raise Refused('empty')
        if self.find(name):
            raise Refused('exists')
        n = (len(data) + 251) // 252 + (2 if random_ else 0)
        # the first slot that is deleted or never used (FLEX's search stops at a never-used one)
        slot = next(((ts, i) for ts, i, e in self.slots() if e[0] == 0 or e[0] & 0x80), None)
        if n + (1 if slot is None else 0) > self.free()[2]:
            raise Refused('no room')
        runs = []
        before = bytes(self.d)
        if slot is None:                     # the directory grows by a sector from the free chain, record number 1
            ns = self.take()
            b = bytearray(SEC)
            b[2:4] = b'\0\1'
            self.wsec(ns, b)
            last = self.dirsecs()[-1]
            lb = self.sec(last)
            lb[0:2] = bytes(ns)
            self.wsec(last, lb)
            slot = (ns, 0)
        secs = [self.take() for _ in range(n)]
        if random_:
            k = 2
            while k < n:
                start, cnt = secs[k], 1
                while k + cnt < n and secs[k + cnt] == self.nxt(secs[k + cnt - 1]) and cnt < 255:
                    cnt += 1
                runs.append(bytes([start[0], start[1], cnt]))
                k += cnt
            if len(runs) > 168:
                self.d = bytearray(before)
                raise Refused('map overflow')
        for k, ts in enumerate(secs):
            b = bytearray(SEC)
            b[0:2] = bytes(secs[k + 1]) if k + 1 < n else b'\0\0'
            j = k - 2 if random_ else k
            b[2:4] = struct.pack('>H', 0 if j < 0 else j + 1)
            if j >= 0:
                chunk = data[j * 252:(j + 1) * 252]
                b[4:4 + len(chunk)] = chunk
            else:
                for r, run in enumerate(runs[k * 84:(k + 1) * 84]):
                    b[4 + 3 * r:7 + 3 * r] = run
            self.wsec(ts, b)
        e = self.stored(name) + bytes([attr & 0xF0, 0]) + bytes(secs[0]) + bytes(secs[-1]) + struct.pack('>H', n) + \
            bytes([2 if random_ else 0, 0]) + bytes(date)
        self.set_entry(slot[0], slot[1], e)

    def guarded(self, name):
        f = self.find(name)
        if not f:
            raise Refused('no such file')
        if f[2][11] & 0xE0:                  # write-protected (11), delete- or read-protected (12)
            raise Refused('protected')
        return f

    def rm(self, name):
        ts, i, e = self.guarded(name)
        start, end, n = (e[13], e[14]), (e[15], e[16]), struct.unpack('>H', e[17:19])[0]
        first, last, count = self.free()
        if start != (0, 0):
            if last == (0, 0):
                first = start
            else:
                lb = self.sec(last)
                lb[0:2] = bytes(start)
                self.wsec(last, lb)
            self.setfree(first, end, count + n)
        self.set_entry(ts, i, b'\xff' + e[1:])

    def mv(self, old, new):
        ts, i, e = self.guarded(old)
        if self.find(new):
            raise Refused('exists')
        self.set_entry(ts, i, self.stored(new) + e[11:])

    def prot(self, name, attr):
        f = self.find(name)
        if not f:
            raise Refused('no such file')
        ts, i, e = f
        self.set_entry(ts, i, e[:11] + bytes([(e[11] & 0x0F) | (attr & 0xF0)]) + e[12:])

    def label(self, name):
        o = 2 * SEC + 0x10
        self.d[o:o + 8] = name.encode('latin-1').ljust(8, b'\0')

    @staticmethod
    def new(tracks, spt, label, number, date):
        """NEWDISK: every sector linked to the next, the SIR, the directory to the end of track 0; sector 1 empty."""
        d = bytearray(tracks * spt * SEC)
        for t in range(tracks):
            for s in range(1, spt + 1):
                if (t, s) in ((0, 1), (0, 3), (0, spt)):
                    link = (0, 0)
                elif s < spt:
                    link = (t, s + 1)
                elif t + 1 < tracks:
                    link = (t + 1, 1)
                else:
                    link = (0, 0)
                o = (t * spt + s - 1) * SEC
                d[o:o + 2] = bytes(link)
        o = 2 * SEC
        d[o + 0x10:o + 0x18] = label.encode('latin-1').ljust(8, b'\0')
        d[o + 0x1B:o + 0x1D] = struct.pack('>H', number)
        d[o + 0x1D:o + 0x23] = bytes([1, 1, tracks - 1, spt]) + struct.pack('>H', (tracks - 1) * spt)
        d[o + 0x23:o + 0x26] = bytes(date)
        d[o + 0x26], d[o + 0x27] = tracks - 1, spt
        return bytes(d)


def entry_info(data, name):
    """(data, protection bits, random, date) of a file, as a copy carries it (an invalid date: none)."""
    disc = Disc(data)
    _, _, files, _ = disc.directory()
    for f in files:
        if f['name'] == name:
            mo, dd, yy = f['date']
            ok = 1 <= mo <= 12 and 1 <= dd <= 31
            return disc.file_data(f), f['attr'] & 0xF0, f['random'], (mo, dd, yy) if ok else None
    raise KeyError(name)


EPOCH_DATE = (1, 1, 0)                   # SOURCE_DATE_EPOCH=946684800: 2000-01-01, the date of a file that brings none


def writes(flexdw, syn, samples=None):
    """Each scenario: a disc, then steps done by flexdw (one invocation, one save, each) and by the model; after every
    step the image must be the model's byte for byte, and a step the model refuses must fail and change nothing."""
    env = dict(os.environ, SOURCE_DATE_EPOCH='946684800', TZ='UTC')
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        def run(*args):
            return subprocess.run([flexdw, '--json'] + [str(a) for a in args], capture_output=True, timeout=120, env=env).returncode

        def host(name, data, date):
            p = os.path.join(tmp, 'host', name.lower())
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with open(p, 'wb') as fh:
                fh.write(data)
            when = time.mktime(time.strptime('%02d-%02d-%04d 12' % (date[0], date[1], 1900 + date[2] if date[2] >= 70 else 2000 + date[2]), '%m-%d-%Y %H')) if date else 0
            os.utime(p, (when, when))
            return p

        def scenario(title, image, steps):
            nonlocal bad
            img = os.path.join(tmp, 'img.dsk')
            with open(image, 'rb') as fh:
                data = fh.read()
            with open(img, 'wb') as fh:
                fh.write(data)
            model = Fms(data)
            problems = []
            for k, step in enumerate(steps, 1):
                op, args = step[0], step[1:]
                before = bytes(model.d)
                refused = None
                try:
                    if op == 'put':
                        name, payload, date = args
                        p = host(name, payload, date)
                        model.put(name, payload, 0, False, date)
                        cmd = ('put', img, p, '/')
                    elif op == 'copy':
                        src, name = args
                        with open(src, 'rb') as fh:
                            payload, attr, rnd, date = entry_info(fh.read(), name)
                        model.put(name, payload, attr, rnd, date or EPOCH_DATE)
                        cmd = ('copy', src, img, name)
                    elif op == 'rm':
                        model.rm(args[0])
                        cmd = ('rm', img, args[0])
                    elif op == 'mv':
                        model.mv(args[0], args[1])
                        cmd = ('mv', img, args[0], args[1])
                    elif op == 'attr':
                        name, bits = args
                        model.prot(name, bits)
                        cmd = ('attr', img, name) + tuple('--set=%s=%s' % (k2, 'true' if bits & b else 'false')
                                                          for k2, b in (('write', 0x80), ('delete', 0x40), ('read', 0x20), ('catalog', 0x10)))
                    elif op == 'title':
                        model.label(args[0])
                        cmd = ('title', img, args[0])
                    else:
                        raise ValueError(op)
                except Refused as e:
                    refused = str(e)
                    model.d = bytearray(before)
                    cmd = {'put': lambda: ('put', img, host(args[0], args[1], args[2]), '/'),
                           'copy': lambda: ('copy', args[0], img, args[1]),
                           'rm': lambda: ('rm', img, args[0]), 'mv': lambda: ('mv', img, args[0], args[1]),
                           'attr': lambda: ('attr', img, args[0], '--set=write=false'),
                           'title': lambda: ('title', img, args[0])}[op]()
                code = run(*cmd)
                with open(img, 'rb') as fh:
                    got = fh.read()
                what = '%d %s %s' % (k, op, ' '.join(str(a) for a in args if isinstance(a, str)))
                if refused and code == 0:
                    problems.append('%s: FLEX refuses it (%s), flexdw did it' % (what, refused))
                elif not refused and code != 0:
                    problems.append('%s: flexdw failed (exit %d)' % (what, code))
                if got != bytes(model.d):
                    diff = [i for i in range(min(len(got), len(model.d))) if got[i] != model.d[i]]
                    problems.append('%s: image differs from FLEX\'s at %d bytes, first at sector %d/%d byte %d' % (
                        what, len(diff), diff[0] // SEC // model.spt, diff[0] // SEC % model.spt + 1, diff[0] % SEC) if diff else '%s: sizes differ' % what)
                    model.d = bytearray(got)          # go on from what flexdw made, so one fault is reported once
            if not Disc(bytes(model.d)).errors() == []:
                problems.append('the result has errors: %s' % Disc(bytes(model.d)).errors())
            if problems:
                bad += 1
                print('%s: DIFFERENT\n  ' % title + '\n  '.join(problems))
            else:
                print('%s: %d steps, the image is FLEX\'s byte for byte after each' % (title, len(steps)))

        # new discs: every shape flexdw makes, against NEWDISK's layout
        code = subprocess.run([flexdw, 'new', '--list', '--json'], capture_output=True, timeout=60, env=env)
        for f in json.loads(code.stdout.decode('utf-8'))['result']['formats']:
            img = os.path.join(tmp, 'new-%s.dsk' % f['id'])
            c = run('new', img, '--format=' + f['id'], '--title=NEWDISC')
            with open(img, 'rb') as fh:
                got = fh.read()
            want = Fms.new(f['tracks'], f['sectorsPerTrack'], 'NEWDISC', 0, EPOCH_DATE)
            if c != 0 or got != want:
                bad += 1
                print('new %s: DIFFERENT from NEWDISK\'s layout (exit %d)' % (f['id'], c))
            else:
                print('new %s: NEWDISK\'s layout byte for byte' % f['id'])

        rnd = random.Random(1983)
        blob = lambda n: bytes(rnd.randrange(256) for _ in range(n))
        basic, random_dsk = os.path.join(syn, 'basic.dsk'), os.path.join(syn, 'random.dsk')
        scenario('basic.dsk: put into a deleted slot, delete, rename, protect, title, copy a random file', basic, [
            ('put', 'NOTE.TXT', b'a note\r' * 60, (3, 20, 84)),            # 2 sectors, in OLDFILE.BAK's slot
            ('rm', 'README.TXT'),                                         # its chain to the end of the free chain
            ('put', 'BIG.BIN', blob(30 * 252 + 1), (12, 25, 99)),
            ('put', 'NOTE.TXT', b'again', (1, 1, 85)),                    # exists: refused
            ('put', 'EMPTY.TXT', b'', (1, 1, 85)),                        # empty: refused
            ('mv', 'PROG.CMD', 'X.CMD'),                                  # write- and delete-protected: refused
            ('rm', 'HIDDEN.SYS'),                                         # read-protected: refused
            ('attr', 'PROG.CMD', 0),
            ('mv', 'PROG.CMD', 'X.CMD'),
            ('mv', 'X.CMD', 'NOTE.TXT'),                                  # exists: refused
            ('rm', 'X.CMD'),
            ('copy', basic, 'DATA.RAN'),                                  # random: a new map, in more than one run
            ('rm', 'BIG.BIN'),
            ('copy', random_dsk, 'RAN300.DAT'),
            ('attr', 'NOTE.TXT', 0x80 | 0x10),
            ('title', 'RENAMED'),
            ('rm', 'NOTE.TXT'),                                           # write-protected now: refused
        ])
        scenario('full.dsk: the directory grows by a sector, then fills it', os.path.join(syn, 'full.dsk'), [
            ('put', 'MORE%02d.TXT' % i, b'more %d\r' % i, (5, 5, 85)) for i in range(12)
        ] + [('rm', 'F05.TXT'), ('put', 'AFTER.TXT', b'into the freed slot', (5, 6, 85))])
        scenario('frag.dsk: a random file whose sectors are all apart', os.path.join(syn, 'frag.dsk'), [
            ('copy', random_dsk, 'RAN200.DAT'),                           # 200 runs: more than FLEX's map holds
            ('copy', basic, 'DATA.RAN'),                                  # 11 runs
            ('put', 'SEQ.BIN', blob(5000), (8, 8, 88)),
        ])
        new35 = os.path.join(tmp, 'new-flex-35x10.dsk')
        scenario('a new 35 x 10 disc: filled to the last sector, emptied, filled again', new35, [
            ('put', 'TOOBIG.BIN', blob(340 * 252 + 1), (1, 2, 3)),        # one byte more than the disc holds: refused
            ('put', 'ALL.BIN', blob(340 * 252), (1, 2, 3)),               # every free sector
            ('put', 'ONEMORE.TXT', b'x', (1, 2, 3)),                       # the disc is full: refused
            ('rm', 'ALL.BIN'),                                            # an empty free chain becomes the file's chain
            ('put', 'SMALL.TXT', b'y' * 600, (1, 2, 3)),
            ('copy', random_dsk, 'RAN300.DAT'),                           # runs of 255 and the rest
        ])
        if samples:
            for name, steps in (('FLEX2_B.DSK', [('put', 'NEW.TXT', b'on a FLEX 2 disc\r' * 40, (10, 6, 79)), ('rm', 'SMLOAD.CMD'),
                                                 ('copy', basic, 'DATA.RAN'), ('mv', 'BASICX.CMD', 'BASIC.CMD')]),
                                ('SYS900.DSK', [('put', 'NEW.TXT', b'on FLEX 9\r' * 40, (10, 19, 80)), ('rm', 'COPY.CMD'),
                                                ('put', 'NEW2.TXT', blob(2000), (10, 19, 80))]),
                                ('FL2_0SWT.DSK', [('rm', 'ASMB.CMD'), ('put', 'NEW.TXT', blob(9000), (2, 27, 79))])):
                p = os.path.join(samples, name)
                if os.path.exists(p):
                    scenario(name, p, steps)
    return bad


# ----------------------------------------------------------------------------------------------- comparing

def flexdw_json(flexdw, *args):
    p = subprocess.run([flexdw, '--json'] + list(args), capture_output=True, timeout=60)
    return p.returncode, json.loads(p.stdout.decode('utf-8'))


def compare(flexdw, images):
    bad = 0
    for img in images:
        mine = read(img)
        name = os.path.basename(img)
        code, ls = flexdw_json(flexdw, 'ls', img)
        if not mine['flex']:
            if code == 0:
                print('%s: flexdw reads it, this reader says it is not FLEX' % name)
                bad += 1
            else:
                print('%s: not FLEX, and flexdw agrees (exit %d)' % (name, code))
            continue
        if code != 0:
            print('%s: flexdw ls failed: %s' % (name, ls.get('error')))
            bad += 1
            continue
        theirs = {e['name']: e for e in ls['result']['entries']}
        problems = []
        if sorted(theirs) != sorted(f['name'] for f in mine['files']):
            problems.append('names differ: %s / %s' % (sorted(theirs), sorted(f['name'] for f in mine['files'])))
        with tempfile.TemporaryDirectory() as tmp:
            gcode, _ = flexdw_json(flexdw, 'get', img, '/', tmp)
            got = {}
            for fn in os.listdir(tmp):
                with open(os.path.join(tmp, fn), 'rb') as fh:
                    got[fn] = hashlib.sha256(fh.read()).hexdigest()
        for f in mine['files']:
            e = theirs.get(f['name'])
            if not e:
                continue
            a = e['attrs']
            for k in ('sectors', 'random', 'write', 'delete', 'read', 'catalog'):
                if a.get(k) != f[k]:
                    problems.append('%s: %s %r, not %r' % (f['name'], k, a.get(k), f[k]))
            if e['size'] != f['size']:
                problems.append('%s: size %d, not %d' % (f['name'], e['size'], f['size']))
            mo, dd, yy = (int(x) for x in f['date'].split('-'))
            want = None
            if 1 <= mo <= 12 and 1 <= dd <= 31:
                want = '%04d-%02d-%02d' % (2000 + yy if yy < 70 else 1900 + yy, mo, dd)
            have = e['modified'][:10] if e['modified'] else None
            if have != want:
                problems.append('%s: date %s, not %s' % (f['name'], have, want))
            if (e['problem'] is None) != (f['sha256'] is not None):
                problems.append('%s: problem %r, but this reader %s it' % (f['name'], e['problem'], 'reads' if f['sha256'] else "can't read"))
            if f['sha256'] is not None and gcode == 0:
                h = got.get(f['name'].lower(), got.get(f['name']))
                if h != f['sha256']:
                    problems.append('%s: contents differ' % f['name'])
        ccode, _ = flexdw_json(flexdw, 'check', img)
        if (ccode == 4) != bool(mine['errors']):
            problems.append('check: flexdw exit %d, this reader found %s' % (ccode, mine['errors'] or 'no errors'))
        if problems:
            bad += 1
            print('%s: DIFFERENT\n  ' % name + '\n  '.join(problems))
        else:
            print('%s: %d files match the independent FLEX reader%s' % (name, len(mine['files']),
                  ' (both find errors)' if mine['errors'] else ''))
    return bad


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'make':
        make(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'read':
        print(json.dumps(read(sys.argv[2]), indent=1))
    elif len(sys.argv) >= 4 and sys.argv[1] == 'compare':
        sys.exit(1 if compare(sys.argv[2], sys.argv[3:]) else 0)
    elif len(sys.argv) in (4, 5) and sys.argv[1] == 'writes':
        sys.exit(1 if writes(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) == 5 else None) else 0)
    else:
        print(__doc__ or 'usage: flex_check.py make DIR | read IMAGE | compare FLEXDW IMAGE... | writes FLEXDW SYN [SAMPLES]', file=sys.stderr)
        sys.exit(2)


if __name__ == '__main__':
    main()
