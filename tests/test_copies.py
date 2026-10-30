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

    def current(self, decomp, pristine=None, values=None):
        return copies.current(decomp, pristine or decomp, UNITS, PORT_SRC, values)

    def locked(self, locks):
        lock = self.root / "copies.lock"
        lock.write_text(copies.render(locks))
        return lock

    def test_lock_round_trip(self):
        locks, errors = self.current(DECOMP)
        self.assertEqual(errors, [])
        self.assertEqual(list(locks["copies"]), ["nucore/fixcore:FixLoad"])  # a wrap is not locked
        self.assertEqual(copies.check(locks, self.locked(locks)), [])

    def test_a_decomp_change_needs_a_look(self):
        lock = self.locked(self.current(DECOMP)[0])
        newer = self.edited("newer", "FixLoad(const char *name)\n{", "FixLoad(const char *name)\n{\n    name++;")
        locks, errors = self.current(newer)
        self.assertEqual(errors, [])
        self.assertEqual(copies.check(locks, lock),
                         ["nucore/fixcore:FixLoad: the decomp changed it; bring NuWorld's definition in line, "
                          "then --update"])

    def test_a_patch_may_not_edit_an_overridden_function(self):
        patched = self.edited("patched", "FixLoad(const char *name)\n{", "FixLoad(const char *name)\n{\n    name++;")
        errors = self.current(patched, DECOMP)[1]
        self.assertEqual(len(errors), 1)
        self.assertRegex(errors[0], "^nucore/fixcore:FixLoad: a port patch or value rule edits it")

    def test_a_patch_may_edit_a_wrapped_function(self):
        patched = self.edited("patched", "FixLocal(s32 v)\n{", "FixLocal(s32 v)\n{\n    v++;")
        self.assertEqual(self.current(patched, DECOMP)[1], [])

    def test_functions_with_value_rules_are_locked(self):
        values = self.root / "values"
        values.mkdir()
        (values / "hz.toml").write_text('schema = 1\nmacro = "NW_HZ"\n\n[[file]]\npath = "src/nucore/fixcore.c"\n'
                                        'sites = [{ fn = "FixAdd", pal = "a", nth = 0, of = 1, ntsc = "b" }]\n')
        locks, errors = self.current(DECOMP, values=values)
        self.assertEqual((errors, list(locks["values"])), ([], ["src/nucore/fixcore.c:FixAdd"]))
        lock = self.locked(locks)
        reordered = self.edited("reordered", "return a + b;", "return b + a;")
        self.assertEqual(copies.check(self.current(reordered, values=values)[0], lock),
                         ["src/nucore/fixcore.c:FixAdd: the decomp changed it; prove its rules with "
                          "tools/ntsc_diff.py check, then --update"])


if __name__ == "__main__":
    unittest.main()
