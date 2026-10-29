"""tools/ee_gate.py's count of host-only blocks (the EE build itself needs the decomp image)."""
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import ee_gate  # noqa: E402

PATCH = b"""--- a/src/a.c
+++ b/src/a.c
@@ -1,2 +1,5 @@
 int a;
+#ifdef NW_HOST
+int host;
+#endif
 int b;
"""


class HostBlocks(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.series, self.counts = root / "patches", root / "ee_gate.toml"
        (self.series / "10-x" / "src").mkdir(parents=True)
        (self.series / "series.toml").write_text('schema = 1\n\n[[concern]]\nname = "10-x"\nreason = "x"\n')
        (self.series / "10-x" / "src" / "a.c.patch").write_bytes(PATCH)

    def tearDown(self):
        self.tmp.cleanup()

    def test_counts_match(self):
        self.counts.write_text('schema = 1\n\n[host_blocks]\n"10-x" = 1\n')
        self.assertEqual(ee_gate.host_blocks(self.series), {"10-x": 1})
        self.assertEqual(ee_gate.lint(self.series, self.counts), [])

    def test_a_new_block_needs_its_count(self):
        self.counts.write_text("schema = 1\n\n[host_blocks]\n")
        self.assertRegex(ee_gate.lint(self.series, self.counts)[0], "10-x: the patches add 1 .* says 0")


if __name__ == "__main__":
    unittest.main()
