#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Motion trace: per-frame screen-space motion of an object -> CSV, SVG plot, unevenness scores (#82).

  motiontrace.py -e 4 k4-d0.trc [--slot N | --class keenobj] [--csv out.csv] [--plot out.svg]
  motiontrace.py --compare A B [-e 4]       # scores side by side (traces or CSVs)
  motiontrace.py positions.csv              # CSV input: header with frame,x,y (screen px)

Input is a KEENDUMP / Omnispeak /DUMPFILE trace (oracle/docs/TRACE_FORMAT.md; reuses oracle/tools/tracecmp.py) or
a simple CSV with columns `frame,x,y` (x, y in screen pixels; extra columns such as a timestamp are
ignored). The CSV form is for display-rate data from the engine's present log.

Camera: the trace format has NO scroll origin (docs/ACTORS.md §7), so it is re-derived by running
the game's own camera code (CK_PLAY.C CenterActor / ScrollScreen / WorldScrollScreen, ID_RF.C
RF_Scroll) on Keen's recorded state, one step per frame with tics = 3 (demo). Limitations:
  * the initial position is CenterActor of Keen's position *before* frame 0 (x - xmove);
  * map-edge clamps need the map size: pass --map-size W H (tiles) or the camera is only clamped
    at the min edge (a Keen walking into the right/bottom edge is then mis-modelled);
  * level scroll blocks (RF_SetScrollBlock) are not modelled;
  * a frame where Keen moves more than xmove/ymove explain (teleporter) re-centres the camera, as
    the game does, and steps into that frame are left out of the scores;
  * the ground-slam shake (K6) is not modelled.
Positions are global units, 16 per pixel (ID_RF.H PIXGLOBAL), 256 per tile.

Display grid: vanilla EGA pans and draws sprites on a 2-pixel grid (xpanmask, 4 pre-shifts; #89
findings). `--grid N` (default 2) floors camera and object pixel positions to N before taking
screen = object - camera. `--grid 1` gives the raw 1-pixel positions.

Scores per series (cam_x, cam_y, scr_x, scr_y; steps = per-frame change in px):
  step_std  population stddev of the step (same metric as oracle/harness/interp/stats.py)
  mean_abs  mean |step|, max_step  largest |step|, frozen% share of zero steps
  alt       number of reversing step pairs with both |step| <= 1 (jitter back and forth, +1/-1)
  uneven    number of pairs of consecutive same-direction steps whose size differs (e.g. 2,4,2,4)
"""
import argparse, csv, os, statistics, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'oracle', 'tools'))   # tracecmp, tracediff
import tracecmp as tc
import tracediff as td

TILEG, PIXG = 256, 16
MAPBORDER, SCREENW, SCREENH = 2, 20, 13    # ID_RF.H / ID_RF.C RF_NewMap
JUMP = 6 * PIXG    # Keen moved this much more than xmove/ymove says: a teleport, re-centre
SERIES = ('cam_x', 'cam_y', 'scr_x', 'scr_y')


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def u16(v):
    return v & 0xFFFF


# ---------------------------------------------------------------- camera model

class Camera:
    """Keen's camera: CenterActor + ScrollScreen / WorldScrollScreen (CK_PLAY.C), RF_Scroll bounds."""

    def __init__(self, ep, mapsize=None, tics=3):
        self.ep, self.tics = ep, tics
        self.xmin = self.ymin = MAPBORDER * TILEG
        if mapsize:
            self.xmax = max(self.xmin, (mapsize[0] - MAPBORDER - SCREENW) * TILEG)
            self.ymax = max(self.ymin, (mapsize[1] - MAPBORDER - SCREENH) * TILEG)
        else:
            self.xmax = self.ymax = 0xFFFF
        names = {v[0]: k for k, v in td.load_statemap(ep).items()}
        self.st = {n: names.get(n, -1) for n in
                   ('s_keenlookup2', 's_keenlookdown3', 's_keenholdon',
                    's_keenclimbup', 's_keenclimbup2', 's_keenclimbup3', 's_keenclimbup4')}
        self.ox = self.oy = 0
        self.centerlevel = 140

    def bound(self, x, y):
        self.ox = min(max(u16(x), self.xmin), self.xmax)
        self.oy = min(max(u16(y), self.ymin), self.ymax)

    def center(self, o, mapon):
        self.centerlevel = 140
        ox = 0 if o['x'] < 152 * PIXG else o['x'] - 152 * PIXG
        if mapon == 0:
            oy = 0 if o['y'] < 80 * PIXG else o['y'] - 80 * PIXG
        else:
            oy = 0 if o['bottom'] < 140 * PIXG else o['bottom'] - 140 * PIXG
        self.bound(ox, oy)

    def step(self, o, mapon):
        world = mapon == 0 or (self.ep == 4 and mapon == 17)
        (self.world_scroll if world else self.scroll)(o)

    def rf_scroll(self, xs, ys):
        xs = max(-0xFF, min(0xFF, xs))
        ys = max(-0xFF, min(0xFF, ys))
        self.bound(self.ox + xs, self.oy + ys)

    def world_scroll(self, o):
        ox, oy = self.ox, self.oy
        xs = ys = 0
        if o['left'] < ox + 9 * TILEG:
            xs = s16(o['left'] - (ox + 9 * TILEG))
        elif o['right'] > ox + 12 * TILEG:
            xs = s16(o['right'] + 16 - (ox + 12 * TILEG))
        if o['top'] < oy + 5 * TILEG:
            ys = s16(o['top'] - (oy + 5 * TILEG))
        elif o['bottom'] > oy + 7 * TILEG:
            ys = s16(o['bottom'] - (oy + 7 * TILEG))
        if xs or ys:
            self.rf_scroll(xs, ys)

    def scroll(self, o):
        ox, oy, tics, st = self.ox, self.oy, self.tics, self.st
        xs = ys = 0
        if o['x'] < ox + 9 * TILEG:
            xs = s16(o['x'] - (ox + 9 * TILEG))
        elif o['x'] > ox + 12 * TILEG:
            xs = s16(o['x'] - (ox + 12 * TILEG))
        if o['state'] == st['s_keenlookup2']:
            pix = 167 - self.centerlevel if self.centerlevel + tics > 167 else tics
            self.centerlevel += pix
            ys = s16(-pix << 4)
        elif o['state'] == st['s_keenlookdown3']:
            pix = self.centerlevel - 33 if self.centerlevel - tics < 33 else tics
            self.centerlevel -= pix
            ys = s16(pix << 4)
        if o['hitnorth'] or not o['needtoclip'] or o['state'] == st['s_keenholdon']:
            if o['state'] not in (st['s_keenclimbup'], st['s_keenclimbup2'],
                                  st['s_keenclimbup3'], st['s_keenclimbup4']):
                ys = s16(ys + s16(o['ymove']))
                bottom = u16(oy + ys + (self.centerlevel << 4))
                if o['bottom'] != bottom:
                    pix = s16(bottom - o['bottom']) if o['bottom'] < bottom else s16(o['bottom'] - bottom)
                    speed = s16(s16(pix << 4) >> 7)
                    if speed > 0x30:
                        speed = 0x30
                    speed = s16(speed * tics)
                    if speed < 0x10:
                        speed = pix if pix < 0x10 else 0x10
                    ys = s16(ys - speed) if o['bottom'] < bottom else s16(ys + speed)
        else:
            self.centerlevel = 140
        pix = s16((o['bottom'] - 32 * PIXG) - (oy + ys))
        if pix < 0:
            ys = s16(ys + pix)
        pix = s16((o['bottom'] + 32 * PIXG) - (oy + ys + 200 * PIXG))
        if pix > 0:
            ys = s16(ys + pix)
        if xs or ys:
            self.rf_scroll(xs, ys)


# ---------------------------------------------------------------- series extraction

def class_slot(ep, name):
    names = td.CLASSES[ep]
    if name not in names:
        sys.exit(f'unknown class {name!r} for Keen {ep}; known: {" ".join(names)}')
    return names.index(name)


def trace_rows(path, ep, slot=None, cls=None, mapsize=None, grid=2, tics=3):
    """One dict per frame: frame, alive, cam_x, cam_y, obj_x, obj_y (px), scr_x, scr_y (px, on grid)."""
    cam, rows, first, prev = Camera(ep, mapsize, tics), [], True, None
    clsno = class_slot(ep, cls) if cls else None
    for i, (_tc, gs, objs) in enumerate(tc.records(path, ep)):
        mapon = gs[tc.GS_FIELDS[ep].index('mapon')]
        keen = dict(zip(tc.OBJ_FIELDS, objs[0]))
        if first:   # CenterActor ran before frame 0's think pass
            pre = dict(keen, x=u16(keen['x'] - s16(keen['xmove'])), y=u16(keen['y'] - s16(keen['ymove'])),
                       bottom=u16(keen['bottom'] - s16(keen['ymove'])))
            cam.center(pre, mapon)
            first = False
        jump = False
        if prev is not None and (abs(s16(keen['x'] - prev['x'] - s16(keen['xmove']))) > JUMP
                                 or abs(s16(keen['y'] - prev['y'] - s16(keen['ymove']))) > JUMP):
            cam.center(keen, mapon)      # teleport / level change: the game re-centres on Keen
            jump = True
        else:
            cam.step(keen, mapon)
        prev = keen
        lv = tc.live(ep, objs)
        if clsno is not None:
            sel = next((s for s in lv if objs[s][0] == clsno), None)
        else:
            sel = slot if slot is not None and slot in lv else (0 if slot is None else None)
        row = dict(frame=i, alive=sel is not None, jump=jump, cam_x=cam.ox // PIXG // grid * grid,
                   cam_y=cam.oy // PIXG // grid * grid)
        if sel is not None:
            ox, oy = objs[sel][tc.OBJ_FIELDS.index('x')], objs[sel][tc.OBJ_FIELDS.index('y')]
            row.update(slot=sel, obj_x=ox // PIXG, obj_y=oy // PIXG,
                       scr_x=ox // PIXG // grid * grid - row['cam_x'],
                       scr_y=oy // PIXG // grid * grid - row['cam_y'])
        rows.append(row)
    return rows


def csv_rows(path):
    with open(path, newline='') as f:
        rd = csv.DictReader(f)
        cols = rd.fieldnames or []
        kx = 'scr_x' if 'scr_x' in cols else 'x'
        ky = 'scr_y' if 'scr_y' in cols else 'y'
        if kx not in cols or ky not in cols:
            sys.exit(f'{path}: CSV needs columns frame,x,y (found {cols})')
        rows = []
        for n, r in enumerate(rd):
            fr = r.get('frame', n)
            row = dict(frame=fr, alive=r[kx] != '', scr_x=float(r[kx] or 0), scr_y=float(r[ky] or 0))
            if 'cam_x' in r and r['cam_x'] != '':
                row['cam_x'], row['cam_y'] = float(r['cam_x']), float(r['cam_y'])
            rows.append(row)
        return rows


def load(path, a):
    if path.lower().endswith('.csv'):
        return csv_rows(path)
    if a.episode is None:
        sys.exit('traces need -e/--episode 4|5')
    return trace_rows(path, a.episode, a.slot, a.cls, a.map_size, a.grid, a.tics)


# ---------------------------------------------------------------- scores

def steps(rows, key, lo=None, hi=None):
    """Per-frame steps of `key` between consecutive frames where the object is present."""
    out = []
    for p, q in zip(rows, rows[1:]):
        if not (p['alive'] and q['alive']) or q.get('jump') or key not in p or key not in q:
            continue
        f = float(p['frame'])
        if (lo is not None and f < lo) or (hi is not None and f >= hi):
            continue
        out.append(q[key] - p[key])
    return out


def score(st):
    if not st:
        return None
    nz = [s for s in st if s]
    alt = sum(1 for a, b in zip(st, st[1:]) if a * b < 0 and abs(a) <= 1 and abs(b) <= 1)
    unev = sum(1 for a, b in zip(st, st[1:]) if a * b > 0 and a != b)
    return dict(n=len(st), step_std=statistics.pstdev(st), mean_abs=sum(map(abs, st)) / len(st),
                max_step=max(map(abs, st)), frozen_pct=100.0 * (len(st) - len(nz)) / len(st),
                alt=alt, uneven=unev)


def all_scores(rows, lo=None, hi=None):
    return {k: score(steps(rows, k, lo, hi)) for k in SERIES if any(k in r for r in rows)}


METRICS = ('n', 'step_std', 'mean_abs', 'max_step', 'frozen_pct', 'alt', 'uneven')


def fmt(m, v):
    return f'{v:d}' if m in ('n', 'alt', 'uneven') else f'{v:.3f}' if m != 'frozen_pct' else f'{v:.1f}'


def print_scores(sc, title):
    print(title)
    print(f'  {"series":7}' + ''.join(f'{m:>10}' for m in METRICS))
    for k, s in sc.items():
        print(f'  {k:7}' + ''.join(f'{fmt(m, s[m]):>10}' if s else '' for m in METRICS))


def print_compare(names, scs):
    keys = [k for k in SERIES if any(k in s for s in scs)]
    print(f'{"series":7} {"metric":10}' + ''.join(f'{os.path.basename(n)[:22]:>24}' for n in names))
    for k in keys:
        for m in METRICS:
            cells = [fmt(m, s[k][m]) if s.get(k) else '-' for s in scs]
            print(f'{k:7} {m:10}' + ''.join(f'{c:>24}' for c in cells))


# ---------------------------------------------------------------- output

def write_csv(rows, f):
    cols = ['frame', 'alive', 'slot', 'cam_x', 'cam_y', 'obj_x', 'obj_y', 'scr_x', 'scr_y',
            'dcam_x', 'dcam_y', 'dscr_x', 'dscr_y']
    w = csv.writer(f)
    w.writerow(cols)
    prev = None
    for r in rows:
        d = {}
        if prev and prev['alive'] and r['alive']:
            for k in SERIES:
                if k in r and k in prev:
                    d['d' + k] = r[k] - prev[k]
        elif prev:
            for k in ('cam_x', 'cam_y'):
                if k in r and k in prev:
                    d['d' + k] = r[k] - prev[k]
        w.writerow([int(r['alive']) if c == 'alive' else r.get(c, d.get(c, '')) for c in cols])
        prev = r


def svg_plot(rows, path, title):
    """Stacked panels (value, step) for each series present; hand-written SVG, no dependencies."""
    keys = [k for k in SERIES if any(k in r for r in rows)]
    W, H, L, R, P = 900, 110, 60, 20, 14
    panels = [(k, False) for k in keys] + [(k, True) for k in keys]
    panels.sort(key=lambda p: (keys.index(p[0]), p[1]))
    out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{len(panels) * (H + P) + 30}" '
           f'font-family="monospace" font-size="11"><text x="{L}" y="16">{title}</text>']
    n = max(1, len(rows) - 1)
    for pi, (k, isstep) in enumerate(panels):
        pts = []
        for i, r in enumerate(rows):
            if k not in r or not r['alive']:
                continue
            if isstep:
                if i == 0 or k not in rows[i - 1] or not rows[i - 1]['alive']:
                    continue
                v = r[k] - rows[i - 1][k]
            else:
                v = r[k]
            pts.append((i, v))
        top = 30 + pi * (H + P)
        out.append(f'<rect x="{L}" y="{top}" width="{W - L - R}" height="{H}" fill="none" stroke="#999"/>')
        out.append(f'<text x="4" y="{top + 12}">{"d" if isstep else ""}{k}</text>')
        if not pts:
            continue
        lo, hi = min(v for _, v in pts), max(v for _, v in pts)
        if hi == lo:
            hi, lo = hi + 1, lo - 1
        out.append(f'<text x="4" y="{top + H}">{lo:g}</text><text x="4" y="{top + 24}">{hi:g}</text>')
        xy = ' '.join(f'{L + (W - L - R) * i / n:.1f},{top + H - 4 - (H - 8) * (v - lo) / (hi - lo):.1f}'
                      for i, v in pts)
        if isstep:   # steps as dots: a line through 2,4,2,4 hides the pattern less than it clutters
            out += [f'<circle cx="{p.split(",")[0]}" cy="{p.split(",")[1]}" r="1.6" fill="#c33"/>'
                    for p in xy.split()]
        else:
            out.append(f'<polyline points="{xy}" fill="none" stroke="#36c" stroke-width="1"/>')
    out.append('</svg>')
    with open(path, 'w') as f:
        f.write('\n'.join(out))


def plot(rows, path, title):
    if path.lower().endswith('.svg'):
        return svg_plot(rows, path, title)
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:
        sys.exit('matplotlib not installed: use a .svg output name for the built-in plot')
    keys = [k for k in SERIES if any(k in r for r in rows)]
    fig, ax = plt.subplots(len(keys), 2, figsize=(12, 2.2 * len(keys)), squeeze=False)
    for j, k in enumerate(keys):
        fr = [r['frame'] for r in rows if r['alive'] and k in r]
        v = [r[k] for r in rows if r['alive'] and k in r]
        ax[j][0].plot(fr, v, lw=1); ax[j][0].set_ylabel(k)
        ax[j][1].plot(fr[1:], [b - a for a, b in zip(v, v[1:])], '.', ms=3); ax[j][1].set_ylabel('d' + k)
    fig.suptitle(title); fig.tight_layout(); fig.savefig(path)


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('input', nargs='?', help='trace (.trc) or CSV (frame,x,y)')
    p.add_argument('-e', '--episode', type=int, choices=(4, 5))
    g = p.add_mutually_exclusive_group()
    g.add_argument('--slot', type=int, help='object slot (default 0 = Keen)')
    g.add_argument('--class', dest='cls', help='first live object of this class (e.g. slugobj)')
    p.add_argument('--grid', type=int, default=2, help='display pixel grid, default 2 (EGA); 1 = raw')
    p.add_argument('--tics', type=int, default=3, help='tics per frame for the camera model (demo: 3)')
    p.add_argument('--map-size', type=int, nargs=2, metavar=('W', 'H'), help='map size in tiles, for edge clamps')
    p.add_argument('--range', nargs=2, type=float, metavar=('FROM', 'TO'), help='score frames FROM <= f < TO')
    p.add_argument('--csv', help='write the per-frame CSV here')
    p.add_argument('--plot', help='write a plot (.svg built-in, .png needs matplotlib)')
    p.add_argument('--compare', nargs=2, metavar=('A', 'B'), help='print scores of two inputs side by side')
    a = p.parse_args(argv)
    lo, hi = a.range or (None, None)
    if a.compare:
        scs = [all_scores(load(x, a), lo, hi) for x in a.compare]
        print_compare(a.compare, scs)
        return 0
    if not a.input:
        p.error('give an input or --compare A B')
    rows = load(a.input, a)
    if a.csv:
        with open(a.csv, 'w', newline='') as f:
            write_csv(rows, f)
    if a.plot:
        plot(rows, a.plot, os.path.basename(a.input))
    print_scores(all_scores(rows, lo, hi), f'{a.input}: {len(rows)} frames')
    return 0


if __name__ == '__main__':
    sys.exit(main())
