"""The committed disc manifests are valid generator output; gen_disc_manifest reads directories and JSON."""
import json
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import gen_disc_manifest as gdm  # noqa: E402


class Committed(unittest.TestCase):
    def test_discs(self):
        for key, disc in gdm.DISCS.items():
            with self.subTest(key):
                text = disc.inc.read_text(encoding="utf-8")
                manifest = gdm.parse_inc(text, disc)
                self.assertEqual(gdm.validate(manifest, disc), [])
                self.assertEqual(gdm.render(manifest, disc), text)
                skipped = [line for line in text.splitlines() if "NW_MANIFEST_SKIP" in line]
                self.assertEqual(len(skipped), 1)
                self.assertIn('"DUMMY.DAT"', skipped[0])

    def test_header_names_the_disc(self):
        with self.assertRaises(ValueError):
            gdm.parse_inc(gdm.DISCS["usa100"].inc.read_text(encoding="utf-8"), gdm.DISCS["pal103"])


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
            pal = gdm.DISCS["pal103"]
            self.assertEqual(gdm.parse_inc(gdm.render(m, pal), pal), m)

    def test_compare_reports_differences(self):
        a = {"A": (1, "0" * 64), "B": (2, "1" * 64)}
        b = {"A": (1, "f" * 64), "C": (3, "2" * 64)}
        errors = gdm.compare(a, b, "x")
        self.assertEqual(len(errors), 3)
        self.assertIn("x: B missing", errors)
        self.assertIn("x: C extra", errors)

    def test_validate_rejects(self):
        errors = gdm.validate({"levels\\a": (1, "z")}, gdm.DISCS["pal103"])
        self.assertTrue(any("disc path" in e for e in errors))
        self.assertTrue(any("bad size or hash" in e for e in errors))
        self.assertTrue(any("SLES_503.86" in e for e in errors))
        self.assertTrue(any("DUMMY.DAT: missing" in e for e in errors))
        errors = gdm.validate({}, gdm.DISCS["usa100"])
        self.assertTrue(any("SLUS_202.38" in e and "USA v1.00" in e for e in errors))


if __name__ == "__main__":
    unittest.main()
