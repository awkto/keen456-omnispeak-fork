#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Animated-tile speeds in the shipped Keen 4 / Keen 5 v1.4 data (tracker #95; docs/ID_RF.md section 6).

DOS RFL_AnimateTiles (oracle/dos/KEEN4-6/ID_RF.C:943-957) does `count -= tics; while (count < 1) {advance; count += speed of
new tile}`; Omnispeak (engine src/id_rf.c RFL_AnimateTiles) uses `if`. The two differ only when a frame's speed is
smaller than `tics` (2 normal, 3 demo, up to 5 under load). This prints, per episode, the speed histogram of every
animated frame (background plane: tinf[SPEED+t]/[ANIM+t]; foreground plane: MSPEED/MANIM), the verdict per tics, and
with --levels the maps that contain a tile of an offending chain.

    tools/tileanim.py [--levels] [--episodes 4 5]

tileinfo source: the table is the tail of MAPHEAD (header = RLEWtag + 100 longs = 402 bytes, ID_CA.H:61 SPEED=402),
which the DOS EXE carries statically; it is ripped from KEEN?.EXE with the same offsets as oracle/tools/ripstatic.py and
cross-checked against data/keen?/TILEINFO.CK? (what Omnispeak loads). Needs game data ($KEEN_DATA, docs/DATA.md).
"""
import os, struct, sys
from collections import Counter, defaultdict
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "oracle", "tools"))   # keendata, ripstatic
import keendata, ripstatic

REPO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
TICS = (2, 3, 5)


def sbyte(b):
    return b - 256 if b > 127 else b


def load_tileinfo(ep):
    """(tileinfo bytes from the EXE, source note, N=numTiles16, M=numTiles16m)."""
    exe = keendata.exe(ep)
    img = ripstatic.load_image(exe)
    vers = ripstatic.parse_pat(ep)
    ok = [v for v, d in vers.items() if ripstatic.fits(img, d)]
    if "1.4" not in ok:
        raise SystemExit(f"tileanim: Keen {ep}: EXE is not a supported v1.4 build (candidates {ok})")
    off, ln = [(o, l) for n, o, l in vers["1.4"] if n.lower().startswith("maphead")][0]
    maphead = img[off:off + ln]
    ti = maphead[402:]
    gi = open(os.path.join(REPO, "data", f"keen{ep}", f"GFXINFOE.CK{ep}"), "rb").read()
    N, M = struct.unpack_from("<2H", gi, 4)
    assert len(ti) == 2 * N + 7 * M, (len(ti), N, M)
    note = f"MAPHEAD.CK{ep} static in the EXE at ${off:X} (+402), {len(ti)} bytes"
    other = os.path.join(REPO, "data", f"keen{ep}", f"TILEINFO.CK{ep}")
    if os.path.exists(other):
        note += "; identical to repo TILEINFO" if open(other, "rb").read() == ti else "; DIFFERS from repo TILEINFO"
    return maphead, ti, note, N, M


def chains(ti, N, M, fg):
    """Every animated start tile: (tile, [speeds of the frames in the chain, in visit order], closed?)
    DOS start timer = speed of the start tile; each advance adds the speed of the NEW tile."""
    if fg:
        speed = lambda t: ti[2 * N + 6 * M + t]
        anim = lambda t: sbyte(ti[2 * N + 4 * M + t])
        n = M
    else:
        speed = lambda t: ti[t]
        anim = lambda t: sbyte(ti[N + t])
        n = N
    out = {}
    for t in range(n):
        if anim(t) == 0 or speed(t) == 0:
            continue
        seq, cur, closed = [], t, False
        while len(seq) <= 64:
            seq.append((cur, speed(cur)))
            cur += anim(cur)
            if cur == t:
                closed = True
                break
            if not 0 <= cur < n or anim(cur) == 0:
                break
        out[t] = (seq, closed)
    return out


def carmack(src, outwords):
    out, i = bytearray(), 0
    while i < len(src):
        lo, hi = src[i], src[i + 1]
        i += 2
        if hi in (0xA7, 0xA8):
            if lo == 0:
                out += bytes([src[i], hi]); i += 1
            elif hi == 0xA7:
                off = src[i]; i += 1
                for _ in range(lo): out += out[len(out) - 2 * off: len(out) - 2 * off + 2]
            else:
                off = src[i] | src[i + 1] << 8; i += 2
                for k in range(lo): out += out[2 * (off + k): 2 * (off + k) + 2]
        else:
            out += bytes([lo, hi])
    return bytes(out)


def rlew(src, tag, words):
    out, i = [], 0
    while len(out) < words and i + 1 < len(src) + 1:
        w = src[i] | src[i + 1] << 8; i += 2
        if w == tag:
            c = src[i] | src[i + 1] << 8; v = src[i + 2] | src[i + 3] << 8; i += 4
            out += [v] * c
        else:
            out.append(w)
    return out[:words]


def level_tiles(ep, maphead):
    """yield (mapnum, name, set of bg tiles, set of fg tiles) from GAMEMAPS."""
    d = keendata.episode_dir(ep)
    gm = open(keendata.find_file(d, [f"GAMEMAPS.CK{ep}"]), "rb").read()
    tag = struct.unpack_from("<H", maphead, 0)[0]
    for n in range(100):
        pos = struct.unpack_from("<l", maphead, 2 + 4 * n)[0]
        if pos <= 0:
            continue
        ps = struct.unpack_from("<3l", gm, pos)
        pl = struct.unpack_from("<3H", gm, pos + 12)
        w, h = struct.unpack_from("<2H", gm, pos + 18)
        name = gm[pos + 22:pos + 38].split(b"\0")[0].decode("latin-1")
        planes = []
        for p in (0, 1):
            raw = gm[ps[p]:ps[p] + pl[p]]
            exp = struct.unpack_from("<H", raw, 0)[0]
            c = carmack(raw[2:], exp // 2)
            expl = struct.unpack_from("<H", c, 0)[0]
            planes.append(set(rlew(c[2:], tag, w * h)))
        yield n, name, planes[0], planes[1]


def main(argv):
    levels = "--levels" in argv
    eps = [int(a) for a in argv[argv.index("--episodes") + 1:] if a.isdigit()] if "--episodes" in argv else [4, 5]
    for ep in eps:
        keendata.require(ep)
        maphead, ti, note, N, M = load_tileinfo(ep)
        print(f"\n== Keen {ep} v1.4: tileinfo from {note}; {N} bg tiles, {M} fg tiles ==")
        allc = {}
        for fg, label in ((0, "background"), (1, "foreground")):
            ch = chains(ti, N, M, fg)
            allc[fg] = ch
            frames = Counter(s for seq, _ in ch.values() for _, s in seq)
            print(f"\n{label}: {len(ch)} animated start tiles; speed histogram over chain frames "
                  f"(speed: frames; chains whose minimum frame speed is that value):")
            minc = Counter(min(s for _, s in seq) for seq, _ in ch.values())
            for s in sorted(frames)[:8]:
                print(f"  speed {s:3d}: {frames[s]:4d} frames, {minc.get(s, 0):3d} chains")
            bad0 = [t for t, (seq, closed) in ch.items() if not closed]
            if bad0:
                print(f"  note: {len(bad0)} chains do not return to their start (end on a non-animated tile)")
            zero = [t for t, (seq, _) in ch.items() if any(s == 0 for _, s in seq)]
            if zero:
                print(f"  note: chains containing a speed-0 frame (DOS would loop forever, so they must be unused): "
                      f"{len(zero)}")
        print("\nverdict (a chain is affected if any frame has speed < tics, so DOS `while` can step twice "
              "where Omnispeak `if` steps once):")
        for t in TICS:
            parts = []
            for fg, label in ((0, "bg"), (1, "fg")):
                n = sum(1 for seq, _ in allc[fg].values() if min(s for _, s in seq) < t)
                parts.append(f"{label} chains affected: {n}")
            print(f"  tics={t}: " + ", ".join(parts))
        if levels:
            sel = {}
            for fg in (0, 1):
                for t, (seq, _) in allc[fg].items():
                    if min(s for _, s in seq) < 5:
                        sel[(fg, t)] = min(s for _, s in seq)
            lv = list(level_tiles(ep, maphead))
            usedbg = set().union(*(b for _, _, b, _ in lv)); usedfg = set().union(*(f for _, _, _, f in lv))
            for t in TICS:
                u = [sum(1 for k, (seq, _) in allc[fg].items() if min(x for _, x in seq) < t and k in (usedfg if fg else usedbg))
                     for fg in (0, 1)]
                print(f"  tics={t}: chains whose START tile occurs in some level: bg {u[0]}, fg {u[1]}")
            print(f"\nlevels using chains with a frame speed < 5 (plane, start tile -> min speed):")
            for n, name, bg, fgs in lv:
                hit = [(("fg" if fg else "bg"), t, sp) for (fg, t), sp in sorted(sel.items())
                       if t in (fgs if fg else bg)]
                if hit:
                    print(f"  map {n:2d} {name:18s} " + ", ".join(f"{p}:{t}->{sp}" for p, t, sp in hit))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
