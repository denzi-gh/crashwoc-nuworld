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
                         ({"size": 40, "align": 8}, {"size": 48, "align": 8}))

    def test_layouts(self):
        self.assertEqual(self.layout("ps2"), [
            ("next", 0, 4), ("", 4, 4), ("named", 8, 4), ("tag", 16, 8), ("kind", 22, 8),
            ("x", 24, 4), ("", 24, 4), ("y", 24, 4), ("name", 26, 6), ("cb", 32, 4),
            ("wide", 36, 4)])
        self.assertEqual(self.layout("host"), [
            ("next", 0, 8), ("", 8, 4), ("named", 12, 4), ("tag", 16, 8), ("kind", 22, 8),
            ("x", 24, 4), ("", 24, 4), ("y", 24, 4), ("name", 26, 6), ("cb", 32, 8),
            ("wide", 40, 8)])
        bits = [(f["name"], f["ps2"]["bit"], f["ps2"]["bits"]) for f in self.node if "bit" in f["ps2"]]
        self.assertEqual(bits, [("tag", 128, 48), ("kind", 176, 16), ("x", 192, 3), ("", 195, 2),
                                ("y", 197, 4)])
        inner = self.db["records"]["struct fixnode_s$1$1"]["fields"]
        self.assertEqual([(f["name"], f["ps2"]["offset"]) for f in inner], [("lo", 0), ("hi", 2)])

    def test_kinds(self):
        by = {f["name"]: f for f in self.node if f["name"]}
        self.assertEqual(by["next"]["kind"], "pointer")
        self.assertEqual(by["next"]["pointee"], "struct fixnode_s")
        self.assertEqual(self.node[1]["ref"], "struct fixnode_s$1")
        self.assertEqual((by["named"]["kind"], by["named"]["ref"], by["named"]["type"]),
                         ("record", "struct fixnode_s$2", "struct fixnode_s$2"))
        self.assertEqual(by["name"]["type"], "char[2][3]")
        self.assertEqual(by["name"]["count"], 2)
        self.assertEqual(by["name"]["element"],
                         {"kind": "array", "count": 3, "element": {"kind": "int", "signed": True}})
        self.assertEqual(by["cb"]["kind"], "fnptr")
        self.assertEqual(by["tag"], {**by["tag"], "kind": "int", "signed": False, "type": "fixu64"})

    def test_lint(self):
        self.assertEqual(self.db["lint"], [{"kind": "long", "record": "struct fixnode_s",
                                            "field": "wide", "where": "nucore/fixtypes.h:29"}])

    def test_typedefs_and_enums(self):
        self.assertEqual(self.db["typedefs"]["FixPair"]["ref"], "FixPair")
        self.assertEqual(self.db["typedefs"]["fixcb_t"]["kind"], "fnptr")
        self.assertEqual(self.db["typedefs"]["s32"]["kind"], "int")
        self.assertEqual(self.db["enums"]["enum fixmode2_e"]["constants"],
                         {"FIXM_A": 1, "FIXM_B": 4})

    def test_header_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            inc = Path(tmp) / "include"
            inc.mkdir()
            (inc / "bad.h").write_text("struct bad_s { int a }\n")
            with self.assertRaisesRegex(typedb.TypeDBError, "ps2 view"):
                typedb.build(Path(tmp))


if __name__ == "__main__":
    unittest.main()
