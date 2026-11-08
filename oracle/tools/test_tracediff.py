#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Unit tests for oracle/tools/tracediff.py using synthetic in-memory traces (no game data)."""
import contextlib, io, os, re, struct, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import tracecmp as tc
import tracediff as td

EP = 4
BASE = tc.OBJARRAY[EP]
S_KEENSTAND, S_PAUSELOOK = 0x098C, 0x09AA      # oracle/docs/statemap-k4.tsv


def ptr(slot):
    return 0 if slot is None else BASE + 76 * slot


def blank_obj(slot, nxt, prv):
    o = [0] * 38
    o[tc.OBJ_FIELDS.index('next')], o[tc.OBJ_FIELDS.index('prev')] = ptr(nxt), ptr(prv)
    return o


def make_frame(i, live=(0, 1), timecount=None):
    """One record as (timecount, gs tuple, objs)."""
    gs = [0] * len(tc.GS_FIELDS[EP])
    objs = [blank_obj(s, None, None) for s in range(tc.MAXACTORS)]
    for k, s in enumerate(live):
        objs[s] = blank_obj(s, live[k + 1] if k + 1 < len(live) else None, live[k - 1] if k else None)
    def setf(s, **kw):
        for f, v in kw.items():
            objs[s][tc.OBJ_FIELDS.index(f)] = v
    setf(0, obclass=2, active=3, x=1000 + 8 * i, y=500, xspeed=8, state=S_KEENSTAND, ticcount=4)
    if 1 in live:
        setf(1, obclass=5, active=1, x=2000, y=600, xspeed=0, state=S_PAUSELOOK, ticcount=60 - i % 60)
    return [6 + 3 * i if timecount is None else timecount, tuple(gs), objs], setf


def pack(ep, frames):
    out = bytearray()
    for t, gs, objs in frames:
        out += struct.pack('<I', t) + struct.pack(tc.gs_fmt(ep), *gs)
        for o in objs:
            out += struct.pack('<38H', *[v & 0xFFFF for v in o])
    return bytes(out)


def trace(n, mutate=None, **kw):
    fr = []
    for i in range(n):
        f, setf = make_frame(i, **kw)
        if mutate:
            mutate(i, f, setf)
        fr.append(f)
    return fr


class T(unittest.TestCase):
    def setUp(self):
        self.d = tempfile.TemporaryDirectory()
        self.addCleanup(self.d.cleanup)

    def write(self, name, frames):
        p = os.path.join(self.d.name, name)
        with open(p, 'wb') as f:
            f.write(pack(EP, frames))
        return p

    def run_td(self, a, b, *extra):
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            rc = td.main(['-e', str(EP), a, b, *extra])
        return rc, out.getvalue(), err.getvalue()

    def test_equal(self):
        a, b = self.write('a', trace(10)), self.write('b', trace(10))
        rc, out, _ = self.run_td(a, b)
        self.assertEqual(rc, 0); self.assertIn('EQUAL', out)
        rc, out, _ = self.run_td(a, b, '--summary')
        self.assertEqual((rc, out.strip()), (0, 'EQUAL 10 frames'))

    def test_timecount_sprite_free_slot_ignored(self):
        def m(i, f, setf):
            f[0] += 2
            f[2][5][tc.OBJ_FIELDS.index('x')] = 77                   # free slot
            f[2][0][tc.OBJ_FIELDS.index('sprite')] = 0xD622 + 32
        a, b = self.write('a', trace(6)), self.write('b', trace(6, m))
        self.assertEqual(self.run_td(a, b)[0], 0)
        rc, out, _ = self.run_td(a, b, '--all-fields')
        self.assertEqual(rc, 1); self.assertIn('TimeCount', out)

    def test_state_divergence_named(self):
        def m(i, f, setf):
            if i >= 7:
                setf(0, state=S_PAUSELOOK, ticcount=60)
        a, b = self.write('a', trace(12)), self.write('b', trace(12, m))
        rc, out, _ = self.run_td(a, b, '--context', '3')
        self.assertEqual(rc, 1)
        self.assertIn('frame 7', out)
        self.assertIn('slot 0 [keenobj] .state', out)
        self.assertIn('s_keenstand / CK_ACT_keenStanding', out)
        self.assertIn('s_keenpauselook / CK_ACT_keenIdle', out)
        self.assertIn('.ticcount', out)
        self.assertEqual(len(re.findall(r'^\s+[4-7] [AB] ', out, re.M)), 8)   # frames 4..7 x A/B
        self.assertNotIn('   3 A ', out)
        rc, out, _ = self.run_td(a, b, '--summary')
        self.assertEqual(rc, 1); self.assertRegex(out, r'^DIVERGE frame 7 slot 0 \.state')
        self.assertEqual(len(out.strip().splitlines()), 1)

    def test_other_object_and_class(self):
        def m(i, f, setf):
            if i >= 3:
                setf(1, x=2000 + 16)
        a, b = self.write('a', trace(5)), self.write('b', trace(5, m))
        rc, out, _ = self.run_td(a, b)
        self.assertIn('frame 3', out); self.assertIn('slot 1 [slugobj] .x: A=2000  B=2016', out)

    def test_gamestate_and_live_list(self):
        def g(i, f, setf):
            if i == 2:
                f[1] = (0,) * 26 + (5,) + (0,) * (len(f[1]) - 27)
        a, b = self.write('a', trace(4)), self.write('b', trace(4, g))
        rc, out, _ = self.run_td(a, b)
        self.assertIn('frame 2', out); self.assertIn('gamestate.', out)
        def spawn(i, f, setf):
            pass
        fa = trace(4)
        fb = trace(4, live=(0,))
        rc, out, _ = self.run_td(self.write('a', fa), self.write('b', fb))
        self.assertIn('live object list differs', out)

    def test_frame_count(self):
        a, b = self.write('a', trace(10)), self.write('b', trace(8))
        rc, out, _ = self.run_td(a, b)
        self.assertEqual(rc, 1); self.assertIn('Frame count differs: A has 10, B has 8', out)
        rc, out, _ = self.run_td(a, b, '--summary')
        self.assertEqual(rc, 1); self.assertIn('frame count 10 vs 8', out)

    def test_errors_exit_2(self):
        a = self.write('a', trace(3))
        bad = os.path.join(self.d.name, 'bad'); open(bad, 'wb').write(b'x' * 100)
        self.assertEqual(self.run_td(a, bad, '--summary')[0], 2)
        self.assertEqual(self.run_td(a, os.path.join(self.d.name, 'nope'))[0], 2)
        with contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(td.main(['-e', '9', a, a]), 2)

    def test_unknown_state(self):
        def m(i, f, setf):
            if i == 1: setf(0, state=0x1234)
        rc, out, _ = self.run_td(self.write('a', trace(3)), self.write('b', trace(3, m)))
        self.assertIn('0x1234 (unknown state)', out)

    def test_tracecmp_still_agrees(self):
        a, b = self.write('a', trace(5)), self.write('b', trace(5, lambda i, f, s: s(0, y=1) if i == 4 else None))
        self.assertEqual(len(list(tc.records(a, EP))), 5)

    def test_class_tables_match_header(self):
        h = os.path.join(HERE, '..', 'dos', 'KEEN4-6', 'CK_DEF.H')
        if not os.path.exists(h):
            self.skipTest('CK_DEF.H absent')
        t = open(h, errors='replace').read()
        blk = t[t.index('typedef enum\n{\n\tnothing'):t.index('} classtype;')]
        for ep, key in ((4, 'KEEN4'), (5, 'KEEN5')):
            m = re.search(r'#(?:if|elif) defined %s\n(.*?)(?:#elif|#endif)' % key, blk, re.S)
            names = ['nothing', 'inertobj', 'keenobj', 'stunshotobj'] + re.findall(r'^\s*(\w+),', m.group(1), re.M)
            self.assertEqual(td.CLASSES[ep], names)

    def test_statemap_loaded(self):
        sm = td.load_statemap(4)
        if sm:
            self.assertEqual(sm[0x098C][0], 's_keenstand')


if __name__ == '__main__':
    unittest.main()
