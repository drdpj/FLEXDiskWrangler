#!/usr/bin/env python3
# FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson
# Licensed under the GNU General Public License v3 or later (see LICENSE).
#
# uniflex_check.py - an independent UniFLEX reader and test-disc maker, written from docs/uniflex-format.md without
# the program's code, to check FLEX Disk Wrangler's reading of UniFLEX discs against.
#
#   uniflex_check.py make DIR                 synthetic UniFLEX discs (deterministic), described in make() below
#   uniflex_check.py read IMAGE               this reader's view of a disc, as JSON
#   uniflex_check.py compare FLEXDW IMAGE...  flexdw's ls -R / get / check against this reader; exit 1 on any difference
#   uniflex_check.py samples FLEXDW LIST DIR...
#                                             every sample named in LIST (tests/uniflex_samples.txt) found in the DIRs:
#                                             compared, and its check's outcome (clean, or the faults it names) as LIST says
#
# Run with python3 -I (the images are untrusted data).
import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
from datetime import datetime, timedelta, timezone

BLOCK = 512
EPOCH = datetime(1980, 1, 1, tzinfo=timezone.utc)


def u16(b, o):
    return b[o] << 8 | b[o + 1]


def u24(b, o):
    return b[o] << 16 | b[o + 1] << 8 | b[o + 2]


def u32(b, o):
    return struct.unpack('>I', b[o:o + 4])[0]


def when(t):
    return None if t == 0 else (EPOCH + timedelta(seconds=t)).strftime('%Y-%m-%dT%H:%M:%S')


# ----------------------------------------------------------------------------------------------- reading

class Fdn:
    def __init__(self, raw, n):
        self.n = n
        self.mode, self.access, self.links = raw[0], raw[1], raw[2]
        self.owner = u16(raw, 3)
        self.size = u32(raw, 5)
        self.map = [u24(raw, 9 + 3 * i) for i in range(13)]
        self.time = u32(raw, 48)

    def kind(self):
        return {0x01: 'file', 0x09: 'dir', 0x03: 'block', 0x05: 'char'}.get(self.mode)

    def type_text(self):
        k = self.kind()
        if k in ('block', 'char'):
            return '%s device %d, %d' % ('block' if k == 'block' else 'character', self.map[0] >> 16, (self.map[0] >> 8) & 0xFF)
        return {'file': 'file', 'dir': 'directory'}.get(k)


class Disc:
    def __init__(self, data):
        self.d = data
        self.nblocks = len(data) // BLOCK + (1 if len(data) % BLOCK else 0)
        s = self.block(1)
        self.fdn_blocks = u16(s, 16)
        self.last = u24(s, 18)
        self.free = u24(s, 21)
        self.free_fdns = u16(s, 24)
        self.pack = s[40:54].split(b'\0')[0]
        self.swap = (u24(s, 60), u16(s, 63))
        self.sir_fdns = [u16(s, 89 + 2 * i) for i in range(min(s[88], 50))]
        self.sir_nfdn = s[88]
        self.sir_nfree = s[189]
        self.sir_free = [u24(s, 190 + 3 * i) for i in range(min(s[189], 100))]
        self.first = 2 + self.fdn_blocks
        raw = data[2 * BLOCK:(2 + self.fdn_blocks) * BLOCK]
        self.fdns = [Fdn(raw[i * 64:i * 64 + 64], i + 1) for i in range(self.fdn_blocks * 8)]

    def block(self, n):
        b = self.d[n * BLOCK:(n + 1) * BLOCK]
        return b + bytes(BLOCK - len(b))

    def ok_block(self, b):
        return self.first <= b <= self.last and b * BLOCK < len(self.d)

    def fdn(self, n):
        return self.fdns[n - 1] if 1 <= n <= len(self.fdns) else None

    def blocks(self, f):
        """The file's blocks for its size (0: never written), its indirect blocks, its blocks after its size; or a
        string saying why its map can't be followed."""
        if f.kind() in ('block', 'char'):
            return [], [], []
        need = (f.size + BLOCK - 1) // BLOCK
        if need > self.last + 1:
            return 'size more than the volume'
        data, ind, extra = [0] * need, [], []

        def put(i, b):
            if not b:
                return None
            if not self.ok_block(b):
                return 'block %d outside the volume or the image' % b
            if i < need:
                data[i] = b
            else:
                extra.append(b)
            return None

        for i in range(10):
            e = put(i, f.map[i])
            if e:
                return e

        def walk(b, level, first):
            if not self.ok_block(b):
                return 'indirect block %d outside the volume or the image' % b
            ind.append(b)
            blk = self.block(b)
            span = 128 ** (level - 1)
            for k in range(128):
                x = u24(blk, 3 * k)
                if not x:
                    continue
                e = put(first + k, x) if level == 1 else walk(x, level - 1, first + k * span)
                if e:
                    return e
            return None

        for i, level, first in ((10, 1, 10), (11, 2, 138), (12, 3, 138 + 128 * 128)):
            if f.map[i]:
                e = walk(f.map[i], level, first)
                if e:
                    return e
        return data, ind, extra

    def content(self, f):
        r = self.blocks(f)
        if isinstance(r, str):
            return None
        return b''.join(self.block(b) if b else bytes(BLOCK) for b in r[0])[:f.size]

    def tree(self):
        """Every name from the root, depth first: (path list, fdn number, problem or None)."""
        out, errors, refs = [], [], {}
        seen_dirs = {1}

        def entries(f):
            c = self.content(f)
            if c is None:
                return None
            r = []
            for o in range(0, len(c) - 15, 16):
                n = u16(c, o)
                if n:
                    r.append((n, c[o + 2:o + 16].split(b'\0')[0]))
            return r

        def walk(n, path, parent):
            es = entries(self.fdn(n))
            if es is None:
                errors.append('/%s: directory unreadable' % '/'.join(path))
                return False
            dot = dotdot = False
            names = set()
            for m, name in es:
                refs[m] = refs.get(m, 0) + 1
                if name == b'.':
                    dot = True
                    if m != n:
                        errors.append('. wrong')
                    continue
                if name == b'..':
                    dotdot = True
                    if m != parent:
                        errors.append('.. wrong')
                    continue
                if name in names:
                    errors.append('name twice')
                names.add(name)
                p = path + [name.decode('latin-1')]
                g = self.fdn(m)
                prob = None
                if g is None:
                    prob = 'fdn beyond the list'
                elif g.mode == 0:
                    prob = 'free fdn'
                elif g.kind() is None:
                    prob = 'unknown type'
                elif g.kind() == 'dir':
                    if m in seen_dirs:
                        prob = 'directory named twice'
                    else:
                        seen_dirs.add(m)
                elif isinstance(self.blocks(g), str):
                    prob = self.blocks(g)
                out.append((p, m, prob))
                if prob:
                    errors.append('/%s: %s' % ('/'.join(p), prob))
                elif g.kind() == 'dir':
                    if not walk(m, p, n):
                        out[-1] = (p, m, 'unreadable')
            if not dot or not dotdot:
                errors.append('/%s: no . or ..' % '/'.join(path))
            return True

        if self.fdn(1).mode != 0x09:
            return out, ['root not a directory'], refs
        walk(1, [], 1)
        return out, errors, refs

    def free_list(self):
        """The free blocks in the order UniFLEX takes them, or (blocks, why) where the chain breaks."""
        got, seen = [], set()
        lst = self.sir_free
        if self.sir_nfree > 100:
            return got, 'SIR list too long'
        while True:
            for i in range(len(lst) - 1, 0, -1):
                if lst[i] == 0:
                    return got, None
                if not self.ok_block(lst[i]) or lst[i] in seen:
                    return got, 'bad entry %d' % lst[i]
                seen.add(lst[i])
                got.append(lst[i])
            if not lst or lst[0] == 0:
                return got, None
            link = lst[0]
            if not self.ok_block(link) or link in seen:
                return got, 'bad link %d' % link
            seen.add(link)
            got.append(link)
            blk = self.block(link)
            lst = [u24(blk, 3 * i) for i in range(100)]

    def check(self):
        """Errors (the program's check must find some exactly when this finds some)."""
        errs = []
        if self.nblocks < self.last + 1:
            errs.append('image short')
        if self.fdn(1).mode != 0x09:
            return errs + ['root']
        nodes, terr, refs = self.tree()
        errs += terr
        owner = {}
        reached = {1}
        for _, m, prob in [([], 1, None)] + nodes:
            g = self.fdn(m)
            if prob or g is None or m in reached and m != 1 or g.mode == 0:
                continue
            reached.add(m)
        for m in sorted(reached):
            g = self.fdn(m)
            if refs.get(m, 0) != g.links:
                errs.append('links of fdn %d' % m)
            r = self.blocks(g)
            if isinstance(r, str):
                continue
            for b in r[0] + r[1] + r[2]:
                if b:
                    if b in owner:
                        errs.append('block %d twice' % b)
                    owner[b] = m
        free, why = self.free_list()
        if why:
            errs.append('free list: ' + why)
        for b in free:
            if b in owner:
                errs.append('block %d free and used' % b)
            owner[b] = 'free'
        if len(free) != self.free:
            errs.append('free count')
        nfree_fdns = sum(1 for f in self.fdns if f.mode == 0)
        if nfree_fdns != self.free_fdns:
            errs.append('free fdn count')
        for n in self.sir_fdns:
            if self.fdn(n) is None or self.fdn(n).mode != 0:
                errs.append('SIR lists a used fdn')
        return errs

    def listing(self):
        nodes, _, _ = self.tree()
        out = []
        for path, m, prob in nodes:
            g = self.fdn(m)
            e = {'path': path, 'problem': bool(prob)}
            if g is not None and g.mode:
                e.update(kind='dir' if g.kind() == 'dir' else 'file', fdn=m, links=g.links, owner=g.owner,
                         access=g.access & 0x7F, modified=when(g.time), type=g.type_text())
                if g.kind() == 'file':
                    e['size'] = g.size
                    if not prob:
                        e['sha'] = hashlib.sha256(self.content(g)).hexdigest()
                elif g.kind() in ('block', 'char'):
                    e['size'] = 0
            out.append(e)
        return out


def read(path):
    with open(path, 'rb') as fh:
        d = Disc(fh.read())
    return {'pack': d.pack.decode('latin-1'), 'free': d.free, 'errors': d.check(), 'entries': d.listing()}


# ----------------------------------------------------------------------------------------------- making

class Maker:
    """A UniFLEX disc laid out as docs/uniflex-format.md describes: SIR, fdns, the root, files, the free lists."""

    def __init__(self, volume, fdn_blocks, swap=0, pack=b'', fsname=b'', number=0, made=0x1144F3FC):
        self.volume, self.fdn_blocks, self.swap = volume, fdn_blocks, swap
        self.pack, self.fsname, self.number, self.made = pack, fsname, number, made
        self.next_block = 2 + fdn_blocks
        self.d = bytearray((volume + swap) * BLOCK)
        self.fdns = {}                          # n -> dict
        self.dirs = {1: []}                     # dir fdn -> [(name, fdn)]
        self.parent = {1: 1}
        self.fdns[1] = dict(mode=0x09, access=0x3F, owner=0, time=made, data=None)
        self.fdns[2] = dict(mode=0x01, access=0x80, owner=0, time=made, data=b'')
        self.dirs[1].append((b'.badblocks', 2))

    def new_fdn(self):
        n = 3
        while n in self.fdns:
            n += 1
        return n

    def lookup(self, path):
        at = 1
        for p in path.split('/')[:-1] if path else []:
            at = dict(self.dirs[at])[p.encode('latin-1')]
        return at, path.split('/')[-1].encode('latin-1')

    def mkdir(self, path, access=0x3F, owner=0, time=None):
        at, name = self.lookup(path)
        n = self.new_fdn()
        self.fdns[n] = dict(mode=0x09, access=access, owner=owner, time=time or self.made, data=None)
        self.dirs[n] = []
        self.parent[n] = at
        self.dirs[at].append((name, n))
        return n

    def file(self, path, data, access=0x1B, owner=0, time=None, holes=()):
        at, name = self.lookup(path)
        n = self.new_fdn()
        self.fdns[n] = dict(mode=0x01, access=access, owner=owner, time=time or self.made, data=data, holes=set(holes))
        self.dirs[at].append((name, n))
        return n

    def device(self, path, char, major, minor, access=0x0B):
        at, name = self.lookup(path)
        n = self.new_fdn()
        self.fdns[n] = dict(mode=0x05 if char else 0x03, access=access, owner=0, time=self.made, data=None, dev=(major, minor))
        self.dirs[at].append((name, n))
        return n

    def link(self, existing, path):
        at, name = self.lookup(existing)
        n = dict(self.dirs[at])[name]
        at2, name2 = self.lookup(path)
        self.dirs[at2].append((name2, n))

    def take(self):
        b = self.next_block
        self.next_block += 1
        return b

    def put_blocks(self, data, holes=()):
        """Writes data in blocks of its own (a hole: no block); returns the 13-entry map."""
        nblk = (len(data) + BLOCK - 1) // BLOCK
        logical = []
        for i in range(nblk):
            chunk = data[i * BLOCK:(i + 1) * BLOCK]
            if i in holes:
                assert not any(chunk), 'a hole must be zeros'
                logical.append(0)
                continue
            b = self.take()
            self.d[b * BLOCK:b * BLOCK + len(chunk)] = chunk
            logical.append(b)
        m = [0] * 13
        m[:min(10, nblk)] = logical[:10]
        rest = logical[10:]

        def write_ind(entries):
            b = self.take()
            for k, x in enumerate(entries):
                self.d[b * BLOCK + 3 * k:b * BLOCK + 3 * k + 3] = x.to_bytes(3, 'big')
            return b

        if rest:
            m[10] = write_ind(rest[:128])
            rest = rest[128:]
        if rest:
            singles = []
            while rest:
                singles.append(write_ind(rest[:128]))
                rest = rest[128:]
            assert len(singles) <= 128
            m[11] = write_ind(singles)
        return m

    def finish(self):
        links = {n: 0 for n in self.fdns}
        # directories: their contents (. and .. first), depth first
        order = []

        def visit(n):
            order.append(n)
            for _, k in self.dirs[n]:
                if k in self.dirs and k not in order:
                    visit(k)

        visit(1)
        content = {}
        for n in order:
            ents = [(b'.', n), (b'..', self.parent[n])] + self.dirs[n]
            content[n] = b''.join(m.to_bytes(2, 'big') + name.ljust(14, b'\0')[:14] for name, m in ents)
            for _, m in ents:
                links[m] += 1
        maps = {}
        for n in sorted(self.fdns):
            f = self.fdns[n]
            if f['mode'] == 0x09:
                maps[n] = self.put_blocks(content[n])
                f['size'] = len(content[n])
            elif 'dev' in f:
                maps[n] = [f['dev'][0] << 16 | f['dev'][1] << 8] + [0] * 12
                f['size'] = 0
            else:
                maps[n] = self.put_blocks(f['data'], f.get('holes', ()))
                f['size'] = len(f['data'])
        for n, f in self.fdns.items():
            o = 2 * BLOCK + (n - 1) * 64
            raw = bytes([f['mode'], f['access'], links[n]]) + f['owner'].to_bytes(2, 'big') + f['size'].to_bytes(4, 'big')
            raw += b''.join(x.to_bytes(3, 'big') for x in maps[n]) + f['time'].to_bytes(4, 'big')
            self.d[o:o + len(raw)] = raw
        # the free lists: every block after the last used one
        free = list(range(self.next_block, self.volume))
        sir = bytearray(BLOCK)
        if len(free) < 100:
            sir_list = [0] + free
            rest = []
        else:
            sir_list, rest = free[:100], free[100:]
            link = sir_list[0]
            while True:
                grp, rest = rest[:99], rest[99:]
                nxt = rest[0] if rest else 0
                rest = rest[1:] if rest else rest
                slots = [nxt] + [0] * (99 - len(grp)) + sorted(grp, reverse=True)
                for k, x in enumerate(slots):
                    self.d[link * BLOCK + 3 * k:link * BLOCK + 3 * k + 3] = x.to_bytes(3, 'big')
                if not nxt:
                    break
                link = nxt
        # the SIR
        nfdn = self.fdn_blocks * 8
        free_fdns = [n for n in range(1, nfdn + 1) if n not in self.fdns]
        struct.pack_into('>II', sir, 8, self.made, self.made)
        struct.pack_into('>H', sir, 16, self.fdn_blocks)
        sir[18:21] = (self.volume - 1).to_bytes(3, 'big')
        sir[21:24] = len(free).to_bytes(3, 'big')
        struct.pack_into('>H', sir, 24, len(free_fdns))
        sir[26:26 + len(self.fsname)] = self.fsname
        sir[40:40 + len(self.pack)] = self.pack
        struct.pack_into('>H', sir, 54, self.number)
        sir[58], sir[59] = 1, 1
        if self.swap:
            sir[60:63] = self.volume.to_bytes(3, 'big')
            struct.pack_into('>H', sir, 63, self.swap)
        sir[88] = min(50, len(free_fdns))
        for i, n in enumerate(free_fdns[:50]):
            struct.pack_into('>H', sir, 89 + 2 * i, n)
        sir[189] = len(sir_list)
        for i, x in enumerate(sir_list):
            sir[190 + 3 * i:193 + 3 * i] = x.to_bytes(3, 'big')
        self.d[BLOCK:2 * BLOCK] = sir
        return bytes(self.d)


def text(lines):
    return b''.join(l.encode() + b'\r' for l in lines)


def make(outdir):
    os.makedirs(outdir, exist_ok=True)

    def write(name, data):
        with open(os.path.join(outdir, name), 'wb') as fh:
            fh.write(data)

    t0 = 0x0A000000                              # 1985-04-25 19:22:40

    # uf-basic.dsk: 600 blocks and 16 of swap; text with CR lines and TABs, a binary, a nested tree, a hard link, a
    # file with a block never written, one big enough for a double indirect block, a 14-character name, devices
    m = Maker(600, 4, swap=16, pack=b'basic', fsname=b'uniflex', number=7, made=t0)
    m.file('readme', text(['UniFLEX test disc', '\tindented with a TAB', 'last line']), time=t0 + 60)
    m.file('Prog', bytes(range(256)) * 3, access=0x2F, owner=3, time=t0 + 120)
    m.mkdir('bin', access=0x2D)
    m.file('bin/hello', b'\x7e\x00\x00' * 50, access=0x6D, time=t0 + 180)       # set user id
    m.mkdir('src')
    m.mkdir('src/sub')
    m.file('src/sub/deep.c', text(['main()', '{', '\tprintf("deep\\n");', '}']), owner=1, time=t0 + 240)
    m.link('src/sub/deep.c', 'src/also.c')
    m.file('src/holey', b'A' * 512 + bytes(1024) + b'B' * 100, holes=(1, 2), time=t0 + 300)
    m.file('src/big.dat', bytes((i * 7) & 0xFF for i in range(160 * 512 + 33)), time=t0 + 360)
    m.file('fourteen_chars', b'x\r', time=t0 + 420)
    m.mkdir('dev')
    m.device('dev/tty0', True, 3, 1)
    m.device('dev/fd0', False, 1, 0)
    m.file('empty', b'', time=t0 + 480)
    write('uf-basic.dsk', m.finish())

    # uf-many.dsk: 2464 blocks, 64 fdn blocks: a directory of 340 files (its block map reaches the indirect block),
    # and long free lists
    m = Maker(2464, 64, pack=b'many', made=t0)
    m.mkdir('lots')
    for i in range(340):
        m.file('lots/f%03d' % i, text(['file %d' % i]), time=t0 + i)
    write('uf-many.dsk', m.finish())

    # uf-odd.dsk: a name with a control character and one with a space (a warning only)
    m = Maker(200, 2, made=t0)
    m.file('\ndynacalc', b'odd\r', time=t0)
    m.file('two words', b'space\r', time=t0)
    write('uf-odd.dsk', m.finish())

    # the damaged discs: uf-basic.dsk changed
    with open(os.path.join(outdir, 'uf-basic.dsk'), 'rb') as fh:
        good = fh.read()
    d0 = Disc(good)

    def fdn_off(n):
        return 2 * BLOCK + (n - 1) * 64

    def path_fdn(d, path):
        nodes, _, _ = d.tree()
        return [m for p, m, _ in nodes if p == path][0]

    # uf-bad-dup.dsk: Prog's first block is also readme's
    b = bytearray(good)
    r, p = path_fdn(d0, ['readme']), path_fdn(d0, ['Prog'])
    b[fdn_off(p) + 9:fdn_off(p) + 12] = b[fdn_off(r) + 9:fdn_off(r) + 12]
    write('uf-bad-dup.dsk', bytes(b))
    # uf-bad-freecount.dsk: the SIR counts one free block too many
    b = bytearray(good)
    b[BLOCK + 21:BLOCK + 24] = (d0.free + 1).to_bytes(3, 'big')
    write('uf-bad-freecount.dsk', bytes(b))
    # uf-bad-links.dsk: readme's link count 2
    b = bytearray(good)
    b[fdn_off(r) + 2] = 2
    write('uf-bad-links.dsk', bytes(b))
    # uf-bad-freefdn.dsk: Prog's fdn marked free (its entry names a free fdn; the free fdn count is off too)
    b = bytearray(good)
    b[fdn_off(p)] = 0
    write('uf-bad-freefdn.dsk', bytes(b))
    # uf-bad-chain.dsk: the SIR's link to the next free list points outside the volume
    b = bytearray(good)
    b[BLOCK + 190:BLOCK + 193] = (0xFFFF00).to_bytes(3, 'big')
    write('uf-bad-chain.dsk', bytes(b))
    # uf-bad-map.dsk: big.dat's double indirect block outside the volume (the file can't be read)
    b = bytearray(good)
    g = path_fdn(d0, ['src', 'big.dat'])
    b[fdn_off(g) + 9 + 33:fdn_off(g) + 9 + 36] = (599 + 50).to_bytes(3, 'big')
    write('uf-bad-map.dsk', bytes(b))
    # uf-short.dsk: uf-many.dsk cut to 2000 blocks (its volume goes on to 2463)
    with open(os.path.join(outdir, 'uf-many.dsk'), 'rb') as fh:
        write('uf-short.dsk', fh.read()[:2000 * BLOCK])
    # uf-noroot.dsk: the root's block never written (as ssdd8inch.dsk)
    b = bytearray(good)
    rb = d0.fdn(1).map[0]
    b[rb * BLOCK:(rb + 1) * BLOCK] = bytes(BLOCK)
    write('uf-noroot.dsk', bytes(b))


# ----------------------------------------------------------------------------------------------- comparing

def flexdw_json(flexdw, *args):
    r = subprocess.run([flexdw, *args, '--json'], capture_output=True)
    return json.loads(r.stdout.decode('utf-8'), strict=False)


ACCESS = [('ownerRead', 1), ('ownerWrite', 2), ('ownerExecute', 4), ('othersRead', 8), ('othersWrite', 0x10),
          ('othersExecute', 0x20), ('setUserId', 0x40)]


def compare_one(flexdw, img):
    """Differences between flexdw and this reader on one image (a list of strings)."""
    diffs = []
    with open(img, 'rb') as fh:
        disc = Disc(fh.read())
    mine = {tuple(e['path']): e for e in disc.listing()}
    # each directory listed on its own (ls -R stops at a directory that can't be read; this reader doesn't list in one)
    theirs, dirs = {}, [()]
    while dirs:
        d = dirs.pop(0)
        ls = flexdw_json(flexdw, 'ls', img, '/' + '/'.join(d))
        if not ls['ok']:
            return ['flexdw ls /%s failed: %s' % ('/'.join(d), ls['error'])]
        for e in ls['result']['entries']:
            theirs[tuple(e['path'])] = e
            if e['kind'] == 'dir' and not e['problem']:
                dirs.append(tuple(e['path']))
    for p in sorted(set(mine) | set(theirs)):
        a, b = mine.get(p), theirs.get(p)
        name = '/' + '/'.join(p)
        if a is None or b is None:
            # flexdw can't list under a directory it can't read; neither can this reader
            diffs.append('%s: %s' % (name, 'only flexdw lists it' if a is None else 'flexdw doesn\'t list it'))
            continue
        if bool(b['problem']) != a['problem']:
            diffs.append('%s: problem %r here, flexdw %r' % (name, a['problem'], b['problem']))
        if 'fdn' not in a:
            continue
        at = b['attrs']
        got = dict(kind=b['kind'], fdn=at['fdn'], links=at['links'], owner=at['owner'], modified=b['modified'],
                   type=at['type'], access=sum(bit for k, bit in ACCESS if at[k]))
        for k, v in got.items():
            if k == 'type' and a.get(k) is None:     # an fdn of no type UniFLEX has: the text is the program's
                continue
            if a.get(k) != v:
                diffs.append('%s: %s %r here, flexdw %r' % (name, k, a.get(k), v))
        if 'size' in a and a['size'] != b['size']:
            diffs.append('%s: size %d here, flexdw %d' % (name, a['size'], b['size']))
    # contents: flexdw get of every readable file, one at a time into a folder (names as flexdw writes them)
    files = [e for e in mine.values() if 'sha' in e]
    with tempfile.TemporaryDirectory() as out:
        for i in range(0, len(files), 200):
            batch = files[i:i + 200]
            dest = os.path.join(out, str(i))
            os.makedirs(dest)
            r = subprocess.run([flexdw, 'get', img] + ['/' + '/'.join(e['path']) for e in batch] + [dest, '--json'],
                               capture_output=True)
            res = json.loads(r.stdout.decode('utf-8'), strict=False)
            if not res['ok']:
                diffs.append('flexdw get failed: %s' % res['error'])
                continue
            hashes = {}
            for root, _, fs in os.walk(dest):
                for f in fs:
                    with open(os.path.join(root, f), 'rb') as fh:
                        h = hashlib.sha256(fh.read()).hexdigest()
                    hashes[h] = hashes.get(h, 0) + 1
            for e in batch:
                if hashes.get(e['sha'], 0) <= 0:
                    diffs.append('/%s: flexdw get gives other bytes' % '/'.join(e['path']))
                else:
                    hashes[e['sha']] -= 1
    chk = flexdw_json(flexdw, 'check', img)
    errs = disc.check()
    if bool(errs) != (chk['exit'] == 4):
        diffs.append('check: this reader finds %d errors (%s), flexdw exit %d' % (len(errs), '; '.join(errs[:3]), chk['exit']))
    return diffs


def compare(flexdw, images):
    bad = False
    for img in images:
        d = compare_one(flexdw, img)
        if d:
            bad = True
            print('%s: DIFFERENT' % img)
            for x in d[:20]:
                print('   ' + x)
            if len(d) > 20:
                print('   ... %d more' % (len(d) - 20))
        else:
            print('%s: same' % img)
    return bad


def samples(flexdw, listfile, dirs):
    """Each sample in the list: compared with this reader, and its check's outcome as the list says ("clean", or the
    start of every error flexdw's check names, one per line after the name)."""
    bad = False
    want = {}
    cur = None
    with open(listfile, encoding='utf-8') as fh:
        for line in fh:
            line = line.rstrip('\n')
            if not line.strip() or line.startswith('#'):
                continue
            if not line.startswith(' '):
                cur = line.strip()
                want[cur] = []
            elif line.strip() != 'clean':
                want[cur].append(line.strip())
    found = 0
    for name, faults in want.items():
        path = next((os.path.join(d, name) for d in dirs if os.path.exists(os.path.join(d, name))), None)
        if path is None:
            print('%s: not found' % name)
            bad = True
            continue
        found += 1
        diffs = compare_one(flexdw, path)
        chk = flexdw_json(flexdw, 'check', path)
        errors = (chk.get('result') or {}).get('errors', [])
        for f in faults:
            if not any(e.startswith(f) for e in errors):
                diffs.append('check doesn\'t name: ' + f)
        for e in errors:
            if not any(e.startswith(f) for f in faults):
                diffs.append('check names more: ' + e)
        if diffs:
            bad = True
            print('%s: DIFFERENT' % name)
            for x in diffs[:12]:
                print('   ' + x)
        else:
            print('%s: as expected (%s)' % (name, 'clean' if not faults else '%d fault%s' % (len(faults), '' if len(faults) == 1 else 's')))
    print('%d samples' % found)
    return bad


def main():
    if len(sys.argv) == 3 and sys.argv[1] == 'make':
        make(sys.argv[2])
    elif len(sys.argv) == 3 and sys.argv[1] == 'read':
        print(json.dumps(read(sys.argv[2]), indent=1))
    elif len(sys.argv) >= 4 and sys.argv[1] == 'compare':
        sys.exit(1 if compare(sys.argv[2], sys.argv[3:]) else 0)
    elif len(sys.argv) >= 5 and sys.argv[1] == 'samples':
        sys.exit(1 if samples(sys.argv[2], sys.argv[3], sys.argv[4:]) else 0)
    else:
        print(__doc__ if __doc__ else 'usage: uniflex_check.py make DIR | read IMAGE | compare FLEXDW IMAGE... | samples FLEXDW LIST DIR...')
        sys.exit(2)


if __name__ == '__main__':
    main()
