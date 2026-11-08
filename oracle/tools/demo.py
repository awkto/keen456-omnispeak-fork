#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Commander Keen 4/5/6 built-in demo tool: list, dump, verify, build.

    oracle/tools/demo.py list   [-e 4|5|6]
    oracle/tools/demo.py dump   -e N DEMO [-o file.json]   # demo -> per-frame input JSON
    oracle/tools/demo.py verify [-e N]                     # decode -> re-encode must be byte-identical
    oracle/tools/demo.py build  script.json -o demo.bin [--chunk -e N]   # JSON -> demo bytes

Game data comes from $KEEN_DATA (default ~/keen-data-decomp), one subdir per episode
(keen4/, keen5/, keen6/). The Huffman dictionary and chunk offset table live inside the
(LZEXE-packed) EXE and are taken from it via oracle/tools/ripstatic.py's .pat offsets. When the
data is missing, prints SKIP and exits 0. Format: oracle/docs/DEMOS.md.

Derived from the layout of oracle/dos/KEEN4-6/ID_IN.C, ID_CA.C and CK_DEMO.C.
"""
import argparse, glob, json, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# DEMO0 chunk number and demo count per episode (GFXE_CK?.H; Keen 6 untested)
DEMO0 = {4: 4746, 5: 4926, 6: 5555}
NUMDEMOS = {4: 5, 5: 5, 6: 5}
NUMCHUNKS = {4: 4751, 5: 4931, 6: 5560}   # NUMGRCHUNKS; EGAHEAD has NUMCHUNKS+1 3-byte entries
DEMOTICS = 3

# ---------------------------------------------------------------- demo bytes <-> frames

def decode_demo(raw):
    """raw = 4-byte header (word mapon, word size) + RLE pairs (count, ctrl byte).
    Returns (level, [ctrl byte per frame], trailer). `trailer` is whatever follows the
    declared size in the chunk (stale recording-buffer bytes in some shipped demos;
    never read by the game, kept so that re-encoding is byte-identical)."""
    level, size = struct.unpack_from("<HH", raw, 0)
    body = raw[4:4 + size]
    frames = []
    for i in range(0, len(body) - 1, 2):
        frames.extend([body[i + 1]] * body[i])
    return level, frames, raw[4 + size:]


def encode_demo(level, frames, trailer=b""):
    """Canonical encoder, identical to IN_ReadControl's recorder (runs capped at 255)."""
    body = bytearray()
    for b in frames:
        if body and body[-1] == b and body[-2] < 255:
            body[-2] += 1
        else:
            body += bytes((1, b))
    return struct.pack("<HH", level, len(body)) + bytes(body) + bytes(trailer)


def ctrl_to_dict(b):
    return {"x": ((b >> 2) & 3) - 1, "y": (b & 3) - 1, "b0": (b >> 4) & 1, "b1": (b >> 5) & 1}


def dict_to_ctrl(d):
    x, y, b0, b1 = int(d.get("x", 0)), int(d.get("y", 0)), int(d.get("b0", 0)), int(d.get("b1", 0))
    if x not in (-1, 0, 1) or y not in (-1, 0, 1) or b0 not in (0, 1) or b1 not in (0, 1):
        raise ValueError(f"bad frame {d}")
    return (b1 << 5) | (b0 << 4) | ((x + 1) << 2) | (y + 1)


def to_json(episode, num, level, frames, trailer=b""):
    out = {"episode": episode, "demo": num, "level": level, "frame_count": len(frames),
           "tics_per_frame": DEMOTICS, "frames": [ctrl_to_dict(b) for b in frames]}
    lines = ['{"episode": %d, "demo": %s, "level": %d, "frame_count": %d, "tics_per_frame": %d, "trailer": "%s", "frames": ['
             % (episode, json.dumps(num), level, len(frames), DEMOTICS, bytes(trailer).hex())]
    lines += [json.dumps(f, separators=(",", ":")) + ("," if i < len(frames) - 1 else "")
              for i, f in enumerate(out["frames"])]
    lines.append("]}")
    return "\n".join(lines) + "\n"


def from_json(text):
    """Accepts {"level": N, "frames": [{x,y,b0,b1}, ...]} and/or
    {"runs": [{"n": count, "x":..,"y":..,"b0":..,"b1":..}, ...]} (frames then runs)."""
    j = json.loads(text)
    frames = [dict_to_ctrl(f) for f in j.get("frames", [])]
    for r in j.get("runs", []):
        frames += [dict_to_ctrl(r)] * int(r["n"])
    return int(j["level"]), frames, j.get("episode"), bytes.fromhex(j.get("trailer", ""))

# ---------------------------------------------------------------- Huffman (ID_CA.C)

class Huff:
    """EGADICT: 255 nodes of two little-endian words (bit0, bit1); <256 = leaf byte,
    >=256 = node index+256. Head node is 254. Bits are read LSB-first."""

    def __init__(self, dictbytes):
        self.nodes = [struct.unpack_from("<HH", dictbytes, 4 * i) for i in range(255)]
        self.codes = {}
        self._walk(254, 0, 0)

    def _walk(self, n, bits, depth):
        for bit in (0, 1):
            v = self.nodes[n][bit]
            c = bits | (bit << depth)
            if v < 256:
                self.codes.setdefault(v, (c, depth + 1))
            else:
                self._walk(v - 256, c, depth + 1)

    def expand(self, src, expanded):
        out = bytearray()
        pos = 0
        node = 254
        while len(out) < expanded:
            bit = (src[pos >> 3] >> (pos & 7)) & 1
            pos += 1
            v = self.nodes[node][bit]
            if v < 256:
                out.append(v)
                node = 254
            else:
                node = v - 256
        return bytes(out)

    def compress(self, data):
        acc = n = 0
        for b in data:
            c, d = self.codes[b]
            acc |= c << n
            n += d
        return acc.to_bytes((n + 7) // 8, "little")

# ---------------------------------------------------------------- data access

from keendata import data_root, find_file   # shared resolver, docs/DATA.md


class Episode:
    def __init__(self, ep):
        self.ep = ep
        d = os.path.join(data_root(), f"keen{ep}")
        self.dir = d
        self.graph = find_file(d, [f"EGAGRAPH.CK{ep}"])
        exe = find_file(d, [f"KEEN{ep}E.EXE", f"KEEN{ep}.EXE"])
        self.ok = bool(self.graph and exe)
        if not self.ok:
            return
        import ripstatic
        img = ripstatic.load_image(exe)
        versions = ripstatic.parse_pat(str(ep))
        match = [v for v, dm in versions.items() if ripstatic.fits(img, dm)]
        if len(match) != 1:
            self.ok = False
            return
        dd = {n.split(".")[0].lower(): (o, l) for n, o, l in versions[match[0]]}
        o, l = dd["egadict"]
        self.huff = Huff(img[o:o + l])
        o, l = dd["egahead"]
        self.starts = [int.from_bytes(img[o + 3 * i:o + 3 * i + 3], "little") for i in range(l // 3)]
        self.version = match[0]
        self.egagraph = open(self.graph, "rb").read()
        self.maphead = img[dd["maphead"][0]:dd["maphead"][0] + dd["maphead"][1]]
        self.gamemaps = find_file(d, [f"GAMEMAPS.CK{ep}"])

    def chunk_compressed(self, chunk):
        pos = self.starts[chunk]
        nxt = chunk + 1
        while self.starts[nxt] == 0xFFFFFF:
            nxt += 1
        return self.egagraph[pos:self.starts[nxt]]

    def demo_raw(self, num):
        """Return (compressed chunk, expanded demo bytes)."""
        comp = self.chunk_compressed(DEMO0[self.ep] + num)
        expanded = struct.unpack_from("<I", comp, 0)[0]
        return comp, self.huff.expand(comp[4:], expanded)

    def level_name(self, level):
        """Level name from the GAMEMAPS map header (uncompressed), or ''."""
        try:
            off = struct.unpack_from("<i", self.maphead, 2 + 4 * level)[0]
            with open(self.gamemaps, "rb") as f:
                f.seek(off)
                h = f.read(38)
            return h[22:38].split(b"\0")[0].decode("latin-1")
        except Exception:
            return ""


def load_episodes(eps):
    out = []
    for ep in eps:
        e = Episode(ep)
        if not e.ok:
            print(f"SKIP: Keen {ep} data not found under {data_root()}/keen{ep}")
        else:
            out.append(e)
    return out

# ---------------------------------------------------------------- commands

def roundtrip(e, num):
    """Return list of failure strings (empty = ok) and (level, frames)."""
    comp, raw = e.demo_raw(num)
    level, frames, trailer = decode_demo(raw)
    errs = []
    re_raw = encode_demo(level, frames, trailer)
    if re_raw != raw:
        errs.append(f"demo bytes differ (orig {len(raw)} B, re-encoded {len(re_raw)} B)")
    re_comp = struct.pack("<I", len(raw)) + e.huff.compress(raw)
    nbits = sum(e.huff.codes[b][1] for b in raw)
    if nbits % 8 == 0 and comp == re_comp + b"\0":
        pass    # the original packer appends one zero byte when the stream ends on a byte boundary
    elif re_comp != comp:
        errs.append(f"huffman chunk differs (orig {len(comp)} B, re-encoded {len(re_comp)} B)")
    return errs, (level, frames)


def cmd_list(a):
    for e in load_episodes([a.episode] if a.episode else [4, 5]):
        print(f"Keen {e.ep} (EXE version {e.version})")
        print("episode demo level frames name")
        for n in range(NUMDEMOS[e.ep]):
            _, raw = e.demo_raw(n)
            level, frames, _ = decode_demo(raw)
            print(f"{e.ep:7d} {n:4d} {level:5d} {len(frames):6d} {e.level_name(level)}")
    return 0


def cmd_dump(a):
    (e,) = load_episodes([a.episode]) or (None,)
    if not e:
        return 0
    _, raw = e.demo_raw(a.demo)
    level, frames, trailer = decode_demo(raw)
    text = to_json(e.ep, a.demo, level, frames, trailer)
    if a.o:
        open(a.o, "w").write(text)
    else:
        sys.stdout.write(text)
    return 0


def cmd_verify(a):
    eps = load_episodes([a.episode] if a.episode else [4, 5])
    if not eps:
        return 0
    bad = 0
    for e in eps:
        for n in range(NUMDEMOS[e.ep]):
            errs, (level, frames) = roundtrip(e, n)
            print(f"Keen {e.ep} DEMO{n}: level {level}, {len(frames)} frames: " + ("OK" if not errs else "FAIL " + "; ".join(errs)))
            bad += bool(errs)
    return 1 if bad else 0


def cmd_build(a):
    level, frames, ep, trailer = from_json(open(a.script).read())
    raw = encode_demo(level, frames, trailer)
    if a.chunk:
        (e,) = load_episodes([a.episode or ep or 4]) or (None,)
        if not e:
            return 0
        raw = struct.pack("<I", len(raw)) + e.huff.compress(raw)
    open(a.o, "wb").write(raw)
    print(f"wrote {a.o}: level {level}, {len(frames)} frames, {len(raw)} bytes")
    return 0


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("list"); s.add_argument("-e", "--episode", type=int, choices=(4, 5, 6)); s.set_defaults(f=cmd_list)
    s = sub.add_parser("dump"); s.add_argument("-e", "--episode", type=int, required=True, choices=(4, 5, 6))
    s.add_argument("demo", type=int); s.add_argument("-o"); s.set_defaults(f=cmd_dump)
    s = sub.add_parser("verify"); s.add_argument("-e", "--episode", type=int, choices=(4, 5, 6)); s.set_defaults(f=cmd_verify)
    s = sub.add_parser("build"); s.add_argument("script"); s.add_argument("-o", required=True)
    s.add_argument("--chunk", action="store_true", help="emit a Huffman-compressed EGAGRAPH chunk (needs data)")
    s.add_argument("-e", "--episode", type=int, choices=(4, 5, 6)); s.set_defaults(f=cmd_build)
    a = p.parse_args(argv)
    return a.f(a)


if __name__ == "__main__":
    sys.exit(main())
