"""tools/copies.py on the synthetic decomp in tests/fixtures/mini (FixLoad is overridden, FixLocal wrapped)."""
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import copies  # noqa: E402

FIXTURE = REPO / "tests" / "fixtures" / "mini"
DECOMP = FIXTURE / "decomp"
UNITS = FIXTURE / "port" / "units.toml"
PORT_SRC = FIXTURE / "port" / "src"
FIXCORE = Path("src") / "nucore" / "fixcore.c"


class Copies(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def edited(self, name, old, new):
        tree = self.root / name
        shutil.copytree(DECOMP, tree)
        path = tree / FIXCORE
        text = path.read_text()
        self.assertIn(old, text)
        path.write_text(text.replace(old, new))
        return tree

    def test_lock_round_trip(self):
        hashes, errors = copies.current(DECOMP, DECOMP, UNITS, PORT_SRC)
        self.assertEqual(errors, [])
        self.assertIn("nucore/fixcore:FixLoad", hashes)
        self.assertIn("nucore/fixcore:FixLocal", hashes)
        lock = self.root / "copies.lock"
        lock.write_text(copies.render(hashes))
        self.assertEqual(copies.check(hashes, lock), [])

    def test_a_decomp_change_needs_a_look(self):
        hashes, _ = copies.current(DECOMP, DECOMP, UNITS, PORT_SRC)
        lock = self.root / "copies.lock"
        lock.write_text(copies.render(hashes))
        newer = self.edited("newer", "FixLocal(s32 v)\n{", "FixLocal(s32 v)\n{\n    v++;")
        hashes, errors = copies.current(newer, newer, UNITS, PORT_SRC)
        self.assertEqual(errors, [])
        self.assertEqual(copies.check(hashes, lock),
                         ["nucore/fixcore:FixLocal: the decomp changed it; bring NuWorld's definition in line, "
                          "then --update"])

    def test_a_patch_may_not_edit_an_overridden_function(self):
        patched = self.edited("patched", "FixLoad(const char *name)\n{", "FixLoad(const char *name)\n{\n    name++;")
        _, errors = copies.current(patched, DECOMP, UNITS, PORT_SRC)
        self.assertEqual(len(errors), 1)
        self.assertRegex(errors[0], "^nucore/fixcore:FixLoad: a port patch edits it")

    def test_a_patch_may_edit_a_wrapped_function(self):
        patched = self.edited("patched", "FixLocal(s32 v)\n{", "FixLocal(s32 v)\n{\n    v++;")
        self.assertEqual(copies.current(patched, DECOMP, UNITS, PORT_SRC)[1], [])


if __name__ == "__main__":
    unittest.main()
