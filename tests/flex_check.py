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
    else:
        print(__doc__ or 'usage: flex_check.py make DIR | read IMAGE | compare FLEXDW IMAGE...', file=sys.stderr)
        sys.exit(2)


if __name__ == '__main__':
    main()
