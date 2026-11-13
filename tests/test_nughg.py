"""nughg, nuvif and title_logo; NUWORLD_TEST_DATA (<data root>/disc) adds the retail .ghg round trips."""
import os
import struct
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import nughg  # noqa: E402
import nuvif  # noqa: E402
import title_logo  # noqa: E402

SVG = REPO / "assets" / "branding" / "nuworld-icon.svg"
DISC = os.environ.get("NUWORLD_TEST_DATA")


def corner(i):
    return nuvif.Corner((float(i), 0.0, -float(i)), 0x80010000 if i % 3 < 2 else 0, (i, -i, 0, 4096), (i, 2, 3, 0x80))


class Encoder(unittest.TestCase):
    def test_batches(self):
        cs = [corner(i) for i in range(90)]
        dma = nuvif.chain([cs])
        self.assertEqual(nuvif.decode(dma), [cs])
        lo, = struct.unpack_from("<I", dma)
        self.assertEqual((lo >> 16, len(dma)), (0x1005, 16 + (lo & 0xFFFF) * 16 + 16))
        strips = [w for w in struct.iter_unpack("<I", dma[16:-16]) if w[0] == nuvif.VIF_STRIP]
        self.assertEqual(len(strips), 3)  # 42 + 42 + 6

    def test_not_ours(self):
        self.assertIsNone(nuvif.decode(bytes(32)))


class TitleLogo(unittest.TestCase):
    def setUp(self):
        self.data = title_logo.build(SVG)
        self.ghg = nughg.read(self.data)

    def test_round_trip(self):
        self.assertEqual(nughg.write(self.ghg), self.data)

    def test_rig(self):
        names = self.ghg.strings.split(b"\0")[:-1]
        self.assertEqual(names[0], b"Character")
        self.assertEqual(len(self.ghg.joints), 11)
        self.assertEqual([struct.unpack_from("<b", j, 0x50)[0] for j in self.ghg.joints], [-1] + [0] * 10)
        self.assertEqual(len(self.ghg.layers[0].gobjs[0]), 0)

    def test_shards(self):
        fills = [colour for _, colour, _ in title_logo.shards(SVG)]
        for j, chain in enumerate(self.ghg.layers[0].gobjs[1:]):
            dma = chain[0].geoms[0].dma
            packets = nuvif.decode(dma)
            self.assertEqual(nuvif.chain(packets), dma)
            self.assertEqual(len(packets[0]), 36)  # a slab: 12 triangles
            self.assertEqual(packets[0][0].col, fills[j] + (0x80,))


@unittest.skipUnless(DISC and Path(DISC).is_dir(), "NUWORLD_TEST_DATA is not set")
class Retail(unittest.TestCase):
    def files(self):
        return sorted(p for p in Path(DISC).rglob("*") if p.suffix.upper() == ".GHG")

    def test_round_trip(self):
        files = self.files()
        self.assertTrue(files)
        for path in files:
            data = path.read_bytes()
            self.assertEqual(nughg.write(nughg.read(data)), data, path.name)

    def test_triangle_lists(self):
        seen = 0
        for path in self.files():
            for layer in nughg.read(path.read_bytes()).layers:
                for chain in layer.gobjs or []:
                    for geom in (g for gobj in chain for g in gobj.geoms if g.dma):
                        packets = nuvif.decode(geom.dma)
                        if packets is not None:
                            seen += 1
                            dma = nuvif.chain(packets)
                            self.assertEqual(geom.dma[:len(dma)], dma, path.name)
        self.assertTrue(seen)


if __name__ == "__main__":
    unittest.main()
