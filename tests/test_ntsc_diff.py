"""ntsc_diff's classes, tokens, alignment and data fields on synthetic MIPS words (no ELF, no game data)."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import ntsc_diff as tool  # noqa: E402


def bits(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def f32(x: float) -> tool.Val:
    return tool.Val("f32", bits(x))


def imm(v: int) -> tool.Val:
    return tool.Val("imm", v)


def code(words, mark=None, name="f") -> tool.Code:
    return tool.tokenize(tool.Code(name, "game/x", 0, list(words)), mark or {})


ADDIU_A0_5 = 0x24040005    # addiu $a0, $zero, 5
ADDIU_A0_6 = 0x24040006
LUI_AT_3F80 = 0x3C013F80   # lui $at, 0x3F80 (1.0f)
MTC1_AT_F0 = 0x44810000    # mtc1 $at, $f0
ADD_S = 0x46000000         # add.s $f0, $f0, $f0
JAL = 0x0C000000
BEQ = 0x10400003           # beqz $v0, +3


class Classes(unittest.TestCase):
    def test_float_rates(self):
        self.assertEqual(tool.classify(f32(0.02), f32(1 / 60)), "rate")
        self.assertEqual(tool.classify(f32(-0.0072), f32(-0.005)), "rate")  # per tick squared
        self.assertEqual(tool.classify(f32(50.0), f32(60.0)), "rate")       # a count
        self.assertEqual(tool.classify(f32(1.0), f32(0.55)), "review")
        self.assertEqual(tool.classify(f32(0.5), f32(-0.5)), "review")

    def test_int_rates(self):
        self.assertEqual(tool.classify(imm(0x5DC), imm(0x708)), "rate")
        self.assertEqual(tool.classify(imm(6), imm(5)), "rate")
        self.assertEqual(tool.classify(imm(3), imm(2)), "review")     # too small to tell
        self.assertEqual(tool.classify(imm(197), imm(177)), "review")  # a sound id
        self.assertEqual(tool.classify(tool.Val("off", 0x10), tool.Val("off", 0x20)), "review")
        self.assertEqual(tool.classify(tool.Val("frame", -80), tool.Val("frame", -64)), "review")

    def test_soft_double(self):
        a = tool.Val("word", 0, struct.pack("<d", 0.08))
        b = tool.Val("word", 1, struct.pack("<d", 0.08 / 1.2))
        self.assertEqual(tool.classify(a, b), "rate")


class Tokens(unittest.TestCase):
    def test_lui_mtc1_is_a_float(self):
        c = code([LUI_AT_3F80, MTC1_AT_F0])
        self.assertEqual(c.vals[0], tool.Val("f32", 0x3F800000))

    def test_lui_ori_mtc1(self):
        c = code([0x3C013C88, 0x34218889, MTC1_AT_F0])  # lui/ori $at 0x3C888889, mtc1
        self.assertEqual(c.vals[0], tool.Val("f32", 0x3C888889))
        self.assertIsNone(c.vals[1])

    def test_nops_jal_and_branches(self):
        a = code([JAL | 0x1234, 0, BEQ, ADDIU_A0_5])
        b = code([JAL | 0x5678, BEQ + 7, 0, 0, ADDIU_A0_5])
        self.assertEqual(a.shape, b.shape)
        self.assertEqual(a.offs, [0, 8, 12])

    def test_marked_literal(self):
        c = code([0xC7800010], {0: f32(0.02)})  # lwc1 $f0, x($gp)
        self.assertEqual(c.vals[0], f32(0.02))
        self.assertEqual(c.shape[0], (0x31, 28, 0))


class Compare(unittest.TestCase):
    def test_sites(self):
        d = tool.compare(code([ADDIU_A0_5, ADD_S]), code([ADDIU_A0_6, ADD_S]))
        self.assertEqual(d.unaligned, 0)
        self.assertEqual([(s.off, s.cls) for s in d.sites], [(0, "rate")])

    def test_aligned_around_new_code(self):
        d = tool.compare(code([ADDIU_A0_5, ADD_S, ADD_S]), code([ADDIU_A0_6, 0x46000001, ADD_S, ADD_S]))
        self.assertEqual(d.unaligned, 1)
        self.assertEqual(len(d.sites), 1)

    def test_one_sided_address(self):
        a = code([0x3C020059])           # lui $v0, 0x59 (an address on one side)
        b = code([0x3C020058], {0: None})
        self.assertFalse(tool.compare(a, b).sites)


class Data(unittest.TestCase):
    def fields(self, a: bytes, b: bytes):
        return [(f.off, f.size, f.kind, f.cls) for f in tool.data_fields("x", a, b, 0x700000, 0x710000)]

    def test_float_and_pointer(self):
        a = struct.pack("<fI", 0.02, 0x00600000)
        b = struct.pack("<fI", 1 / 60, 0x00610000)
        self.assertEqual(self.fields(a, b), [(0, 4, "f32", "rate")])

    def test_bytes_side_by_side(self):
        a, b = struct.pack("<I", 0x00000605), struct.pack("<I", 0x00000706)
        self.assertEqual(self.fields(a, b), [(0, 1, "int", "rate"), (1, 1, "int", "rate")])

    def test_halfword_and_id(self):
        a, b = struct.pack("<HBB", 1500, 197, 0), struct.pack("<HBB", 1800, 177, 0)
        self.assertEqual(self.fields(a, b), [(0, 4, "int", "review")])
        a, b = struct.pack("<HH", 1500, 0), struct.pack("<HH", 1800, 0)
        self.assertEqual(self.fields(a, b), [(0, 4, "int", "rate")])
        a, b = struct.pack("<HH", 1500, 7), struct.pack("<HH", 1800, 7)
        self.assertEqual(self.fields(a, b), [(0, 2, "int", "rate")])


class Known(unittest.TestCase):
    def test_rules(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / "known.txt"
            p.write_text("^func:nu3d/  # engine\n+^data:TUBWAIT\\+  # 48 ticks\n", encoding="utf-8")
            k = tool.Known(p)
        self.assertEqual(k.reason("func:nu3d/nuanim/NuAnimDataLoadBuff+0x1c"), "engine")
        self.assertIsNone(k.reason("data:TUBWAIT+0x0"))
        self.assertTrue(k.is_rate("data:TUBWAIT+0x0"))
        self.assertFalse(k.is_rate("func:game/game/UpdateTimer"))


if __name__ == "__main__":
    unittest.main()
