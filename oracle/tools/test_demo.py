#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic round-trip tests for oracle/tools/demo.py (no game data needed)."""
import os, random, struct, sys, unittest
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import demo


def flat_dict():
    """A complete 8-bit tree: 255 nodes, head = 254 (LSB-first), leaves 0..255."""
    # level k nodes: build bottom-up. Node i (0..127) pairs leaves 2i,2i+1 -- but the
    # bit path must be unique per byte; any full binary tree is fine.
    nodes = []
    ids = list(range(256))               # current items (leaf values)
    while len(ids) > 1:
        nxt = []
        for i in range(0, len(ids), 2):
            nodes.append((ids[i], ids[i + 1]))
            nxt.append(256 + len(nodes) - 1)
        ids = nxt
    assert len(nodes) == 255 and ids == [256 + 254]
    return b"".join(struct.pack("<HH", *n) for n in nodes) + b"\0" * 4


class T(unittest.TestCase):
    def test_rle_roundtrip(self):
        r = random.Random(1)
        frames = []
        for _ in range(200):
            frames += [r.randrange(0, 0x40) & 0x3F | (r.randrange(3) << 4) & 0x30] * r.choice((1, 2, 7, 255, 256, 300))
        raw = demo.encode_demo(5, frames)
        lvl, back, tr = demo.decode_demo(raw)
        self.assertEqual((lvl, back, tr), (5, frames, b""))
        self.assertEqual(demo.encode_demo(lvl, back, tr), raw)
        raw2 = demo.encode_demo(5, frames, b"\x01\x02")
        l2, b2, t2 = demo.decode_demo(raw2)
        self.assertEqual((b2, t2, demo.encode_demo(l2, b2, t2)), (frames, b"\x01\x02", raw2))

    def test_run_cap(self):
        raw = demo.encode_demo(0, [9] * 300)
        self.assertEqual(raw[4:], bytes((255, 9, 45, 9)))

    def test_json_roundtrip(self):
        frames = [0b010110, 0b010110, 0b100101, 0b000101]
        text = demo.to_json(4, 2, 7, frames, b"\xaa")
        self.assertEqual(demo.from_json(text), (7, frames, 4, b"\xaa"))

    def test_runs_script(self):
        lvl, fr, _, _ = demo.from_json('{"level":3,"runs":[{"n":4,"x":1,"b0":1},{"n":2}]}')
        self.assertEqual(fr, [demo.dict_to_ctrl({"x": 1, "b0": 1})] * 4 + [demo.dict_to_ctrl({})] * 2)
        self.assertEqual(demo.dict_to_ctrl({}), 0b0101)   # neutral = x=1,y=1 in the 2-bit encoding

    def test_huffman(self):
        h = demo.Huff(flat_dict())
        data = bytes(random.Random(2).randrange(256) for _ in range(500))
        self.assertEqual(h.expand(h.compress(data), len(data)), data)


if __name__ == "__main__":
    unittest.main()
