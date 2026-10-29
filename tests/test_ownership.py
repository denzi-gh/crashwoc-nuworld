"""tools/ownership.py on the synthetic decomp in tests/fixtures/mini."""
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import nwunits  # noqa: E402
import ownership  # noqa: E402

FIXTURE = REPO / "tests" / "fixtures" / "mini"
DECOMP = FIXTURE / "decomp"
UNITS = FIXTURE / "port" / "units.toml"
PORT_SRC = FIXTURE / "port" / "src"
PATCH = b"""--- a/src/nucore/fixcore.c
+++ b/src/nucore/fixcore.c
@@ -10,3 +10,3 @@
 s32 FixAdd(s32 a, s32 b)
 {
-    return a + b;
+    return b + a;
"""


class Ownership(unittest.TestCase):
    def test_kinds(self):
        with tempfile.TemporaryDirectory() as tmp:
            series = Path(tmp)
            (series / "40-x" / "src" / "nucore").mkdir(parents=True)
            (series / "series.toml").write_text('schema = 1\n\n[[concern]]\nname = "40-x"\nreason = "x"\n')
            (series / "40-x" / "src" / "nucore" / "fixcore.c.patch").write_bytes(PATCH)
            changes = ownership.touched(DECOMP, series)
        model = nwunits.load(DECOMP, UNITS, PORT_SRC)
        rows = {r["name"]: r for r in ownership.report(model, changes, ownership.port_definitions(PORT_SRC))}
        self.assertEqual((rows["FixAdd"]["kind"], rows["FixAdd"]["concerns"]), ("patched", ["40-x"]))
        self.assertEqual(rows["FixCallLocal"]["kind"], "retail")
        self.assertEqual((rows["FixLoad"]["kind"], rows["FixLoad"]["layer"], rows["FixLoad"]["where"]),
                         ("copy", "port", "src/fiximpl.c"))
        self.assertEqual(rows["FixLocal"]["kind"], "wrap")
        self.assertEqual(rows["FixMissing"]["kind"], "stub")
        self.assertEqual(rows["_start"]["kind"], "shim")
        self.assertIn("| patched | 1 |", ownership.render_md(list(rows.values())))


if __name__ == "__main__":
    unittest.main()
