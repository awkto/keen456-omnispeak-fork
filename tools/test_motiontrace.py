#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Tests for tools/motiontrace.py. Synthetic data only; no game data needed."""
import os, struct, sys, tempfile, unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, '..', 'oracle', 'tools'))   # tracecmp, tracediff
import motiontrace as mt
import tracecmp as tc


def rows_from(xs):
    return [dict(frame=i, alive=True, scr_x=x, scr_y=0) for i, x in enumerate(xs)]


def sc(xs, key='scr_x'):
    return mt.score(mt.steps(rows_from(xs), key))


def write_trace(path, keen_px, mapon=1):
    """Keen-4 trace: Keen (slot 0) at x pixel keen_px[i] on flat ground; all other slots free."""
    ep = 4
    rs = 4 + tc.GS_SIZE[ep] + tc.OBJ_SIZE * tc.MAXACTORS
    gsoff = 4 + 2 * 2 + 25 * 2 + 4 + 4 + 2 * 2 + 4 + 4 * 2        # offset of `mapon` in the record
    f = open(path, 'wb')
    prev = None
    for px in keen_px:
        r = bytearray(rs)
        struct.pack_into('<h', r, gsoff, mapon)
        o = 4 + tc.GS_SIZE[ep]
        v = dict(obclass=2, active=2, needtoclip=1, x=px * 16, y=300 * 16 - 32 * 16, xmove=(px - prev) * 16 if prev is not None else 0,
                 left=px * 16, right=px * 16 + 24 * 16, top=300 * 16 - 48 * 16, bottom=300 * 16,
                 hitnorth=1)
        v['left'], v['right'] = px * 16, px * 16 + 16 * 16
        for k, val in v.items():
            struct.pack_into('<H', r, o + 2 * tc.OBJ_FIELDS.index(k), val & 0xFFFF)
        f.write(r)
        prev = px
    f.close()


class Scores(unittest.TestCase):
    def test_even_motion_scores_zero(self):
        s = sc([3 * i for i in range(100)])
        self.assertAlmostEqual(s['step_std'], 0.0)
        self.assertEqual((s['alt'], s['uneven']), (0, 0))

    def test_standing_still_scores_zero(self):
        s = sc([7] * 50)
        self.assertEqual((s['step_std'], s['alt'], s['uneven'], s['frozen_pct']), (0.0, 0, 0, 100.0))

    def test_alternating_one_pixel_jitter(self):
        s = sc([100 + (i % 2) for i in range(101)])
        self.assertEqual(s['alt'], 99)            # every consecutive step pair reverses by 1 px
        self.assertAlmostEqual(s['step_std'], 1.0)

    def test_2_4_step_pattern_is_uneven(self):
        xs = [0]
        for i in range(60):
            xs.append(xs[-1] + (2 if i % 2 == 0 else 4))
        s = sc(xs)
        self.assertEqual(s['uneven'], 59)
        self.assertEqual(s['alt'], 0)
        self.assertAlmostEqual(s['step_std'], 1.0)

    def test_gap_frames_and_jumps_are_skipped(self):
        rows = rows_from([0, 3, 6, 9, 500, 503, 506])
        rows[4]['jump'] = True
        self.assertEqual(mt.steps(rows, 'scr_x'), [3, 3, 3, 3, 3])
        rows[2]['alive'] = False
        self.assertEqual(mt.steps(rows, 'scr_x'), [3, 3, 3])


class Camera(unittest.TestCase):
    def run_trace(self, px, grid):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, 't.trc')
            write_trace(p, px)
            return mt.trace_rows(p, 4, grid=grid)

    def test_even_walk_gives_even_screen_motion(self):
        rows = self.run_trace([1000 + 4 * i for i in range(120)], grid=2)
        # camera starts centred; once Keen reaches the scroll window it follows at his speed
        st = mt.steps(rows[40:], 'cam_x')
        self.assertTrue(st and all(s == 4 for s in st), st[:10])
        self.assertAlmostEqual(mt.score(mt.steps(rows[40:], 'scr_x'))['step_std'], 0.0)
        self.assertAlmostEqual(mt.score(mt.steps(rows[40:], 'cam_x'))['step_std'], 0.0)

    def test_odd_speed_on_2px_grid_is_uneven(self):
        rows = self.run_trace([1000 + 3 * i for i in range(120)], grid=2)
        s = mt.score(mt.steps(rows[40:], 'cam_x'))
        self.assertGreater(s['step_std'], 0.5)    # 3 px/frame lands as 2,4,2,4 on the 2-px grid
        self.assertGreater(s['uneven'], 30)
        rows = self.run_trace([1000 + 3 * i for i in range(120)], grid=1)
        self.assertAlmostEqual(mt.score(mt.steps(rows[40:], 'cam_x'))['step_std'], 0.0)

    def test_synthetic_jitter_detected_in_trace(self):
        rows = self.run_trace([1000 + (i % 2) for i in range(80)], grid=1)
        s = mt.score(mt.steps(rows, 'scr_x'))
        self.assertGreater(s['alt'], 60)


class Cli(unittest.TestCase):
    def test_csv_input_and_compare(self):
        with tempfile.TemporaryDirectory() as d:
            a, b = os.path.join(d, 'a.csv'), os.path.join(d, 'b.csv')
            for p, xs in ((a, [3 * i for i in range(50)]), (b, [100 + i % 2 for i in range(50)])):
                with open(p, 'w') as f:
                    f.write('frame,x,y\n' + ''.join(f'{i},{x},0\n' for i, x in enumerate(xs)))
            import io, contextlib
            out = io.StringIO()
            with contextlib.redirect_stdout(out):
                mt.main(['--compare', a, b])
            text = out.getvalue()
            line = [l for l in text.splitlines() if l.startswith('scr_x') and 'alt' in l][0].split()
            self.assertEqual((line[2], line[3]), ('0', '48'))


if __name__ == '__main__':
    unittest.main()
