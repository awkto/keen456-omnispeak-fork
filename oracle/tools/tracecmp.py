#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Minimal reader/comparer for KEENDUMP / Omnispeak /DUMPFILE traces (oracle/docs/TRACE_FORMAT.md).

  tracecmp.py info -e 4 A.trc            # frames, live objects, TimeCount slack
  tracecmp.py cmp  -e 4 A.trc B.trc      # compare; exit 0 = equal on all compared fields

Compared: the whole gamestate block, and for every *live* object (the list from slot 0 along
`next`) every field except `sprite`; `next`/`prev` are compared as slot numbers. Not compared:
TimeCount (wall-clock), free slots, and `sprite`. A full tracediff with names is #59.
"""
import argparse, struct, sys

GS_SIZE = {4: 86, 5: 88, 6: 90}              # sizeof(gametype)
OBJARRAY = {4: 0xA807, 5: 0x9E6F, 6: 0xA995}        # DS offset of objarray (= Omnispeak ck_exe_objArrayOffset)
OBJ_SIZE, MAXACTORS = 76, 100
OBJ_FIELDS = ('obclass active needtoreact needtoclip nothink x y xdir ydir xmove ymove xspeed yspeed '
              'ticcount state shapenum priority left top right bottom midx tileleft tiletop tileright '
              'tilebottom tilemidx hitnorth hiteast hitsouth hitwest temp1 temp2 temp3 temp4 sprite next prev').split()
GS_FIELDS = {
    4: ['worldx', 'worldy'] + [f'leveldone[{i}]' for i in range(25)] + ['score', 'nextextra', 'ammo', 'drops',
        'wetsuit', 'rescued', 'keys[0]', 'keys[1]', 'keys[2]', 'keys[3]', 'mapon', 'lives', 'difficulty', 'riding'],
    5: ['worldx', 'worldy'] + [f'leveldone[{i}]' for i in range(25)] + ['score', 'nextextra', 'ammo', 'drops',
        'keycard', 'destroyed', 'numfuses', 'keys[0]', 'keys[1]', 'keys[2]', 'keys[3]', 'mapon', 'lives', 'difficulty', 'riding'],
    6: ['worldx', 'worldy'] + [f'leveldone[{i}]' for i in range(25)] + ['score', 'nextextra', 'ammo', 'drops',
        'sandwichstate', 'hookstate', 'passcardstate', 'rocketstate', 'keys[0]', 'keys[1]', 'keys[2]', 'keys[3]',
        'mapon', 'lives', 'difficulty', 'riding'],
}


def gs_fmt(ep):
    return '<2H25H2l' + 'h' * (len(GS_FIELDS[ep]) - 29)


def records(path, ep):
    d = open(path, 'rb').read()
    rs = 4 + GS_SIZE[ep] + OBJ_SIZE * MAXACTORS
    if len(d) % rs:
        sys.exit(f'{path}: {len(d)} bytes is not a multiple of the Keen {ep} record size {rs}')
    for i in range(len(d) // rs):
        r = d[i * rs:(i + 1) * rs]
        tc, = struct.unpack_from('<I', r, 0)
        gs = struct.unpack_from(gs_fmt(ep), r, 4)
        objs = [struct.unpack_from('<38H', r, 4 + GS_SIZE[ep] + OBJ_SIZE * s) for s in range(MAXACTORS)]
        yield tc, gs, objs


def slot(ep, ptr):
    if ptr == 0: return None
    q, m = divmod(ptr - OBJARRAY[ep], OBJ_SIZE)
    return q if m == 0 and 0 <= q < MAXACTORS else f'0x{ptr:04X}'


def live(ep, objs):
    out, s, seen = [], 0, set()
    while s is not None and isinstance(s, int) and s not in seen:
        seen.add(s); out.append(s); s = slot(ep, objs[s][OBJ_FIELDS.index('next')])
    return out


def norm(ep, o):
    v = dict(zip(OBJ_FIELDS, o))
    del v['sprite']
    v['next'], v['prev'] = slot(ep, v['next']), slot(ep, v['prev'])
    return v


def cmd_info(a):
    n, maxlive, slack = 0, 0, []
    for i, (tc, gs, objs) in enumerate(records(a.trace, a.episode)):
        n += 1; maxlive = max(maxlive, len(live(a.episode, objs)))
        # demo mode: at the dump point lasttimecount = 3+3i, TimeCount was set to lasttimecount+3 (i>0)
        slack.append(tc - (3 if i == 0 else 6 + 3 * i))
    print(f'{a.trace}: {n} frames, max live objects {maxlive}')
    if slack:
        print(f'TimeCount - expected-at-frame-start (tics elapsed before the dump, demo mode): '
              f'min {min(slack)} max {max(slack)}')


def cmd_cmp(a):
    ra, rb = list(records(a.a, a.episode)), list(records(a.b, a.episode))
    ep, gsn, diffs = a.episode, GS_FIELDS[a.episode], 0
    for i, ((_, ga, oa), (_, gb, ob)) in enumerate(zip(ra, rb)):
        msgs = [f'gamestate.{gsn[k]}: {ga[k]} != {gb[k]}' for k in range(len(gsn)) if ga[k] != gb[k]]
        la, lb = live(ep, oa), live(ep, ob)
        if la != lb:
            msgs.append(f'live list {la} != {lb}')
        for s in la:
            if s in lb:
                va, vb = norm(ep, oa[s]), norm(ep, ob[s])
                msgs += [f'slot {s}.{f}: {va[f]} != {vb[f]}' for f in va if va[f] != vb[f]]
        if msgs:
            diffs += 1
            if diffs <= a.max:
                print(f'frame {i}: ' + '; '.join(msgs[:8]) + (' ...' if len(msgs) > 8 else ''))
    if len(ra) != len(rb):
        print(f'frame count differs: {len(ra)} vs {len(rb)}'); diffs += 1
    print(f'{len(ra)} vs {len(rb)} frames; {diffs} differing frames' + (' -> EQUAL (compared fields)' if not diffs else ''))
    return 0 if not diffs else 1


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sp = p.add_subparsers(dest='c', required=True)
    s = sp.add_parser('info'); s.add_argument('-e', '--episode', type=int, choices=(4, 5, 6), required=True); s.add_argument('trace')
    s.set_defaults(f=cmd_info)
    s = sp.add_parser('cmp'); s.add_argument('-e', '--episode', type=int, choices=(4, 5, 6), required=True)
    s.add_argument('a'); s.add_argument('b'); s.add_argument('--max', type=int, default=10, help='differing frames to print')
    s.set_defaults(f=cmd_cmp)
    a = p.parse_args()
    sys.exit(a.f(a) or 0)


if __name__ == '__main__':
    main()
