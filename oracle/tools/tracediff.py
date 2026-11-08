#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""First divergence between two KEENDUMP / Omnispeak /DUMPFILE traces, human-readable (#59).

  tracediff.py -e 4 A.trc B.trc [--context N] [--all-fields]
  tracediff.py -e 4 A.trc B.trc --summary        # one line; exit 0 equal, 1 divergent, 2 usage/parse error

Comparison rules are those of oracle/docs/TRACE_FORMAT.md and oracle/tools/tracecmp.py: TimeCount, free slots
and `sprite` are not compared; only live objects (slot 0 along `next`) are; next/prev compare
as slot numbers. --all-fields compares everything byte-for-byte instead (TimeCount, all 100
slots, raw sprite/next/prev). State addresses are shown with DOS and Omnispeak names from
oracle/docs/statemap-k<N>.tsv, object classes with the names of the CK_DEF.H classtype enum.
"""
import argparse, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tracecmp as tc

DOCS = os.path.join(HERE, '..', 'docs')

CLASSES = {
    4: ('nothing inertobj keenobj stunshotobj bonusobj slugobj oracleobj classtype_7 eggobj madmushroomobj '
        'arachnutobj skypestobj wormouthobj thundercloudobj berkeloidobj bounderobj inchwormobj footobj '
        'lickobj mimrockobj platformobj dopefishobj schoolfishobj pixieobj lindseyobj lightningobj '
        'treasureeaterobj eggbirdobj classtype_28 classtype_29 scubaobj mshotobj mineobj stunnedobj flagobj').split(),
    5: ('nothing inertobj keenobj stunshotobj mshotobj bonusobj platformobj stunnedobj flagobj sparkyobj '
        'mineobj slicestarobj roboredobj spirogripobj amptonobj cannonobj volteobj shelleyobj spindredobj '
        'shikadimasterobj shikadiobj petobj spherefulobj scottieobj teleporterobj qedobj').split(),
}
KEY_FIELDS = ['obclass', 'x', 'y', 'xspeed', 'yspeed', 'xdir', 'ydir', 'state', 'ticcount']


class TraceError(Exception):
    pass


def load_statemap(ep, path=None):
    """{ds_offset: (dos_name, omni_name)} from oracle/docs/statemap-k<ep>.tsv ({} if missing)."""
    path = path or os.path.join(DOCS, f'statemap-k{ep}.tsv')
    out = {}
    try:
        f = open(path)
    except OSError:
        return out
    with f:
        for line in f:
            if line.startswith('#') or line.startswith('status\t'):
                continue
            c = line.rstrip('\n').split('\t')
            if len(c) > 4 and c[4].startswith('0x'):
                out[int(c[4], 16)] = (c[1], c[3])
    return out


class Namer:
    def __init__(self, ep, statemap=None):
        self.ep = ep
        self.states = load_statemap(ep) if statemap is None else statemap

    def cname(self, v):
        n = CLASSES[self.ep]
        return n[v] if 0 <= v < len(n) else f'class{v}'

    def cls(self, v):
        return f'{v} ({self.cname(v)})'

    def state(self, v):
        if v == 0:
            return '0 (NULL)'
        s = self.states.get(v)
        return f'0x{v:04X} ({s[0]} / {s[1]})' if s else f'0x{v:04X} (unknown state)'

    def ptr(self, v):
        if v == 0:
            return 'NULL'
        s = tc.slot(self.ep, v)
        return f'slot {s}' if isinstance(s, int) else f'{s}'

    def field(self, name, v):
        if name == 'obclass': return self.cls(v)
        if name == 'state': return self.state(v)
        if name in ('next', 'prev'): return v if not isinstance(v, int) or v is None else f'slot {v}'
        return str(v)


def load(path, ep):
    try:
        return list(tc.records(path, ep))
    except SystemExit as e:                       # tracecmp reports a bad size via sys.exit(msg)
        raise TraceError(str(e.code))
    except OSError as e:
        raise TraceError(f'{path}: {e.strerror}')


def obj_view(ep, o, all_fields):
    v = dict(zip(tc.OBJ_FIELDS, o))
    if not all_fields:
        del v['sprite']
        v['next'], v['prev'] = tc.slot(ep, v['next']), tc.slot(ep, v['prev'])
    return v


def frame_diffs(ep, ra, rb, all_fields):
    """List of (kind, slot, field, va, vb) for one frame; first entry is the headline."""
    d = []
    tca, ga, oa = ra
    tcb, gb, ob = rb
    if all_fields and tca != tcb:
        d.append(('header', None, 'TimeCount', tca, tcb))
    names = tc.GS_FIELDS[ep]
    d += [('gs', None, names[k], ga[k], gb[k]) for k in range(len(names)) if ga[k] != gb[k]]
    la, lb = tc.live(ep, oa), tc.live(ep, ob)
    if la != lb and not all_fields:
        d.append(('live', None, 'live list', la, lb))
    slots = range(tc.MAXACTORS) if all_fields else [s for s in la if s in lb]
    for s in slots:
        va, vb = obj_view(ep, oa[s], all_fields), obj_view(ep, ob[s], all_fields)
        d += [('obj', s, f, va[f], vb[f]) for f in sorted(va, key=lambda f: f != 'state') if va[f] != vb[f]]  # state first
    return d


def find_divergence(ep, A, B, all_fields=False):
    """Return (frame, diffs) of the first differing frame, or None. Frame-count mismatch separate."""
    for i, (a, b) in enumerate(zip(A, B)):
        d = frame_diffs(ep, a, b, all_fields)
        if d:
            return i, d
    return None


def window_table(ep, namer, A, B, frame, slot, ctx, all_fields):
    lines = []
    lo = max(0, frame - ctx)
    for i in range(lo, frame + 1):
        va, vb = obj_view(ep, A[i][2][slot], True), obj_view(ep, B[i][2][slot], True)
        for label, v, o in (('A', va, vb), ('B', vb, va)):
            cells = []
            for f in KEY_FIELDS[:-2]:
                x = namer.cname(v[f]) if f == 'obclass' else str(v[f])
                cells.append(f'{x:>8}' + ('*' if v[f] != o[f] else ' '))
            st = namer.state(v['state']) + ('*' if v['state'] != o['state'] else '')
            lines.append(f'{i:>6} {label} ' + ' '.join(cells) + f' {st:<46}'
                         + f'{v["ticcount"]}' + ('*' if v['ticcount'] != o['ticcount'] else ''))
    head = f'{"frame":>6}   ' + ' '.join(f'{f:>9}' for f in KEY_FIELDS[:-2]) + f' {"state":<46}ticcount'
    return [head] + lines


def report(ep, namer, A, B, pa, pb, ctx, all_fields, maxlines=12):
    out = []
    n = min(len(A), len(B))
    res = find_divergence(ep, A, B, all_fields)
    if res:
        frame, d = res
        kind, s, f, va, vb = d[0]
        out.append(f'First divergence at frame {frame} ({len(d)} differing fields in that frame)')
        out.append(f'  A = {pa}\n  B = {pb}')
        for kind, s, f, va, vb in d[:maxlines]:
            if kind == 'obj':
                cls = namer.cname(obj_view(ep, A[frame][2][s], True)['obclass'])
                clsb = namer.cname(obj_view(ep, B[frame][2][s], True)['obclass'])
                who = f'slot {s} [{cls}]' + ('' if cls == clsb else f' / B [{clsb}]')
                out.append(f'  {who} .{f}: A={namer.field(f, va)}  B={namer.field(f, vb)}')
            elif kind == 'gs':
                out.append(f'  gamestate.{f}: A={va}  B={vb}')
            elif kind == 'live':
                out.append(f'  live object list differs (spawn/remove): A={va}  B={vb}')
            else:
                out.append(f'  {f}: A={va}  B={vb}')
        if len(d) > maxlines:
            out.append(f'  ... {len(d) - maxlines} more')
        if ctx > 0:
            slot = next((x[1] for x in d if x[0] == 'obj'), None)
            if slot is None:
                slot = 0
                why = 'slot 0 (player)'
            else:
                why = f'slot {slot}'
            out.append(f'\nLast {min(ctx, frame)} frame(s) before, then the divergent frame, key fields of {why} (* = differs):')
            out += ['  ' + l for l in window_table(ep, namer, A, B, frame, slot, ctx, all_fields)]
    if len(A) != len(B):
        out.append(f'Frame count differs: A has {len(A)}, B has {len(B)}'
                   + ('' if res else f' (first {n} frames are equal on compared fields)'))
    if not res and len(A) == len(B):
        out.append(f'EQUAL: {len(A)} frames identical on compared fields' + (' (all fields)' if all_fields else ''))
    return out, bool(res) or len(A) != len(B)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('-e', '--episode', type=int, choices=(4, 5), required=True)
    p.add_argument('a'); p.add_argument('b')
    p.add_argument('--context', type=int, default=8, metavar='N', help='frames of history before the divergence (default 8)')
    p.add_argument('--summary', action='store_true', help='one line; exit 0 equal / 1 divergent / 2 error')
    p.add_argument('--all-fields', action='store_true', help='also compare TimeCount, sprite, raw next/prev and free slots')
    try:
        a = p.parse_args(argv)
    except SystemExit as e:
        return 2 if e.code else 0
    ep = a.episode
    try:
        A, B = load(a.a, ep), load(a.b, ep)
    except TraceError as e:
        print(f'tracediff: {e}', file=sys.stderr)
        return 2
    namer = Namer(ep)
    if a.summary:
        res = find_divergence(ep, A, B, a.all_fields)
        if res:
            frame, d = res
            kind, s, f, va, vb = d[0]
            where = f'slot {s} .{f}' if kind == 'obj' else (f'gamestate.{f}' if kind == 'gs' else f)
            if kind == 'obj':
                va, vb = namer.field(f, va), namer.field(f, vb)
            print(f'DIVERGE frame {frame} {where}: A={va} B={vb}'
                  + (f' (frame counts {len(A)} vs {len(B)})' if len(A) != len(B) else ''))
            return 1
        if len(A) != len(B):
            print(f'DIVERGE frame count {len(A)} vs {len(B)} (first {min(len(A), len(B))} frames equal)')
            return 1
        print(f'EQUAL {len(A)} frames')
        return 0
    lines, bad = report(ep, namer, A, B, a.a, a.b, a.context, a.all_fields)
    print('\n'.join(lines))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
