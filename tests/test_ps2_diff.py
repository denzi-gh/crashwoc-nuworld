"""ps2_diff's dump reader, placement, comparison and classes, and pcsx2_dump's pokes, on synthetic files (no PCSX2,
no game data)."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import pcsx2_dump  # noqa: E402
import ps2_diff as tool  # noqa: E402

TYPEDB = {"records": {
    "struct vec": {"ps2": {"size": 12}, "fields": [
        {"name": "x", "kind": "float", "ps2": {"offset": 0, "size": 4}},
        {"name": "y", "kind": "float", "ps2": {"offset": 4, "size": 4}},
        {"name": "z", "kind": "float", "ps2": {"offset": 8, "size": 4}}]},
    "struct obj": {"ps2": {"size": 28}, "fields": [
        {"name": "next", "kind": "pointer", "ps2": {"offset": 0, "size": 4}},
        {"name": "pos", "kind": "record", "ref": "struct vec", "ps2": {"offset": 4, "size": 12}},
        {"name": "ids", "kind": "array", "count": 4, "element": {"kind": "int", "signed": True, "size": 2},
         "ps2": {"offset": 16, "size": 8}},
        {"name": "count", "kind": "int", "signed": True, "ps2": {"offset": 24, "size": 4}}]},
}}


def text(s: str) -> bytes:
    return struct.pack("<H", len(s)) + s.encode()


def nwd(blocks: list[dict], opaque: tuple[int, ...] = (), vblank: int = 7) -> bytes:
    out = tool.MAGIC + struct.pack("<iI", vblank, 1000 + vblank)
    for i, b in enumerate(blocks):
        data = b.get("data", b"")
        out += struct.pack("<6I", i, b.get("kind", tool.ABS), b.get("parent", tool.NONE), b.get("base", 0), len(data),
                           b.get("count", 1))
        out += text(b.get("name", "")) + text(b.get("type", "")) + text(b.get("path", "")) + data
        relocs = b.get("relocs", [])
        out += struct.pack("<I", len(relocs)) + b"".join(struct.pack("<3I", *r) for r in relocs)
    out += struct.pack("<II", tool.NONE, len(opaque)) + b"".join(struct.pack("<Q", h) for h in opaque)
    return out + struct.pack("<4I", 0, 0, 0, 0)


def ram_with(chunks: dict[int, bytes]) -> bytes:
    ram = bytearray(0x100000)
    for addr, data in chunks.items():
        ram[addr:addr + len(data)] = data
    return bytes(ram)


def f32(*v: float) -> bytes:
    return struct.pack(f"<{len(v)}f", *v)


def obj(nxt: int, pos: tuple[float, float, float], ids: tuple[int, ...], count: int) -> bytes:
    return struct.pack("<I", nxt) + f32(*pos) + struct.pack("<4hi", *ids, count)


class ReadAndPlace(unittest.TestCase):
    def test_round_trip_of_the_format(self):
        blocks = [{"name": "root", "base": 0x1000, "data": b"\1\2\3\4", "relocs": [(0, 1, 8)]},
                  {"kind": tool.FREE, "type": "struct obj", "path": "root[0]", "data": bytes(28), "count": 1}]
        d = tool.read_dump(nwd(blocks, opaque=(0xDEAD,), vblank=42))
        self.assertEqual((d.vblank, d.vbcnt, len(d.blocks), d.opaque), (42, 1042, 2, [0xDEAD]))
        self.assertEqual((d.blocks[0].name, d.blocks[0].relocs, d.blocks[1].type), ("root", [(0, 1, 8)], "struct obj"))
        self.assertEqual(d.blocks[1].label(), "struct obj <- root[0]")

    def test_free_blocks_go_where_most_pointers_say_and_children_follow(self):
        blocks = [{"name": "a", "base": 0x100, "data": bytes(8), "relocs": [(0, 2, 0), (4, 2, 0x10)]},
                  {"name": "b", "base": 0x200, "data": bytes(4), "relocs": [(0, 2, 0)]},
                  {"kind": tool.FREE, "name": "heap", "data": bytes(0x40)},
                  {"kind": tool.IN, "parent": 2, "base": 0x20, "type": "struct obj", "data": bytes(28)}]
        ram = ram_with({0x100: struct.pack("<II", 0x8000, 0x8010), 0x200: struct.pack("<I", 0x9000)})
        base, split = tool.place(tool.read_dump(nwd(blocks)), ram)
        self.assertEqual((base[2], base[3], split), (0x8000, 0x8020, 1))


class Compare(unittest.TestCase):
    def compare(self, blocks, ram, known=None, **kw):
        return tool.compare(tool.read_dump(nwd(blocks)), ram, tool.Fields(TYPEDB), known, **kw)

    def test_equal_blocks_and_pointers_report_nothing(self):
        o = obj(0, (1.0, 2.0, 3.0), (1, 2, 3, 4), 5)
        blocks = [{"name": "o", "base": 0x400, "type": "struct obj", "data": o, "relocs": [(0, 1, 0)]},
                  {"kind": tool.FREE, "type": "struct obj", "data": o}]
        ram = ram_with({0x400: obj(0x800, (1.0, 2.0, 3.0), (1, 2, 3, 4), 5), 0x800: o})
        res = self.compare(blocks, ram)
        self.assertEqual((res.compared, res.differ_blocks, res.pointers, res.pointer_diffs), (2, 0, 1, 0))

    def test_differences_are_named_by_field(self):
        blocks = [{"name": "objs", "base": 0x400, "type": "struct obj", "count": 2,
                   "data": obj(0, (1, 2, 3), (1, 2, 3, 4), 5) + obj(0, (1, 2, 3), (1, 2, 3, 4), 5)}]
        ram = ram_with({0x400: obj(0, (1, 2, 3), (1, 2, 3, 4), 5) + obj(0, (1, 2, 9), (1, 7, 3, 4), 6)})
        res = self.compare(blocks, ram)
        self.assertEqual((res.differ_blocks, res.differ_fields), (1, 3))
        text = "\n".join(res.lines)
        for want in ("[1].pos.z: port 3, PCSX2 9", "[1].ids[1]: port 2, PCSX2 7", "[1].count: port 5, PCSX2 6"):
            self.assertIn(want, text)

    def test_a_pointer_to_the_wrong_place_is_a_pointer_difference(self):
        blocks = [{"name": "p", "base": 0x400, "data": bytes(4), "relocs": [(0, 1, 4)]},
                  {"name": "q", "base": 0x800, "data": bytes(16)},
                  {"name": "r", "base": 0x900, "data": bytes(4), "relocs": [(0, 1, 8)]}]
        res = self.compare(blocks, ram_with({0x400: struct.pack("<I", 0x804), 0x900: struct.pack("<I", 0x80C)}))
        self.assertEqual((res.pointers, res.pointer_diffs, res.differing), (2, 1, ["r"]))

    def test_float_rounding_is_counted_not_listed(self):
        blocks = [{"name": "v", "base": 0x400, "type": "struct vec", "data": f32(1.0, 0.0, 2.0)},
                  {"name": "w", "base": 0x500, "data": f32(0.5) + struct.pack("<I", 0x954)}]
        ram = bytearray(ram_with({0x400: f32(1.0, -0.0, 2.5), 0x500: f32(0.5) + struct.pack("<I", 0x952)}))
        ram[0x400:0x404] = struct.pack("<I", struct.unpack("<I", f32(1.0))[0] + 3)
        ram[0x500:0x504] = struct.pack("<I", struct.unpack("<I", f32(0.5))[0] - 2)
        res = self.compare(blocks, bytes(ram))
        self.assertEqual(res.floats, 3)  # v.x 3 ulps, v.y -0, w+0x0 2 ulps
        self.assertEqual((res.differ_fields, sorted(res.differing)), (2, ["v", "w"]))  # v.z, and the int w+0x4

    def test_holes_and_known_rules(self):
        heap = bytes(range(64))
        blocks = [{"name": "sb", "base": 0x1000, "data": heap},
                  {"kind": tool.IN, "parent": 0, "base": 0x10, "type": "hole", "name": "DMA", "count": 0x10},
                  {"name": "stack", "base": 0x2000, "data": b"\1\1\1\1"}]
        ram = bytearray(ram_with({0x1000: heap}))
        ram[0x1010:0x1020] = b"\xff" * 16
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "known.txt"
            path.write_text("^stack\\.  # EE thread stacks\n", encoding="utf-8")
            res = self.compare(blocks, bytes(ram), tool.Known(path))
        self.assertEqual((res.differ_blocks, dict(res.known)), (0, {"DMA": 1, "EE thread stacks": 1}))

    def test_unplaced_host_pointers_only_have_to_agree(self):
        blocks = [{"name": "a", "base": 0x400, "data": bytes(12),
                   "relocs": [(0, tool.OPAQUE | 0, 0), (4, tool.OPAQUE | 0, 0), (8, tool.OPAQUE | 1, 0)]}]
        res = self.compare(blocks, ram_with({0x400: struct.pack("<III", 5, 6, 7)}))
        self.assertEqual((res.opaque_slots, res.opaque_split, res.differ_blocks), (3, 1, 0))


class Pokes(unittest.TestCase):
    def test_pokes_by_symbol_offset_and_address(self):
        syms = {"new_level": 0x630B94, "Level": 0x630B90}
        got = pcsx2_dump.parse_pokes("300:new_level=0x25, 10:Level+4=-1,20:0x00100000=7", syms)
        self.assertEqual(got, [(10, "Level+4=-1", 0x630B94, 0xFFFFFFFF), (20, "0x00100000=7", 0x100000, 7),
                               (300, "new_level=0x25", 0x630B94, 0x25)])
        with self.assertRaises(SystemExit):
            pcsx2_dump.parse_pokes("300:new_level", syms)


if __name__ == "__main__":
    unittest.main()
