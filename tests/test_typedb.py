"""typedb on the synthetic decomp in tests/fixtures/mini (needs libclang)."""
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import typedb  # noqa: E402

DECOMP = REPO / "tests" / "fixtures" / "mini" / "decomp"


@unittest.skipIf(typedb.cindex is None, "no libclang (tools/requirements.txt)")
class TypeDB(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.db = typedb.build(DECOMP)
        cls.node = cls.db["records"]["struct fixnode_s"]["fields"]

    def layout(self, view):
        return [(f["name"], f[view]["offset"], f[view]["size"]) for f in self.node]

    def test_records(self):
        self.assertIsNone(self.db["decomp"])
        self.assertIn("nucore/fixtypes.h", self.db["headers"])
        self.assertLessEqual({"struct fixnode_s", "struct fixnode_s$1", "struct fixnode_s$1$1",
                              "struct fixnode_s$2", "FixPair", "struct fixvec_s"},
                             set(self.db["records"]))
        node = self.db["records"]["struct fixnode_s"]
        self.assertEqual(node["where"], "nucore/fixtypes.h:11")
        self.assertEqual((node["ps2"], node["host"]),
                         ({"size": 40, "align": 8}, {"size": 40, "align": 8}))

    def test_layouts(self):
        self.assertEqual(self.layout("ps2"), [
            ("next", 0, 4), ("", 4, 4), ("named", 8, 4), ("tag", 16, 8), ("kind", 22, 8),
            ("x", 24, 4), ("", 24, 4), ("y", 24, 4), ("name", 26, 6), ("cb", 32, 4)])
        self.assertEqual(self.layout("host"), [
            ("next", 0, 8), ("", 8, 4), ("named", 12, 4), ("tag", 16, 8), ("kind", 22, 8),
            ("x", 24, 4), ("", 24, 4), ("y", 24, 4), ("name", 26, 6), ("cb", 32, 8)])
        bits = [(f["name"], f["ps2"]["bit"], f["ps2"]["bits"], f["ps2"]["unit"])
                for f in self.node if "bit" in f["ps2"]]
        self.assertEqual(bits, [("tag", 128, 48, 16), ("kind", 176, 16, 16), ("x", 192, 3, 24),
                                ("", 195, 2, 24), ("y", 197, 4, 24)])
        misc = {f["name"]: f for f in self.db["records"]["struct fixmisc_s"]["fields"]}
        self.assertEqual([(misc[n]["ps2"]["unit"], misc[n]["host"]["unit"]) for n in ("lo", "hi")],
                         [(28, 48), (28, 48)])
        inner = self.db["records"]["struct fixnode_s$1$1"]["fields"]
        self.assertEqual([(f["name"], f["ps2"]["offset"]) for f in inner], [("lo", 0), ("hi", 2)])

    def test_kinds(self):
        by = {f["name"]: f for f in self.node if f["name"]}
        self.assertEqual(by["next"]["kind"], "pointer")
        self.assertEqual(by["next"]["pointee"], {"kind": "record", "ref": "struct fixnode_s"})
        self.assertEqual(self.node[1]["ref"], "struct fixnode_s$1")
        self.assertEqual((by["named"]["kind"], by["named"]["ref"], by["named"]["type"]),
                         ("record", "struct fixnode_s$2", "struct fixnode_s$2"))
        self.assertEqual(by["name"]["type"], "char[2][3]")
        self.assertEqual(by["name"]["count"], 2)
        self.assertEqual(by["name"]["element"],
                         {"kind": "array", "count": 3,
                          "element": {"kind": "int", "signed": True, "size": 1}})
        self.assertEqual(by["cb"]["kind"], "fnptr")
        self.assertEqual(by["tag"], {**by["tag"], "kind": "int", "signed": False, "size": 8,
                                     "type": "fixu64"})

    def test_pointees_and_tags(self):
        misc = {f["name"]: f for f in self.db["records"]["struct fixmisc_s"]["fields"]}
        node = {"kind": "record", "ref": "struct fixnode_s"}
        self.assertEqual(misc["label"]["pointee"],
                         {"kind": "int", "signed": True, "size": 1, "const": True})
        self.assertEqual(misc["nodes"]["pointee"], {"kind": "pointer", "pointee": node})
        self.assertEqual(misc["rows"]["pointee"], {"kind": "array", "count": 4, "element": {
            "kind": "int", "signed": True, "size": 4}})
        self.assertEqual(misc["cbs"]["pointee"], {"kind": "fnptr"})
        self.assertEqual(misc["opaque"]["pointee"], {"kind": "record", "ref": "struct fixopaque_s"})
        self.assertNotIn("struct fixopaque_s", self.db["records"])
        self.assertEqual((misc["addr"]["typedef"], misc["addr"]["ps2"]["size"],
                          misc["addr"]["host"]["size"]), ("nuintptr", 4, 8))
        self.assertEqual([(misc[n]["ref"], misc[n]["signed"]) for n in ("mode", "sign")],
                         [("enum fixmode2_e", False), ("enum fixsign_e", True)])

    def test_lint(self):
        self.assertEqual(self.db["lint"], [{"kind": "long", "record": "struct fixlong_s",
                                            "field": "wide", "where": "nucore/fixtypes.h:32"}])

    def test_typedefs_and_enums(self):
        self.assertEqual(self.db["typedefs"]["FixPair"]["ref"], "FixPair")
        self.assertEqual(self.db["typedefs"]["fixcb_t"]["kind"], "fnptr")
        self.assertEqual(self.db["typedefs"]["s32"]["kind"], "int")
        self.assertEqual(self.db["enums"]["enum fixmode2_e"],
                         {"where": "nucore/fixtypes.h:9", "constants": {"FIXM_A": 1, "FIXM_B": 4},
                          "signed": False, "size": 4})
        self.assertTrue(self.db["enums"]["enum fixsign_e"]["signed"])

    def test_header_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            inc = Path(tmp) / "include"
            inc.mkdir()
            (inc / "bad.h").write_text("struct bad_s { int a }\n")
            with self.assertRaisesRegex(typedb.TypeDBError, "ps2 view"):
                typedb.build(Path(tmp))

    def test_straddling_bitfield(self):
        with tempfile.TemporaryDirectory() as tmp:
            inc = Path(tmp) / "include"
            inc.mkdir()
            (inc / "packed.h").write_text(
                "struct packed_s { char a; int b : 30; } __attribute__((packed));\n")
            with self.assertRaisesRegex(typedb.TypeDBError, r"packed_s\.b straddles"):
                typedb.build(Path(tmp))


if __name__ == "__main__":
    unittest.main()
