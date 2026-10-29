"""tools/patches.py on synthetic decomp trees and series (no git, no libclang)."""
import json
import sys
import tempfile
import textwrap
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import patches  # noqa: E402

BASE = b"".join(b"line %d\n" % i for i in range(1, 21))


def hunk_patch(path: str, body: str) -> bytes:
    return f"--- a/{path}\n+++ b/{path}\n".encode() + textwrap.dedent(body).lstrip("\n").encode()


class Series(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.decomp, self.patches, self.out = root / "decomp", root / "patches", root / "tree"
        (self.decomp / "src").mkdir(parents=True)
        (self.decomp / "src" / "a.c").write_bytes(BASE.replace(b"\n", b"\r\n"))
        (self.decomp / "src" / "b.c").write_bytes(b"int b;\n")
        self.patches.mkdir()

    def tearDown(self):
        self.tmp.cleanup()

    def series(self, *names):
        text = "schema = 1\n" + "".join(f'[[concern]]\nname = "{n}"\n' for n in names)
        (self.patches / "series.toml").write_text(text, newline="\n")
        for n in names:
            (self.patches / n).mkdir(exist_ok=True)

    def patch(self, concern, path, body):
        dest = self.patches / concern / f"{path}.patch"
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(hunk_patch(path, body))

    def apply(self):
        return patches.apply(self.decomp, self.out, self.patches)

    def test_plain_copy_is_lf(self):
        self.assertEqual(self.apply(), (2, 2))
        self.assertEqual((self.out / "src" / "a.c").read_bytes(), BASE)

    def test_patch_and_new_file(self):
        self.series("10-x")
        self.patch("10-x", "src/a.c", """
            @@ -4,3 +4,4 @@ ctx
             line 4
             line 5
            +inserted
             line 6
            """)
        (self.patches / "files" / "include").mkdir(parents=True)
        (self.patches / "files" / "include" / "host.h").write_bytes(b"#define HOST 1\r\n")
        self.assertEqual(self.apply(), (3, 3))
        self.assertIn(b"line 5\ninserted\nline 6\n", (self.out / "src" / "a.c").read_bytes())
        self.assertEqual((self.out / "include" / "host.h").read_bytes(), b"#define HOST 1\n")
        self.assertEqual(self.apply(), (3, 0))

    def test_moved_hunk_applies_unique_match(self):
        self.series("10-x")
        self.patch("10-x", "src/a.c", """
            @@ -1,3 +1,3 @@
             line 10
            -line 11
            +eleven
             line 12
            """)
        self.apply()
        self.assertIn(b"line 10\neleven\nline 12\n", (self.out / "src" / "a.c").read_bytes())

    def test_mismatch_and_ambiguity_fail(self):
        self.series("10-x")
        self.patch("10-x", "src/a.c", """
            @@ -4,1 +4,1 @@
            -line 99
            +x
            """)
        with self.assertRaisesRegex(patches.PatchError, "matches nowhere"):
            self.apply()
        (self.decomp / "src" / "a.c").write_bytes(b"same\nsame\n")
        self.patch("10-x", "src/a.c", """
            @@ -5,1 +5,1 @@
            -same
            +x
            """)
        with self.assertRaisesRegex(patches.PatchError, "matches 2 places"):
            self.apply()

    def test_no_newline_at_end(self):
        (self.decomp / "src" / "b.c").write_bytes(b"int b;")
        self.series("10-x")
        self.patch("10-x", "src/b.c", """
            @@ -1 +1 @@
            -int b;
            \\ No newline at end of file
            +int b = 1;
            \\ No newline at end of file
            """)
        self.apply()
        self.assertEqual((self.out / "src" / "b.c").read_bytes(), b"int b = 1;")

    def test_new_file_may_not_shadow_the_decomp(self):
        (self.patches / "files" / "src").mkdir(parents=True)
        (self.patches / "files" / "src" / "b.c").write_bytes(b"int c;\n")
        with self.assertRaisesRegex(patches.PatchError, "patch it instead"):
            self.apply()

    def test_series_lists_every_directory(self):
        self.series("10-x")
        (self.patches / "20-y").mkdir()
        with self.assertRaisesRegex(patches.PatchError, "not in the series: 20-y"):
            self.apply()

    def test_cr_in_a_patch_fails(self):
        self.series("10-x")
        (self.patches / "10-x" / "src").mkdir(parents=True)
        (self.patches / "10-x" / "src" / "b.c.patch").write_bytes(b"--- a/src/b.c\r\n+++ b/src/b.c\r\n")
        with self.assertRaisesRegex(patches.PatchError, "CR line endings"):
            self.apply()

    def test_stale_files_go_and_the_stamp_names_the_tree(self):
        self.apply()
        (self.decomp / "src" / "b.c").unlink()
        (self.out / "compiler").mkdir()
        self.apply()
        self.assertFalse((self.out / "src" / "b.c").exists())
        self.assertTrue((self.out / "compiler").is_dir())
        stamp = json.loads((self.out / patches.STAMP).read_text())
        self.assertEqual(stamp["files"], ["src/a.c"])
        self.assertEqual(patches.tree_id(self.out), f"None+{stamp['series'][:12]}")
        self.assertIsNone(patches.tree_id(self.decomp))


if __name__ == "__main__":
    unittest.main()
