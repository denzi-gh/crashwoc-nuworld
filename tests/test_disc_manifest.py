"""The committed disc manifest is valid generator output; gen_disc_manifest reads directories and JSON."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_disc_manifest as gdm  # noqa: E402


class Committed(unittest.TestCase):
    def setUp(self):
        self.text = gdm.INC.read_text(encoding="utf-8")
        self.manifest = gdm.parse_inc(self.text)

    def test_valid(self):
        self.assertEqual(gdm.validate(self.manifest), [])

    def test_is_generator_output(self):
        self.assertEqual(gdm.render(self.manifest), self.text)

    def test_only_dummy_is_skipped(self):
        skipped = [line for line in self.text.splitlines() if "NW_MANIFEST_SKIP" in line]
        self.assertEqual(len(skipped), 1)
        self.assertIn('"DUMMY.DAT"', skipped[0])


class Sources(unittest.TestCase):
    def test_dir_and_json_agree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp) / "disc"
            (root / "levels" / "b").mkdir(parents=True)
            (root / "levels" / "b" / "a.bin").write_bytes(b"abc")
            (root / "DUMMY.DAT").write_bytes(bytes(16))
            m = gdm.from_dir(root)
            self.assertEqual(m["LEVELS/B/A.BIN"],
                             (3, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"))
            js = Path(tmp) / "manifest.json"
            js.write_text(json.dumps({p: {"sha256": s.upper(), "size": n} for p, (n, s) in m.items()}))
            self.assertEqual(gdm.compare(m, gdm.from_json(js), "x"), [])
            self.assertEqual(gdm.parse_inc(gdm.render(m)), m)

    def test_compare_reports_differences(self):
        a = {"A": (1, "0" * 64), "B": (2, "1" * 64)}
        b = {"A": (1, "f" * 64), "C": (3, "2" * 64)}
        errors = gdm.compare(a, b, "x")
        self.assertEqual(len(errors), 3)
        self.assertIn("x: B missing", errors)
        self.assertIn("x: C extra", errors)

    def test_validate_rejects(self):
        errors = gdm.validate({"levels\\a": (1, "z")})
        self.assertTrue(any("disc path" in e for e in errors))
        self.assertTrue(any("bad size or hash" in e for e in errors))
        self.assertTrue(any("SLES_503.86" in e for e in errors))
        self.assertTrue(any("DUMMY.DAT: missing" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
