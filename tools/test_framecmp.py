#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
#
# test_framecmp.py: unit tests for tools/framecmp.py.

import io
import os
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import framecmp  # noqa: E402

W = framecmp.WIDTH
H = framecmp.HEIGHT
FRAME = framecmp.FRAME_SIZE


def make_frame(value):
    return bytes([value & 0xFF]) * FRAME


def make_frame_with_pixel(value, x, y, pixel):
    f = bytearray([value & 0xFF]) * FRAME
    f[y * W + x] = pixel
    return bytes(f)


class FrameCmpTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp(prefix="framecmp-test-")
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def path(self, name):
        return os.path.join(self.tmp, name)

    def write(self, name, data):
        p = self.path(name)
        with open(p, "wb") as f:
            f.write(data)
        return p

    def run_tool(self, argv):
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = framecmp.main(argv)
        return rc, buf.getvalue()

    def test_identical(self):
        a = self.write("a.bin", b"".join(make_frame(i) for i in range(3)))
        b = self.write("b.bin", b"".join(make_frame(i) for i in range(3)))
        rc, out = self.run_tool([a, b])
        self.assertEqual(rc, 0)
        self.assertIn("chosen shift: +0", out)
        self.assertIn("frames compared: 3", out)
        self.assertIn("differing: 0", out)

    def test_one_pixel_difference(self):
        frames = [make_frame(i) for i in range(3)]
        bad = make_frame_with_pixel(1, 10, 20, 0x0F)
        a = self.write("a.bin", b"".join(frames))
        b = self.write("b.bin", b"".join([frames[0], bad, frames[2]]))
        rc, out = self.run_tool([a, b])
        self.assertEqual(rc, 1)
        self.assertIn("chosen shift: +0", out)
        self.assertIn("differing: 1", out)
        self.assertIn("frame 1: 1 differing pixels, bbox (10,20,10,20)", out)

    def test_shift_detected(self):
        frames = [make_frame(i) for i in range(4)]
        a = self.write("a.bin", b"".join(frames))
        b = self.write("b.bin", b"".join([make_frame(99)] + frames))
        rc, out = self.run_tool([a, b])
        self.assertEqual(rc, 0)
        self.assertIn("chosen shift: +1", out)
        self.assertIn("frames compared: 4", out)
        self.assertIn("differing: 0", out)

    def test_size_not_multiple_of_frame(self):
        a = self.write("a.bin", b"\x00" * (FRAME - 1))
        b = self.write("b.bin", b"\x00" * FRAME)
        rc, _out = self.run_tool([a, b])
        self.assertEqual(rc, 2)

    def test_ignore_rows(self):
        a = self.write("a.bin", make_frame(0))
        b = self.write("b.bin", make_frame_with_pixel(0, 5, 7, 1))
        rc, out = self.run_tool([a, b, "--ignore-rows", "0:10"])
        self.assertEqual(rc, 0)
        self.assertIn("differing: 0", out)

    def test_png_output(self):
        frames = [make_frame(i) for i in range(3)]
        bad = make_frame_with_pixel(1, 10, 20, 0x0F)
        a = self.write("a.bin", b"".join(frames))
        b = self.write("b.bin", b"".join([frames[0], bad, frames[2]]))
        png_dir = self.path("png")
        rc, out = self.run_tool([a, b, "--png-dir", png_dir])
        self.assertEqual(rc, 1)
        png = os.path.join(png_dir, "frame_00001.png")
        self.assertTrue(os.path.exists(png))
        with open(png, "rb") as f:
            self.assertEqual(f.read(8), b"\x89PNG\r\n\x1a\n")
        self.assertIn("wrote", out)


if __name__ == "__main__":
    unittest.main()
