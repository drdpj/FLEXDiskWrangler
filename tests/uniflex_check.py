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
#   uniflex_check.py writes FLEXDW DIR [SAMPLES]
#                                             changes made by flexdw and by the Kernel model (UniFLEX's kernel and
#                                             utilities, docs/uniflex-format.md "How UniFLEX writes"), byte for byte after
#                                             every step, on DIR's synthetic discs (make) and copies of healthy samples
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


# ----------------------------------------------------------------------------------------------- writing

class Refused(Exception):
    pass


class Kernel:
    """UniFLEX's 6809 kernel changing a disc, written from docs/uniflex-format.md ("How UniFLEX writes") without the
    program's code: the kernel's routines (aldb, frdb / mkfch, alfdn / filfdn, frfdn, mapfil, rmvfil, crfil, link,
    unlink, chprm, chown, update) and on them the utilities (crdir, copy, kill, rename / move, perms, owner). Every
    fdn written gets the time `now`, as does the SIR (update). The image is changed in place (a bytearray)."""

    CDBLKS, CFDN, DIRENT = 100, 50, 16

    def __init__(self, data, now):
        self.d = bytearray(data)
        self.now = now

    # --- the SIR, in place
    def _sir(self, off, size):
        o = BLOCK + off
        return int.from_bytes(self.d[o:o + size], 'big')

    def _set_sir(self, off, size, v):
        o = BLOCK + off
        self.d[o:o + size] = v.to_bytes(size, 'big')

    def _free(self, i):
        return self._sir(190 + 3 * i, 3)

    def _set_free(self, i, b):
        self._set_sir(190 + 3 * i, 3, b)

    def blk(self, b):
        return self.d[b * BLOCK:(b + 1) * BLOCK]

    # --- blocks: aldb (with filfr), frdb (with mkfch)
    def aldb(self):
        """A block from the free list, cleared; 0 when the disc is full (the SIR's count is lowered all the same)."""
        n = self._sir(189, 1)
        if n == 0:
            return 0
        n -= 1
        self._set_sir(189, 1, n)
        b = self._free(n)
        if b == 0:
            return 0
        if n == 0:                                  # filfr: the link block's list becomes the SIR's
            self.d[BLOCK + 190:BLOCK + 490] = self.d[b * BLOCK:b * BLOCK + 300]
            self._set_sir(189, 1, self.CDBLKS)
        self._set_sir(21, 3, self._sir(21, 3) - 1)
        self.d[b * BLOCK:(b + 1) * BLOCK] = bytes(BLOCK)
        return b

    def frdb(self, b):
        n = self._sir(189, 1)
        if n == 0:
            self._set_free(0, 0)
            n = 1
        elif n == self.CDBLKS:                     # mkfch: entries 1-99 sorted, largest first, into the freed block
            ents = [self._free(i) for i in range(self.CDBLKS)]
            ents = ents[:1] + sorted(ents[1:], reverse=True)
            for i, x in enumerate(ents):               # sorted in the SIR itself, then copied out
                self._set_free(i, x)
            o = b * BLOCK
            self.d[o:o + 300] = b''.join(x.to_bytes(3, 'big') for x in ents)   # 300-511 left as they were
            n = 0
        self._set_free(n, b)
        self._set_sir(189, 1, n + 1)
        self._set_sir(21, 3, self._sir(21, 3) + 1)

    # --- fdns: the slot, alfdn (with filfdn), frfdn
    def fo(self, n):
        return 2 * BLOCK + (n - 1) * 64

    def fdn(self, n):
        return Fdn(self.d[self.fo(n):self.fo(n) + 64], n)

    def put(self, n, **kv):
        """Changes fields of fdn n and writes it with the time now (updfdn)."""
        o = self.fo(n)
        for k, v in kv.items():
            if k == 'mode': self.d[o] = v
            elif k == 'access': self.d[o + 1] = v
            elif k == 'links': self.d[o + 2] = v
            elif k == 'owner': self.d[o + 3:o + 5] = v.to_bytes(2, 'big')
            elif k == 'size': self.d[o + 5:o + 9] = v.to_bytes(4, 'big')
            elif k == 'map': self.d[o + 9:o + 48] = b''.join(x.to_bytes(3, 'big') for x in v)
            else: raise KeyError(k)
        self.d[o + 48:o + 52] = self.now.to_bytes(4, 'big')

    def filfdn(self):
        bad = set()
        bb = Disc(bytes(self.d)).blocks(self.fdn(2))
        if not isinstance(bb, str):
            bad = set(x for x in bb[0] if x)
        listed = []
        nblocks = self._sir(16, 2)
        for blk in range(2, nblocks + 2):
            if blk in bad:
                continue
            for k in range(8):
                n = (blk - 2) * 8 + k + 1
                if self.d[self.fo(n)] == 0:
                    listed.append(n)
                    if len(listed) == self.CFDN:
                        break
            if len(listed) == self.CFDN:
                break
        for i, n in enumerate(listed):
            self._set_sir(89 + 2 * i, 2, n)
        self._set_sir(88, 1, len(listed))

    def alfdn(self):
        if self._sir(88, 1) == 0:
            self.filfdn()
            if self._sir(88, 1) == 0:
                raise Refused('no free fdn')
        k = self._sir(88, 1) - 1
        self._set_sir(88, 1, k)
        n = self._sir(89 + 2 * k, 2)
        self.d[self.fo(n):self.fo(n) + 64] = bytes(64)
        self.put(n, mode=0x01)
        self._set_sir(24, 2, self._sir(24, 2) - 1)
        return n

    def frfdn(self, n):
        self._set_sir(24, 2, self._sir(24, 2) + 1)
        k = self._sir(88, 1)
        if k < self.CFDN:
            self._set_sir(89 + 2 * k, 2, n)
            self._set_sir(88, 1, k + 1)

    # --- a file's blocks: mapfil when writing, rmvfil
    def _ind(self, b, k):
        return int.from_bytes(self.d[b * BLOCK + 3 * k:b * BLOCK + 3 * k + 3], 'big')

    def _set_ind(self, b, k, v):
        self.d[b * BLOCK + 3 * k:b * BLOCK + 3 * k + 3] = v.to_bytes(3, 'big')

    def mapw(self, n, i):
        """The block of logical block i of fdn n, taking blocks as mapfil does when writing (map slot first, then
        each indirect level, then the data block); 0 when the disc is full."""
        f = self.fdn(n)
        m = list(f.map)

        def slot(s):
            if not m[s]:
                b = self.aldb()
                if not b:
                    return 0
                m[s] = b
                self.put(n, map=m)
            return m[s]

        def through(b, k):
            x = self._ind(b, k)
            if not x:
                x = self.aldb()
                if not x:
                    return 0
                self._set_ind(b, k, x)
            return x

        if i < 10:
            return slot(i)
        i -= 10
        if i < 128:
            b = slot(10)
            return through(b, i) if b else 0
        i -= 128
        if i < 128 * 128:
            b = slot(11)
            b = through(b, i >> 7) if b else 0
            return through(b, i & 127) if b else 0
        i -= 128 * 128
        b = slot(12)
        b = through(b, i >> 14) if b else 0
        b = through(b, (i >> 7) & 127) if b else 0
        return through(b, i & 127) if b else 0

    def write(self, n, pos, data):
        """filwr: data at byte pos, block by block; the size grows to the end of what is written."""
        f = self.fdn(n)
        size = f.size
        done = 0
        while done < len(data):
            p = pos + done
            i, off = divmod(p, BLOCK)
            take = min(BLOCK - off, len(data) - done)
            b = self.mapw(n, i)
            if not b:
                raise Refused('disc full')
            self.d[b * BLOCK + off:b * BLOCK + off + take] = data[done:done + take]
            done += take
            if p + take > size:
                size = p + take
        self.put(n, size=size)

    def _frind(self, b, level):
        for k in range(127, -1, -1):
            x = self._ind(b, k)
            if x:
                if level == 1:
                    self.frdb(x)
                else:
                    self._frind(x, level - 1)
        self.frdb(b)

    def rmvfil(self, n):
        f = self.fdn(n)
        if f.mode & 0x06:
            return
        for s, level in ((12, 3), (11, 2), (10, 1)):
            if f.map[s]:
                self._frind(f.map[s], level)
        for s in range(9, -1, -1):
            if f.map[s]:
                self.frdb(f.map[s])
        self.put(n, size=0, map=[0] * 13)

    # --- directories
    def entries(self, n):
        f = self.fdn(n)
        c = Disc(bytes(self.d)).content(f)
        return [(u16(c, o), c[o + 2:o + 16]) for o in range(0, len(c) - 15, 16)]

    @staticmethod
    def name14(name):
        name = name if isinstance(name, bytes) else name.encode('latin-1')
        return name[:14].ljust(14, b'\0')

    def search(self, d, name):
        """pthnm in directory d: (slot of the name or None, first empty slot before it or None)."""
        first = None
        for k, (m, nm) in enumerate(self.entries(d)):
            if m == 0:
                if first is None:
                    first = k
                continue
            if nm == self.name14(name):
                return k, first
        return None, first

    def lookup(self, path):
        """(parent directory, last name, fdn or None) of a path from the root."""
        parts = [p for p in path.split('/') if p]
        if not parts:
            return 1, '', 1
        at = 1
        for p in parts[:-1]:
            k, _ = self.search(at, p)
            if k is None:
                raise Refused('no directory ' + p)
            at = self.entries(at)[k][0]
            if self.fdn(at).mode != 0x09:
                raise Refused('not a directory ' + p)
        k, _ = self.search(at, parts[-1])
        return at, parts[-1], (self.entries(at)[k][0] if k is not None else None)

    def enter(self, d, name, n):
        """crfil4: the entry in the first empty slot, or after the last."""
        _, first = self.search(d, name)
        pos = (first if first is not None else self.fdn(d).size // self.DIRENT) * self.DIRENT
        self.write(d, pos, n.to_bytes(2, 'big') + self.name14(name))

    # --- the system calls
    def crfil(self, path, mode, access):
        d, name, n = self.lookup(path)
        if n is not None:
            raise Refused('exists')
        n = self.alfdn()
        self.put(n, mode=mode | 0x01, access=access & 0x7F, links=1, owner=0)
        self.enter(d, name, n)
        return n

    def link(self, old, new):
        _, _, n = self.lookup(old)
        d, name, m = self.lookup(new)
        if m is not None:
            raise Refused('exists')
        f = self.fdn(n)
        if f.links < 127:
            self.put(n, links=f.links + 1)
        self.enter(d, name, n)

    def unlink(self, path):
        d, name, n = self.lookup(path)
        if n is None:
            raise Refused('no file')
        k, _ = self.search(d, name)
        self.write(d, k * self.DIRENT, b'\0\0')
        f = self.fdn(n)
        links = f.links - 1 if f.links < 127 else f.links
        self.put(n, links=links)
        if links <= 0 and not (n == d):              # the last link: frefdn frees it
            self.rmvfil(n)
            o = self.fo(n)
            self.d[o] = 0
            self.frfdn(n)
            self.put(n)

    def update(self):
        self.d[BLOCK:BLOCK + 4] = bytes(4)
        self._set_sir(12, 4, self.now)

    # --- the utilities
    def crdir(self, path):
        n = self.crfil(path, 0x08, 0x3F)
        self.put(n, owner=0)                        # chown to the user (the system manager here)
        self.link(path, path.rstrip('/') + '/.')
        parent = path.rstrip('/').rsplit('/', 1)[0] or '/'
        self.link(parent, path.rstrip('/') + '/..')
        return n

    def copy(self, data, path, access, owner=0):
        """copy (or a file brought in): create, then the data written in order."""
        d, name, n = self.lookup(path)
        if n is not None:                           # create over a file: its blocks freed, its fdn kept
            if self.fdn(n).mode == 0x09:
                raise Refused('a directory')
            self.rmvfil(n)
        else:
            n = self.crfil(path, 0, access)
        if data:
            self.write(n, 0, data)
        return n

    def kill(self, path):
        d, name, n = self.lookup(path)
        if n is None:
            raise Refused('no file')
        if self.fdn(n).mode == 0x09:
            if any(m and nm.rstrip(b'\0') not in (b'.', b'..') for m, nm in self.entries(n)):
                raise Refused('directory not empty')
            self.unlink(path.rstrip('/') + '/.')
            self.unlink(path.rstrip('/') + '/..')
        self.unlink(path)

    def remove(self, path):
        """The program's delete: a directory's contents first, depth first in the directory's order, then each
        directory as kill +d removes it."""
        _, _, n = self.lookup(path)
        if n is None:
            raise Refused('no file')
        if self.fdn(n).mode == 0x09:
            for m, nm in self.entries(n):
                nm = nm.rstrip(b'\0')
                if m and nm not in (b'.', b'..'):
                    self.remove(path.rstrip('/') + '/' + nm.decode('latin-1'))
        self.kill(path)

    def rename(self, old, new):
        """rename / move: link to the new name (a file there unlinked first), then unlink the old. A directory moved
        to another directory: then its .. unlinked and the new parent linked as its .. (link and unlink as the system
        manager; UniFLEX's own tools move none). Not into itself or below."""
        od, oname, n = self.lookup(old)
        nd, nname, m = self.lookup(new)
        is_dir = self.fdn(n).mode == 0x09
        if is_dir and nd != od:
            o = '/' + '/'.join(p for p in old.split('/') if p)
            t = '/' + '/'.join(p for p in new.split('/') if p)
            if t.startswith(o + '/'):
                raise Refused('into itself')
        if m == n:
            if nd == od and self.name14(nname) == self.name14(oname):
                return
            raise Refused('the same file')
        if m is not None:
            if self.fdn(m).mode == 0x09:
                raise Refused('a directory')
            self.unlink(new)
        self.link(old, new)
        self.unlink(old)
        if is_dir and nd != od:
            self.unlink(new.rstrip('/') + '/..')
            parent = new.rstrip('/').rsplit('/', 1)[0] or '/'
            self.link(parent, new.rstrip('/') + '/..')

    def perms(self, path, access):
        _, _, n = self.lookup(path)
        f = self.fdn(n)
        self.put(n, access=(f.access & 0x80) | (access & 0x7F))

    def owner(self, path, uid):
        _, _, n = self.lookup(path)
        self.put(n, owner=uid)


def format_disc(cylinders, heads, sectors, density, sides, fsname=b'', pack=b'', number=0, now=0):
    """A new disc as TSC's format 1.09 lays one out (uos-s3/unibug/format with formatfd: strd, calcs, mksir, mffd,
    mfbl, mkrfdn, mkrtf, mkfree, dofb), with no bad blocks, no swap space, and block 0 (format's bootstrap) empty."""
    volbc = cylinders * heads * sectors
    fdnbc = volbc // (100 // 3)
    frebc = volbc - (fdnbc + 3)
    fdnfc = fdnbc * 8 - 2                                   # rtfcnt: the root and /.badblocks
    blkic = (frebc + 1) % 100 or 100
    frseq = 3 + fdnbc
    d = bytearray(volbc * BLOCK)
    sir = bytearray(BLOCK)
    struct.pack_into('>II', sir, 8, now, now)
    struct.pack_into('>H', sir, 16, fdnbc)
    sir[18:21] = (volbc - 1).to_bytes(3, 'big')
    sir[21:24] = frebc.to_bytes(3, 'big')
    struct.pack_into('>H', sir, 24, fdnfc)
    for off, n in ((26, fsname), (40, pack)):
        n = n.split(b'\0')[0][:14]
        sir[off:off + len(n)] = n
    struct.pack_into('>H', sir, 54, number)
    sir[58], sir[59] = density, sides
    nf = min(50, fdnfc)                                     # mffd
    sir[88] = nf
    for i in range(nf):
        struct.pack_into('>H', sir, 89 + 2 * i, 3 + i)
    sir[189] = blkic                                        # mfbl: stored last entry first, frseq counting up
    i = blkic
    while i > 0:
        i -= 1
        sir[190 + 3 * i:193 + 3 * i] = frseq.to_bytes(3, 'big')
        if i:
            frseq += 1
    frblk = frseq
    d[BLOCK:2 * BLOCK] = sir
    f = bytearray(128)                                      # mkrfdn: the templates rtfdn1, rtfdn2
    f[0:3] = b'\x09\x3f\x02'
    f[7:9] = (48).to_bytes(2, 'big')                        # fdb rtsiz, after fcb 0,0,0,0
    f[10:12] = (fdnbc + 2).to_bytes(2, 'big')               # std 10,x: the root's block
    f[64:67] = b'\x01\x80\x01'
    for o in (48, 64 + 48):
        f[o:o + 4] = now.to_bytes(4, 'big')
    d[2 * BLOCK:2 * BLOCK + 128] = f
    r = (fdnbc + 2) * BLOCK                                 # mkrtf: rtdir
    for k, (n, name) in enumerate(((1, b'.'), (1, b'..'), (2, b'.badblocks'))):
        d[r + 16 * k:r + 16 * k + 16] = n.to_bytes(2, 'big') + name.ljust(14, b'\0')
    while frseq != volbc:                                   # mkfree: dofb until the end
        buf = bytearray(BLOCK)
        x, count, ended = 300, 99, False
        while count:
            frseq += 1
            if frseq == volbc:
                ended = True
                break
            x -= 3
            buf[x:x + 3] = frseq.to_bytes(3, 'big')
            count -= 1
        if not ended:
            frseq += 1
            if frseq != volbc:
                buf[0:3] = frseq.to_bytes(3, 'big')
        d[frblk * BLOCK:(frblk + 1) * BLOCK] = buf
        frblk = frseq
    return bytes(d)


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


# ----------------------------------------------------------------------------------------------- writes

UNIX_1980 = 315532800                        # 1 January 1980, 00:00 GMT, in Unix seconds
DEFAULT_ACCESS = 0x1B                        # a host file that brings no access: rw-rw-, as UniFLEX's shell makes a file
SDE = 1000000000                             # SOURCE_DATE_EPOCH for flexdw: the time of every change
NOW = SDE - UNIX_1980


def pattern(n, seed):
    return bytes((i * 31 + seed * 7 + (i >> 9)) & 0xFF for i in range(n))


def scenarios(syn, samples):
    """(title, image, steps). Steps: ('put', dir, name, data[, replace]), ('mkdir', path), ('rm', path),
    ('mv', from, to), ('access', path, bits), ('owner', path, uid), ('title', text)."""
    out = []
    basic = os.path.join(syn, 'uf-basic.dsk')
    out.append(('files of every size on uf-basic.dsk', basic, [
        ('put', '/', 'empty2', b''),
        ('put', '/', 'one', b'1'),
        ('put', '/', 'b511', pattern(511, 1)),
        ('put', '/', 'b512', pattern(512, 2)),
        ('put', '/', 'b513', pattern(513, 3)),
        ('put', '/', 'direct10', pattern(10 * 512, 4)),                  # the last direct block
        ('put', '/', 'single1', pattern(10 * 512 + 1, 5)),              # the single indirect block's first
        ('put', '/src', 'double', pattern(138 * 512 + 700, 6)),         # past the single: the double indirect
        ('rm', '/single1'),
        ('rm', '/src/double'),
        ('put', '/', 'again', pattern(3000, 7)),
    ]))
    out.append(('directories on uf-basic.dsk', basic, [
        ('mkdir', '/new'),
        ('mkdir', '/new/inner'),
        ('put', '/new/inner', 'x', pattern(700, 8)),
        ('put', '/new', 'y', pattern(100, 9)),
        ('mv', '/new/y', '/new/inner/y2'),                              # a file moved
        ('mv', '/new/inner', '/new/renamed'),                           # a directory renamed in place
        ('mv', '/new/renamed', '/src/renamed'),                         # a directory moved: its .. now /src
        ('mv', '/src/renamed', '/src/renamed/deeper'),                  # refused: into itself
        ('mv', '/src', '/src/renamed/src'),                             # refused: into a directory inside it
        ('mv', '/src/sub', '/sub'),                                     # up to the root, its file and hard link along
        ('mkdir', '/src/renamed/in'),
        ('mv', '/src/renamed', '/bin/r2'),                              # moved again, with a directory in it
        ('mv', '/readme', '/README'),                                   # case matters
        ('mv', '/Prog', '/fourteen_chars'),                             # refused: the name exists
        ('rm', '/bin/r2'),                                              # a moved tree deleted: its .. found right
        ('rm', '/src'),                                                 # a tree: contents first
        ('rm', '/new'),
        ('mkdir', '/new'),
        ('rm', '/.badblocks'),                                          # refused
    ]))
    out.append(('attributes, replacing, names on uf-basic.dsk', basic, [
        ('access', '/readme', 0x3F),
        ('access', '/bin/hello', 0x01),                                  # set user id off, as everything but r
        ('owner', '/readme', 12),
        ('put', '/', 'readme', pattern(2000, 10), True),                 # replaced: its fdn, access, owner kept
        ('put', '/', 'readme', pattern(9, 11)),                         # refused: exists
        ('put', '/bin', 'hello', b'', True),                            # replaced by nothing
        ('title', 'newpack'),
    ]))
    out.append(('the fdn list running out and refilled on uf-basic.dsk', basic,
                [('mkdir', '/many')] + [('put', '/many', 'f%02d' % i, pattern(40, i)) for i in range(16)]
                + [('rm', '/many/f03'), ('rm', '/many/f07')] + [('put', '/many', 'g%02d' % i, pattern(40, i)) for i in range(3)]
                + [('put', '/', 'toomany%d' % i, b'z') for i in range(8)]))                    # the last refused
    many = os.path.join(syn, 'uf-many.dsk')
    out.append(('long free lists on uf-many.dsk', many, [
        ('put', '/', 'huge', pattern(900 * 512 + 5, 12)),                # list blocks taken (filfr)
        ('rm', '/huge'),                                                 # and full lists written (mkfch)
        ('put', '/', 'huge2', pattern(400 * 512, 13)),
        ('rm', '/lots'),
        ('put', '/', 'end', pattern(1234, 14)),
    ]))
    out.append(('a disc filled on uf-basic.dsk', basic, [
        ('put', '/', 'fill', pattern(300 * 512, 15)),
        ('put', '/', 'over', pattern(200 * 512, 16)),                    # refused: the disc is full
        ('rm', '/fill'),
        ('put', '/', 'over', pattern(200 * 512, 16)),
    ]))
    if samples:
        for name in ('dsdd8inch', 'fd0_nopassword', 'pw03', 'wd1'):
            p = os.path.join(samples, name + '.dsk')
            if os.path.exists(p):
                out.append(('changes on the sample ' + name, p, [
                    ('mkdir', '/dwtest'),
                    ('put', '/dwtest', 'a', pattern(30000, 17)),
                    ('put', '/dwtest', 'b', pattern(80000, 18)),
                    ('mv', '/dwtest/a', '/dwtest/c'),
                    ('access', '/dwtest/c', 0x2F),
                    ('rm', '/dwtest/b'),
                    ('rm', '/dwtest'),
                ]))
    return out


def writes(flexdw, syn, samples=None):
    """Each scenario's steps done by flexdw (one invocation, one save, each) and by the Kernel model; after every step
    the image must be the model's byte for byte, and a step the model refuses must fail and change nothing."""
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(SDE), TZ='UTC')
    bad = 0
    with tempfile.TemporaryDirectory() as tmp:
        def run(*args):
            return subprocess.run([flexdw, '--json'] + [str(a) for a in args], capture_output=True, timeout=300, env=env)

        keys = (('ownerRead', 0x01), ('ownerWrite', 0x02), ('ownerExecute', 0x04), ('othersRead', 0x08),
                ('othersWrite', 0x10), ('othersExecute', 0x20), ('setUserId', 0x40))
        for title, image, steps in scenarios(syn, samples):
            img = os.path.join(tmp, 'img.dsk')
            with open(image, 'rb') as fh:
                data = fh.read()
            with open(img, 'wb') as fh:
                fh.write(data)
            model = Kernel(data, NOW)
            problems = []
            for k, step in enumerate(steps, 1):
                op, args = step[0], step[1:]
                before = bytes(model.d)
                refused = None
                try:
                    cmd = None
                    if op == 'put':
                        d, name, payload = args[:3]
                        replace = len(args) > 3 and args[3]
                        p = os.path.join(tmp, 'host', name)
                        os.makedirs(os.path.dirname(p), exist_ok=True)
                        with open(p, 'wb') as fh:
                            fh.write(payload)
                        path = d.rstrip('/') + '/' + name
                        cmd = ('put', img, p, d) + (('--replace',) if replace else ())
                        if model.lookup(path)[2] is not None and not replace:
                            raise Refused('exists')
                        model.copy(payload, path, DEFAULT_ACCESS)
                    elif op == 'mkdir':
                        model.crdir(args[0])
                        cmd = ('mkdir', img, args[0])
                    elif op == 'rm':
                        cmd = ('rm', img, args[0], '-r')
                        if args[0] == '/.badblocks':
                            raise Refused('.badblocks')
                        model.remove(args[0])
                    elif op == 'mv':
                        cmd = ('mv', img, args[0], args[1])
                        if model.lookup(args[1])[2] is not None:
                            raise Refused('exists (the core never renames onto a name)')
                        model.rename(args[0], args[1])
                    elif op == 'access':
                        model.perms(args[0], args[1])
                        cmd = ('attr', img, args[0]) + tuple('--set=%s=%s' % (key, 'true' if args[1] & bit else 'false') for key, bit in keys)
                    elif op == 'owner':
                        model.owner(args[0], args[1])
                        cmd = ('attr', img, args[0], '--set=owner=%d' % args[1])
                    elif op == 'title':
                        model.d[BLOCK + 40:BLOCK + 54] = args[0].encode('latin-1')[:14].ljust(14, b'\0')
                        cmd = ('title', img, args[0])
                    else:
                        raise ValueError(op)
                except Refused as e:
                    refused = str(e)
                    model.d = bytearray(before)
                else:
                    model.update()
                r = run(*cmd)
                with open(img, 'rb') as fh:
                    got = fh.read()
                if refused:
                    if r.returncode == 0 or got != before:
                        problems.append('step %d %s: the model refuses it (%s), flexdw %s' % (k, step[:2], refused,
                                        'did it' if r.returncode == 0 else 'changed the image'))
                    continue
                if r.returncode != 0:
                    problems.append('step %d %s: flexdw failed: %s' % (k, step[:2], (r.stdout + r.stderr).decode()[-300:]))
                    model.d = bytearray(got)
                    continue
                if got != bytes(model.d):
                    diff = [i for i in range(min(len(got), len(model.d))) if got[i] != model.d[i]]
                    blocks = sorted(set(i // BLOCK for i in diff))
                    problems.append('step %d %s: %d bytes differ from the model, blocks %s (first at %d: flexdw %02x, model %02x)'
                                    % (k, step[:2], len(diff), blocks[:12], diff[0], got[diff[0]], model.d[diff[0]]))
                    model.d = bytearray(got)
                    continue
                errs = Disc(got).check()
                if errs:
                    problems.append('step %d %s: the result has errors: %s' % (k, step[:2], errs[:5]))
            print('%s: %s' % (title, 'ok (%d steps)' % len(steps) if not problems else 'FAILED'))
            for p in problems:
                print('   ' + p)
            bad += bool(problems)
        # new discs: flexdw new against format_disc, byte for byte, then the disc filled and emptied again
        for pid, shape in (('uniflex-ss-sd', (77, 1, 8, 0, 0)), ('uniflex-ss-dd', (77, 1, 16, 1, 0)),
                           ('uniflex-ds-sd', (77, 2, 8, 0, 1)), ('uniflex-ds-dd', (77, 2, 16, 1, 1))):
            img = os.path.join(tmp, pid + '.dsk')
            r = run('new', img, '--format=' + pid, '--title=pack ' + pid[-5:])
            want = format_disc(*shape, pack=('pack ' + pid[-5:]).encode(), now=NOW)
            got = open(img, 'rb').read() if os.path.exists(img) else b''
            problems = []
            if r.returncode != 0:
                problems.append('flexdw new failed: ' + (r.stdout + r.stderr).decode()[-300:])
            elif got != want:
                diff = [i for i in range(min(len(got), len(want))) if got[i] != want[i]]
                problems.append('%d bytes differ from format_disc (sizes %d, %d), blocks %s' % (len(diff), len(got), len(want), sorted(set(i // BLOCK for i in diff))[:10]))
            elif Disc(got).check():
                problems.append('the new disc has errors: %s' % Disc(got).check()[:3])
            else:
                model = Kernel(got, NOW)
                data = pattern(Disc(got).free * BLOCK // 2, 19)
                p = os.path.join(tmp, 'host', 'half')
                with open(p, 'wb') as fh:
                    fh.write(data)
                for cmd, act in ((('put', img, p, '/'), lambda: model.copy(data, '/half', DEFAULT_ACCESS)),
                                 (('rm', img, '/half'), lambda: model.remove('/half'))):
                    act()
                    model.update()
                    if run(*cmd).returncode != 0 or open(img, 'rb').read() != bytes(model.d):
                        problems.append('%s on the new disc: not the model\'s result' % cmd[0])
                        break
            print('new disc %s: %s' % (pid, 'ok' if not problems else 'FAILED'))
            for p in problems:
                print('   ' + p)
            bad += bool(problems)
    return bad


def main():
    if len(sys.argv) in (4, 5) and sys.argv[1] == 'writes':
        sys.exit(1 if writes(sys.argv[2], sys.argv[3], sys.argv[4] if len(sys.argv) == 5 else None) else 0)
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
