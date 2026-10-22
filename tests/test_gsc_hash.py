"""gsc_hash: the DMA chain, the block walk, the manifest and the hash recipe (no game data, no tooling)."""
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gsc_hash  # noqa: E402


def strip_tris(wpos, adc=None, nloop=None, coord_limit=1e4):
    """The tooling's strip_tris rule (nus_scene.py), for the recipe test."""
    return [(i - 2, i, i - 1) if i & 1 else (i - 2, i - 1, i) for i in range(2, len(wpos)) if not adc[i]]


def tag(tag_id, qwc, addr=0):
    return struct.pack("<IIII", tag_id << 28 | qwc, addr, 0, 0)


class GscHashTest(unittest.TestCase):
    def test_linear(self):
        cnt = tag(1, 1) + b"A" * 16
        nxt = tag(2, 1, 0x60) + b"B" * 16 + b"\xcd" * 0x20
        ret = tag(6, 1) + b"C" * 16 + b"stale" * 9
        out = gsc_hash.linear(cnt + nxt + ret)
        self.assertEqual(out, cnt + b"\0" * 8 + b"B" * 16 + b"\0" * 8 + b"C" * 16)
        with self.assertRaisesRegex(ValueError, "sends more than"):
            gsc_hash.linear(tag(2, 0, 0))
        with self.assertRaisesRegex(ValueError, "DMA tag id 3"):
            gsc_hash.linear(tag(3, 0))

    def test_blocks(self):
        def block(tag_id, data):
            return tag_id + struct.pack("<I", 8 + len(data)) + data
        body = block(b"NTBL", b"\0" * 8) + block(b"XXXX", b"") + block(b"OBJ0", b"\0" * 4)
        nu = b"NU20" + struct.pack("<iII", -(16 + len(body)), 6, 0) + body
        found, unknown, truncated = gsc_hash.blocks(nu)
        self.assertEqual([t for t, _o, _s in found], [b"NTBL", b"XXXX", b"OBJ0"])
        self.assertEqual((found[0][1], found[0][2], unknown, truncated), (24, 8, 1, False))
        found, unknown, truncated = gsc_hash.blocks(nu[:-2])
        self.assertEqual((len(found), unknown, truncated), (2, 1, True))
        with self.assertRaisesRegex(ValueError, "scene version 5"):
            gsc_hash.blocks(nu[:8] + struct.pack("<I", 5) + nu[12:])

    def test_manifest(self):
        paths = gsc_hash.scene_paths()
        self.assertEqual(len(paths), 66)
        self.assertIn("LEVELS/B/HUB/HUB.GSC", paths)

    def test_recipe(self):
        # The batch tests/c/test_scene.c decodes and pins to the same line.
        w_adc, w = struct.unpack("<f", struct.pack("<I", 0x3F808001))[0], 1.0
        geom = {"pos": [(0, 0, 0, w_adc), (1, 0, 0, w_adc), (0, 1, 0, w), (1, 1, -0.0, w)],
                "uv": [(i, -i / 2) for i in range(4)], "col": [(i, 0x40, 0x40, 0x80) for i in range(4)],
                "adc": [True, True, False, False], "nloop": 4}
        line, batches, verts, tris = gsc_hash.obj_line(0, [geom], strip_tris)
        self.assertEqual(line, "OBJ 0 batches=1 verts=4 tris=2 "
                               "vtx=5ff61ee696e31a115059cd16ee188d6eb4aa25b17e7a40ac6583f6893c700941 "
                               "idx=a55571c9db30026e44ac0bdd7674d9c597d8254732feb18418f3aaf8a5b4f418")
        self.assertEqual((batches, verts, tris), (1, 4, 2))
        self.assertEqual(gsc_hash.obj_line(1, [], strip_tris)[0],
                         f"OBJ 1 batches=0 verts=0 tris=0 vtx={gsc_hash.EMPTY} idx={gsc_hash.EMPTY}")

    def test_tex_recipe(self):
        # The PSMT4 texture tests/c/test_scene.c decodes and pins to the same line.
        rgba = b"".join(bytes((c * 16, 255 - c * 16, c * 3, c * 16))
                        for c in ((x + 2 * y) & 15 for y in range(8) for x in range(8)))
        self.assertEqual(gsc_hash.tex_line(0, 8, 8, 4, 1, 1, rgba),
                         "TEX 0 8x8 type=4 version=1 levels=1 "
                         "rgba=8d5cce5284f06b924fa01c1b8a0b335b49fe6182c3533c2dfb9f08d921632092")
        cur = struct.pack("<3HxxiBBBB", 64, 1, 32, 0, 7, 0x13, 0, 0x81)
        old = struct.pack("<3iBBBB", 16, 8, 0, 5, 0x13, 2, 1)
        self.assertEqual(gsc_hash.tex_header(cur, 0), (64, 32, 7, 1, 1))
        self.assertEqual(gsc_hash.tex_header(old, 0), (16, 8, 5, 0, 2))
        self.assertTrue(gsc_hash.compared("TEX 3 64x32 type=7 version=1 levels=1 rgba=00"))
        self.assertFalse(gsc_hash.compared("TEX 3 64x32 type=6 version=1 levels=1 rgba=00"))
        self.assertFalse(gsc_hash.compared("TEX 3 16x8 type=5 version=0 levels=2 rgba=00"))

    def test_no_tooling(self):
        env = {k: v for k, v in os.environ.items() if k != "CRASHWOC_TOOLING"}
        with tempfile.TemporaryDirectory() as root:
            run = subprocess.run([sys.executable, str(REPO / "tools" / "gsc_hash.py"), "--data-dir", root],
                                 capture_output=True, text=True, env=env)
        self.assertEqual(run.returncode, 77, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
