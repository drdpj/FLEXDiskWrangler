#!/usr/bin/env python3
# FLEX Disk Wrangler - Copyright (C) 2026 Daniel Jameson - GPL v3 or later
"""FLEX and UniFLEX discs in ImageDisk (.IMD) and extended DSK files, checked independently of the program.

This script has its own ImageDisk and extended DSK writer and reader (nothing taken from the program or the core). It
lays raw FLEX and UniFLEX discs out on the discs they came from, puts them in both containers, and holds flexdw to them:

  containers_check.py make OUTDIR RAW...      the containers of each raw disc (and a list of them, OUTDIR/pairs.json)
  containers_check.py compare FLEXDW OUTDIR   flexdw reads each container exactly as its raw disc: detection, every
                                              listing, every file's bytes, the check
  containers_check.py news FLEXDW WORKDIR     flexdw new in an ImageDisk, extended DSK or DSK file: every preset, the same
                                              disc as its raw image (less a padded track 0's padding) on the disc TSC's
                                              format describes, and a file added to it as to the raw image
  containers_check.py writes FLEXDW OUTDIR    the same changes made with flexdw on a raw disc and on its containers
                                              give the same disc; the containers keep every track's density, sector IDs
                                              and order, and an IMD file's untouched tracks byte for byte; convert
                                              between them and to raw

How the discs lie (the physical shapes):
- FLEX (TSC's 6809 FLEX Adaptation Guide, Appendix B and section 9.4): a cylinder's sectors on both sides are one track,
  numbered from 1, side 1's on from side 0's. 10 sectors a side single density (FM) and 18 double (MFM) on 5.25" discs
  at 250 kbit/s; 15 and 26 on 8" discs at 500 kbit/s. A double-density disc's track 0 is single density. A raw image
  whose track 0 is stored at its own size gives that track 0; one whose track 0 is a full track (padded, or a disc that
  was double density throughout) is laid out with a full track 0 too, and, when its directory and everything else on
  track 0 fits in a single-density track 0, also with one (variant "sd0": the padding dropped).
- UniFLEX (TSC's floppy driver dmf3drvr and formatter formatfd): 8", 77 cylinders at most, 8 sectors of 512 a side
  single density, 16 double, one density throughout; block n is sector n mod (sectors a side) + 1 of track n / (sectors
  a side), cylinder = track / 2 and side = track mod 2 on a double-sided disc, side 1's sectors numbered on (+8 / +16).
  The SIR's sdenf and ssidf say which.
Sectors are laid out round each track out of order (an interleave), so that a reader going by position, not by sector
ID, gets the disc wrong.
"""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

# ImageDisk modes: (data rate, FM)
IMD_MODE = {(500, True): 0, (300, True): 1, (250, True): 2, (500, False): 3, (300, False): 4, (250, False): 5}
SIZE_CODE = {128: 0, 256: 1, 512: 2, 1024: 3}
SDE = 1000000000                              # SOURCE_DATE_EPOCH: the time of every change


# ------------------------------------------------------------------------------------------------ a disc in memory
# A disc: {'tracks': [track...]}, each track {'c', 'h', 'mode', 'secs': [(id, bytes) in the order round the track],
# 'headid': the head byte in the sectors' IDs}.

def interleave(n):
    """The order round a track of sectors 0..n-1: every second (n <= 10) or third one."""
    step = 2 if n <= 10 else 3
    order, used, at = [], [False] * n, 0
    for _ in range(n):
        while used[at]:
            at = (at + 1) % n
        order.append(at)
        used[at] = True
        at = (at + step) % n
    return order


def make_track(c, h, mode, first_id, sectors, headid=None):
    """sectors: the track's data in ID order (first_id, first_id + 1, ...)."""
    return {'c': c, 'h': h, 'mode': mode, 'headid': h if headid is None else headid,
            'secs': [(first_id + i, sectors[i]) for i in interleave(len(sectors))]}


def stored(disc):
    """The disc as one run of bytes: cylinder by cylinder, side 0 then side 1, sectors in ID order."""
    out = bytearray()
    for t in sorted(disc['tracks'], key=lambda t: (t['c'], t['h'])):
        for _, d in sorted(t['secs']):
            out += d
    return bytes(out)


def track_shapes(disc):
    """Per (c, h): (mode, IDs in the order round the track, head byte): what a container must keep."""
    return {(t['c'], t['h']): (t['mode'], [i for i, _ in t['secs']], t['headid']) for t in disc['tracks']}


# ------------------------------------------------------------------------------------------------ ImageDisk

def write_imd(disc, path, comment):
    out = bytearray(b'IMD 1.18: 01/01/2026 00:00:00\r\n' + comment.encode('ascii') + b'\x1a')
    for t in sorted(disc['tracks'], key=lambda t: (t['c'], t['h'])):
        size = len(t['secs'][0][1])
        head = t['h'] | (0x40 if t['headid'] != t['h'] else 0)
        out += bytes([t['mode'], t['c'], head, len(t['secs']), SIZE_CODE[size]])
        out += bytes(i for i, _ in t['secs'])
        if head & 0x40:
            out += bytes([t['headid']] * len(t['secs']))
        for _, d in t['secs']:
            if d == bytes([d[0]]) * len(d):
                out += bytes([2, d[0]])              # compressed: every byte the same
            else:
                out += b'\x01' + d
    with open(path, 'wb') as f:
        f.write(out)


def read_imd(path):
    """The disc, and each track's bytes in the file (to see which a save rewrote)."""
    with open(path, 'rb') as f:
        b = f.read()
    if not b.startswith(b'IMD '):
        raise ValueError('%s: not an ImageDisk file' % path)
    i = b.index(b'\x1a') + 1
    disc, raw = {'tracks': []}, {}
    while i < len(b):
        start = i
        mode, c, head, n, sz = b[i:i + 5]
        i += 5
        ids = list(b[i:i + n])
        i += n
        if head & 0x80:
            i += n                                   # cylinder map
        headids = list(b[i:i + n]) if head & 0x40 else None
        if head & 0x40:
            i += n
        size = 128 << sz
        secs = []
        for k in range(n):
            typ = b[i]
            i += 1
            if typ == 0:
                raise ValueError('%s: a sector with no data' % path)
            if (typ - 1) % 2 == 1:                   # compressed
                d = bytes([b[i]]) * size
                i += 1
            else:
                d = b[i:i + size]
                i += size
            if typ > 2:
                raise ValueError('%s: cylinder %d head %d sector %d is marked deleted or bad (type %d)' % (path, c, head & 1, ids[k], typ))
            secs.append((ids[k], d))
        h = head & 1
        disc['tracks'].append({'c': c, 'h': h, 'mode': mode, 'headid': headids[0] if headids else h, 'secs': secs})
        raw[(c, h)] = b[start:i]
    return disc, raw


# ------------------------------------------------------------------------------------------------ extended DSK

def write_edsk(disc, path, creator):
    tracks = sorted(disc['tracks'], key=lambda t: (t['c'], t['h']))
    cyls = max(t['c'] for t in tracks) + 1
    heads = max(t['h'] for t in tracks) + 1
    blocks = []
    for t in tracks:
        size = len(t['secs'][0][1])
        hdr = bytearray(b'Track-Info\r\n' + b'\0' * 4 + bytes([t['c'], t['h'], 0, 0, SIZE_CODE[size], len(t['secs']), 0x4E, 0xE5]))
        for i, d in t['secs']:
            hdr += bytes([t['c'], t['headid'], i, SIZE_CODE[size], 0, 0]) + struct.pack('<H', len(d))
        hdr += b'\0' * (256 - len(hdr))
        body = bytes(hdr) + b''.join(d for _, d in t['secs'])
        body += b'\0' * (-len(body) % 256)
        blocks.append(body)
    head = bytearray(b'EXTENDED CPC DSK File\r\nDisk-Info\r\n')
    head += creator.encode('ascii')[:14].ljust(14, b'\0') + bytes([cyls, heads, 0, 0])
    head += bytes(len(x) // 256 for x in blocks)
    head += b'\0' * (256 - len(head))
    with open(path, 'wb') as f:
        f.write(bytes(head) + b''.join(blocks))


def read_edsk(path):
    with open(path, 'rb') as f:
        b = f.read()
    if not b.startswith(b'EXTENDED CPC DSK File'):
        raise ValueError('%s: not an extended DSK file' % path)
    cyls, heads = b[0x30], b[0x31]
    sizes = b[0x34:0x34 + cyls * heads]
    disc, at = {'tracks': []}, 256
    for k in range(cyls * heads):
        if sizes[k] == 0:
            continue
        t = b[at:at + sizes[k] * 256]
        if not t.startswith(b'Track-Info'):
            raise ValueError('%s: track %d has no Track-Info' % (path, k))
        c, h, n = t[0x10], t[0x11], t[0x15]
        secs, off, headid = [], 256, h
        for s in range(n):
            info = t[0x18 + 8 * s:0x20 + 8 * s]
            ln = struct.unpack('<H', info[6:8])[0]
            secs.append((info[2], t[off:off + ln]))
            headid = info[1]
            off += ln
        disc['tracks'].append({'c': c, 'h': h, 'mode': None, 'headid': headid, 'secs': secs})
        at += sizes[k] * 256
    return disc


def read_container(path):
    return read_imd(path)[0] if path.endswith('.imd') else read_edsk(path)


# ------------------------------------------------------------------------------------------------ the shapes

# FLEX: sectors per (FLEX) track -> (sides, sectors a side, FM, data rate, single-density sectors a side for track 0)
FLEX_SHAPES = {10: (1, 10, True, 250, None), 20: (2, 10, True, 250, None), 18: (1, 18, False, 250, 10),
               36: (2, 18, False, 250, 10), 15: (1, 15, True, 500, None), 30: (2, 15, True, 500, None),
               26: (1, 26, False, 500, 15), 52: (2, 26, False, 500, 15)}


def flex_shape(data):
    """(last track, sectors, track 0's sectors as stored) from the SIR and the size, or None."""
    if len(data) < 1024 or len(data) % 256:
        return None
    last, spt = data[512 + 38], data[512 + 39]
    if spt not in FLEX_SHAPES or last < 1:
        return None
    n = len(data) // 256
    if n == (last + 1) * spt:
        return last, spt, spt
    t0 = n - last * spt
    if not 5 <= t0 < spt:
        return None
    # stored short only if sector (0, t0) ends track 0's directory (its link 0/0 or off track 0): a full track 0 in a
    # file cut short by the same amount links on to (0, t0 + 1) (disc-format.md, "Track 0")
    link = data[(t0 - 1) * 256:(t0 - 1) * 256 + 2]
    return (last, spt, t0) if link[0] != 0 or link[1] == 0 else None


def flex_track0_use(data, spt):
    """The highest sector of track 0 anything uses: the boot sectors, the SIR, the directory's chain."""
    hi, t, s, seen = 5, 0, 5, set()
    while (t, s) != (0, 0) and (t, s) not in seen and len(seen) < 1000:
        seen.add((t, s))
        if t == 0:
            hi = max(hi, s)
        off = (t * spt + s - 1) * 256
        if off + 2 > len(data):
            break
        t, s = data[off], data[off + 1]
    return hi


def flex_disc(data, last, spt, t0, sd0=False):
    """data's FLEX disc laid out on its physical disc; sd0: a padded track 0 as the single-density track it was."""
    sides, side, fm, rate, sdside = FLEX_SHAPES[spt]
    disc = {'tracks': []}
    t0side = (t0 // sides) if t0 < spt else (sdside if sd0 else side)
    pos = 0
    for c in range(last + 1):
        per = t0side if c == 0 else side
        dd = c == 0 and per != side
        mode = IMD_MODE[(rate, True if dd else fm)]
        sec = [data[pos + k * 256:pos + (k + 1) * 256] for k in range(per * sides)]
        pos += (t0 if c == 0 else spt) * 256      # a padded track 0's padding is skipped (sd0)
        for h in range(sides):
            disc['tracks'].append(make_track(c, h, mode, 1 + h * per, sec[h * per:(h + 1) * per]))
    return disc


def uniflex_disc(data):
    """data's UniFLEX disc on its 8" disc, or None (a Winchester, or not whole cylinders)."""
    if len(data) < 1024 or len(data) % 512:
        return None
    sir = data[512:1024]
    den, sid = sir[58], sir[59]
    fdnblocks, last = int.from_bytes(sir[16:18], 'big'), int.from_bytes(sir[18:21], 'big')
    if den not in (0, 1) or not 0 < fdnblocks < last or last >= len(data) // 512 + 8:
        return None                              # a Winchester, or no UniFLEX SIR (an OS-9 disc)
    sides, spt = (2 if sid else 1), (16 if den else 8)
    per = sides * spt
    blocks = len(data) // 512
    if blocks % per or not 1 <= blocks // per <= 80:
        return None
    mode = IMD_MODE[(500, not den)]
    disc = {'tracks': []}
    for c in range(blocks // per):
        for h in range(sides):
            first = (c * sides + h) * spt
            disc['tracks'].append(make_track(c, h, mode, 1 + h * spt, [data[(first + k) * 512:(first + k + 1) * 512] for k in range(spt)]))
    return disc


# ------------------------------------------------------------------------------------------------ make

def make(outdir, raws):
    os.makedirs(outdir, exist_ok=True)
    pairs = []
    for raw in raws:
        with open(raw, 'rb') as f:
            data = f.read()
        base = os.path.splitext(os.path.basename(raw))[0].replace(' ', '_')
        variants = []
        fs = flex_shape(data)
        if fs:
            last, spt, t0 = fs
            variants.append(('', flex_disc(data, last, spt, t0), 'flex'))
            sd = FLEX_SHAPES[spt][4]
            if t0 == spt and sd and flex_track0_use(data, spt) <= sd * FLEX_SHAPES[spt][0]:
                variants.append(('-sd0', flex_disc(data, last, spt, t0, sd0=True), 'flex'))
        else:
            ud = uniflex_disc(data)
            if ud:
                variants.append(('', ud, 'uniflex'))
        if not variants:
            print('%s: no physical shape known for it, skipped' % os.path.basename(raw))
            continue
        rawcopy = os.path.join(outdir, base + '.raw')
        shutil.copyfile(raw, rawcopy)
        for suffix, disc, fs_id in variants:
            for kind in ('imd', 'edsk'):
                name = os.path.join(outdir, '%s%s.%s' % (base, suffix, 'imd' if kind == 'imd' else 'dsk'))
                (write_imd if kind == 'imd' else write_edsk)(disc, name, 'containers_check.py')
                again = read_container(name)
                if stored(again) != stored(disc):
                    raise SystemExit('%s: this script reads back something else' % name)
                pairs.append({'raw': rawcopy, 'container': name, 'fs': fs_id, 'sd0': suffix == '-sd0'})
        print('%s: %s' % (os.path.basename(raw), ', '.join('%s%s (%d tracks)' % (fs_id, s, len(d['tracks'])) for s, d, fs_id in variants)))
    with open(os.path.join(outdir, 'pairs.json'), 'w') as f:
        json.dump(pairs, f, indent=1)
    return 0


# ------------------------------------------------------------------------------------------------ flexdw

def run(flexdw, *args, env=None):
    p = subprocess.run([flexdw, '--json'] + list(args), capture_output=True, timeout=120, env=env)
    try:
        return p.returncode, json.loads(p.stdout.decode('utf-8'))
    except ValueError:
        return p.returncode, {'error': p.stdout.decode('utf-8', 'replace') + p.stderr.decode('utf-8', 'replace')}


def tree_hashes(flexdw, img):
    """Every file flexdw extracts, by path, and its SHA-256."""
    with tempfile.TemporaryDirectory() as tmp:
        code, _ = run(flexdw, 'get', img, '/', tmp)
        out = {}
        for root, _, files in os.walk(tmp):
            for fn in files:
                p = os.path.join(root, fn)
                with open(p, 'rb') as f:
                    out[os.path.relpath(p, tmp)] = hashlib.sha256(f.read()).hexdigest()
        return code, out


def reading(flexdw, img):
    """What flexdw says of a disc, less what depends on the file it is in."""
    _, info = run(flexdw, 'info', img)
    r = info.get('result') or {}
    chosen = r.get('chosen') or {}
    lcode, ls = run(flexdw, 'ls', img, '-R')
    ccode, check = run(flexdw, 'check', img)
    gcode, files = tree_hashes(flexdw, img)
    vols = [{k: v for k, v in vol.items() if k not in ('readOnlyWhy',)} for vol in r.get('volumes', [])]
    return {'fs': chosen.get('fsId'), 'tier': chosen.get('tier'), 'safeToWrite': chosen.get('safeToWrite'),
            'volumes': vols, 'ls': (lcode, ls.get('result')), 'check': (ccode, check.get('result')), 'get': (gcode, files)}


def compare(flexdw, outdir):
    with open(os.path.join(outdir, 'pairs.json')) as f:
        pairs = json.load(f)
    bad, cache = 0, {}
    for p in pairs:
        if p['raw'] not in cache:
            cache[p['raw']] = reading(flexdw, p['raw'])
        want, have = cache[p['raw']], reading(flexdw, p['container'])
        name = os.path.basename(p['container'])
        _, info = run(flexdw, 'info', p['container'])
        kind = ((info.get('result') or {}).get('container') or {}).get('kind')
        if p['sd0']:
            # the same disc with its track 0 single density: the raw image's padding isn't there, so only the files,
            # the listing and the check must agree (the raw is padded; this one is exact with track 0 at its own size)
            for k in ('volumes',):
                want = dict(want, **{k: None})
                have = dict(have, **{k: None})
        diffs = [k for k in want if want[k] != have[k]]
        if have['fs'] != p['fs']:
            diffs.append('read as %s' % have['fs'])
        if diffs:
            bad += 1
            print('%s: DIFFERENT from %s in %s' % (name, os.path.basename(p['raw']), ', '.join(diffs)))
            for k in diffs:
                if k in want:
                    print('  raw:       %s\n  container: %s' % (json.dumps(want[k])[:600], json.dumps(have[k])[:600]))
        else:
            print('%s (%s): reads as its raw disc: %s, %s, %d files%s' % (name, kind, have['fs'], have['tier'], len(have['get'][1]),
                  '' if have['check'][0] == 0 else ' (both find errors)'))
    return bad


# ------------------------------------------------------------------------------------------------ writes

def logical(fs, data, spt=None, t0=None):
    """A disc's bytes as {sector or block: bytes}: FLEX (track, sector), UniFLEX block number."""
    if fs == 'uniflex':
        return {n: data[n * 512:(n + 1) * 512] for n in range(len(data) // 512)}
    out, pos, t = {}, 0, 0
    while pos < len(data):
        n = t0 if t == 0 else spt
        for s in range(n):
            out[(t, s + 1)] = data[pos + s * 256:pos + (s + 1) * 256]
        pos += n * 256
        t += 1
    return out


def flex_ops(flexdw, img, host, env):
    """FLEX changes: a file in, one out, a rename, the volume name; each its own save."""
    _, ls = run(flexdw, 'ls', img)
    names = [e['name'] for e in (ls.get('result') or {}).get('entries', [])
             if not e.get('problem') and not any(e['attrs'].get(k) for k in ('write', 'delete'))]   # FLEX refuses protected ones
    steps = [('put', img, host, '/')]
    if names:
        steps.append(('rm', img, '/' + names[0]))
    if len(names) > 1:
        steps.append(('mv', img, '/' + names[1], '/RENAMED.TXT'))
    steps.append(('title', img, 'CONTAIN'))
    return steps


def uniflex_ops(flexdw, img, host, env):
    return [('mkdir', img, '/contain'), ('put', img, host, '/contain'), ('mv', img, '/contain', '/moved'), ('title', img, 'contain')]


def writes(flexdw, outdir):
    with open(os.path.join(outdir, 'pairs.json')) as f:
        pairs = json.load(f)
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(SDE), TZ='UTC')
    bad = 0
    work = tempfile.mkdtemp(dir=outdir)
    host = os.path.join(work, 'host.txt')
    with open(host, 'wb') as f:
        f.write(b''.join(b'line %d of a file put on a disc in a container\r' % i for i in range(30)))
    done = 0
    for p in pairs:
        name = os.path.basename(p['container'])
        _, info = run(flexdw, 'info', p['raw'])
        r = (info.get('result') or {})
        if any(v.get('readOnly') for v in r.get('volumes', [])):
            print('%s: its raw disc is read-only, so no changes' % name)
            continue
        ccode, _ = run(flexdw, 'check', p['raw'])
        if ccode != 0:
            print('%s: its raw disc has errors, so no changes' % name)
            continue
        raw = os.path.join(work, 'r-' + os.path.basename(p['raw']))
        cont = os.path.join(work, name)
        shutil.copyfile(p['raw'], raw)
        shutil.copyfile(p['container'], cont)
        before = read_container(cont)
        imd_before = read_imd(cont)[1] if cont.endswith('.imd') else None
        ops = flex_ops if p['fs'] == 'flex' else uniflex_ops
        problems = []
        for img in (raw, cont):
            for step in ops(flexdw, img, host, env):
                code, out = run(flexdw, *step, env=env)
                if code != 0:
                    problems.append('%s %s: exit %d %s' % (step[0], os.path.basename(img), code, out.get('error')))
        with open(raw, 'rb') as f:
            rdata = f.read()
        after = read_container(cont)
        cdata = stored(after)
        if p['fs'] == 'flex':
            last, spt, t0 = flex_shape(rdata)
            ct0 = sum(len(t['secs']) for t in after['tracks'] if t['c'] == 0)
            want, have = logical('flex', rdata, spt, t0), logical('flex', cdata, spt, ct0)
            if p['sd0']:
                with open(p['raw'], 'rb') as f:
                    orig = logical('flex', f.read(), spt, t0)
                pad = [k for k in want if k not in have]
                if any(want[k] != orig[k] for k in pad):
                    problems.append('the change wrote into the raw disc\'s track 0 padding, which the single-density disc lacks')
                want = {k: v for k, v in want.items() if k in have}
        else:
            want, have = logical('uniflex', rdata), logical('uniflex', cdata)
        if want != have:
            diff = sorted(k for k in set(want) | set(have) if want.get(k) != have.get(k))
            problems.append('the disc differs from the raw one changed the same way, in %d sectors (first %s)' % (len(diff), diff[:4]))
        sb, sa = track_shapes(before), track_shapes(after)
        if cont.endswith('.dsk'):
            sb = {k: (None,) + v[1:] for k, v in sb.items()}
        if sb != sa:
            problems.append('the tracks\' shapes changed: %s' % sorted(k for k in set(sb) | set(sa) if sb.get(k) != sa.get(k))[:6])
        if imd_before is not None:
            imd_after = read_imd(cont)[1]
            tb = {(t['c'], t['h']): sorted(t['secs']) for t in before['tracks']}
            ta = {(t['c'], t['h']): sorted(t['secs']) for t in after['tracks']}
            kept = [k for k in tb if tb[k] == ta.get(k)]
            if any(imd_before[k] != imd_after.get(k) for k in kept):
                problems.append('a track whose sectors didn\'t change was rewritten in the IMD file')
        # convert: to the other container and to raw; each reads as the changed disc
        for kind, ext in (('imd', 'imd'), ('edsk', 'dsk'), ('raw', 'img')):
            out = os.path.join(work, 'conv.' + ext)
            code, res = run(flexdw, 'convert', cont, out, '--container=' + kind)
            if code != 0:
                problems.append('convert to %s: exit %d %s' % (kind, code, res.get('error')))
                continue
            if kind == 'raw':
                with open(out, 'rb') as f:
                    conv = f.read()
            else:
                conv = stored(read_container(out))
                if track_shapes(read_container(out)).keys() != sa.keys():
                    problems.append('convert to %s: other tracks' % kind)
            if conv != cdata:
                problems.append('convert to %s: another disc' % kind)
            elif kind == 'raw' and reading(flexdw, out)['get'] != reading(flexdw, cont)['get']:
                problems.append('convert to raw: flexdw reads its files differently')
            os.remove(out)
        code, res = run(flexdw, 'convert', raw, os.path.join(work, 'no.imd'), '--container=imd')
        if code == 0:
            problems.append('a raw disc converted to ImageDisk (it doesn\'t say how its sectors lie)')
        done += 1
        if problems:
            bad += 1
            print('%s: DIFFERENT\n  %s' % (name, '\n  '.join(problems)))
        else:
            print('%s: %d changes give the raw disc\'s result; tracks kept%s; converts to ImageDisk, DSK and raw' %
                  (name, len(ops(flexdw, cont, host, env)), ', untouched IMD tracks byte for byte' if imd_before is not None else ''))
    shutil.rmtree(work)
    if not done:
        print('no disc was changed')
        bad += 1
    return bad


def news(flexdw, work):
    os.makedirs(work, exist_ok=True)
    env = dict(os.environ, SOURCE_DATE_EPOCH=str(SDE), TZ='UTC')
    host = os.path.join(work, 'host.txt')
    with open(host, 'wb') as f:
        f.write(b''.join(b'a file put on a new disc, line %d\r' % i for i in range(30)))
    _, lst = run(flexdw, 'new', '--list')
    bad = 0
    for fmt in lst['result']['formats']:
        fid, fs = fmt['id'], fmt['fs']
        uniform = fs == 'uniflex' or fmt['track0Sectors'] == fmt['sectorsPerTrack']
        raw = os.path.join(work, fid + '.raw')
        problems = []
        steps = [('new', raw, '--format=' + fid, '--title=NEW', '--replace')]
        for c in ('imd', 'edsk', 'dsk'):
            steps.append(('new', os.path.join(work, '%s.%s' % (fid, c)), '--format=' + fid, '--container=' + c, '--title=NEW', '--replace'))
        for st in steps:
            code, out = run(flexdw, *st, env=env)
            refused = st[1].endswith('.dsk') and not uniform
            if refused != (code != 0):
                problems.append('%s: exit %d %s' % (os.path.basename(st[1]), code, out.get('error')))
        for img in [raw] + [os.path.join(work, '%s.%s' % (fid, c)) for c in ('imd', 'edsk')]:
            code, out = run(flexdw, 'put', img, host, '/', env=env)
            if code:
                problems.append('put %s: %s' % (os.path.basename(img), out.get('error')))
        with open(raw, 'rb') as f:
            data = f.read()
        for c in ('imd', 'edsk'):
            img = os.path.join(work, '%s.%s' % (fid, c))
            d = read_container(img)
            if fs == 'flex':
                last, spt, t0 = flex_shape(data)
                sides, side, fm, rate, sd = FLEX_SHAPES[spt]
                ct0 = sum(len(t['secs']) for t in d['tracks'] if t['c'] == 0)
                want = logical('flex', data, spt, t0)
                have = logical('flex', stored(d), spt, ct0)
                want = {k: v for k, v in want.items() if k in have}
                if fmt['track0Sectors'] != ct0:
                    problems.append('%s: track 0 has %d sectors, not %d' % (c, ct0, fmt['track0Sectors']))
                for t in d['tracks']:
                    n = len(t['secs'])
                    ids = sorted(i for i, _ in t['secs'])
                    if ids != list(range(1 + t['h'] * n, 1 + (t['h'] + 1) * n)):
                        problems.append('%s: cylinder %d side %d IDs %s' % (c, t['c'], t['h'], ids[:3]))
                        break
                    single = t['c'] == 0 and n < side or fm
                    if c == 'imd' and t['mode'] != IMD_MODE[(rate, single)]:
                        problems.append('%s: cylinder %d side %d mode %d' % (c, t['c'], t['h'], t['mode']))
                        break
            else:
                want, have = data, stored(d)
            if want != have:
                problems.append('%s: another disc than the raw image' % c)
            code, _ = run(flexdw, 'check', img)
            if code:
                problems.append('%s: the check fails' % c)
        if problems:
            bad += 1
            print('%s: DIFFERENT\n  %s' % (fid, '\n  '.join(problems)))
        else:
            print('%s: new in ImageDisk and extended DSK%s as the raw image, on its disc; a file added the same' %
                  (fid, ', DSK' if uniform else ' (not DSK: track 0 its own shape)'))
    shutil.rmtree(work)
    return bad


def main():
    a = sys.argv[1:]
    if len(a) >= 3 and a[0] == 'make':
        sys.exit(make(a[1], a[2:]))
    if len(a) == 3 and a[0] == 'compare':
        sys.exit(1 if compare(a[1], a[2]) else 0)
    if len(a) == 3 and a[0] == 'news':
        sys.exit(1 if news(a[1], a[2]) else 0)
    if len(a) == 3 and a[0] == 'writes':
        sys.exit(1 if writes(a[1], a[2]) else 0)
    print(__doc__)
    sys.exit(2)


if __name__ == '__main__':
    main()
