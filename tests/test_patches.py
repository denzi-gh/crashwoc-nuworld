"""tools/patches.py on synthetic decomp trees and series (git for the work repository, no libclang)."""
import json
import os
import subprocess
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
        return patches.apply(self.decomp, self.out, self.patches, self.decomp.parent / "values")

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


SOURCE = b"""int f(int x)
{
    /* 25 in a comment does not count */
    return x * 25 + 25 / 2.0f / 50.0f;
}

int g(void)
{
    return 25;
}
"""


class Values(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.values = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def rules(self, sites):
        lines = ['schema = 1', 'macro = "NW_HZ"', "", "[[file]]", 'path = "src/a.c"', "sites = ["]
        lines += [f"  {s}," for s in sites] + ["]"]
        (self.values / "hz.toml").write_text("\n".join(lines) + "\n")
        return patches.load_values(self.values)[0]

    def test_sites_wrap_their_value(self):
        rules = self.rules(['{ fn = "f", pal = "25", nth = 1, of = 2, ntsc = "30" }',
                            '{ fn = "f", pal = "2.0f / 50.0f", nth = 0, of = 1, ntsc = "0.033f" }',
                            '{ fn = "g", pal = "25", nth = 0, of = 1, ntsc = "30" }'])
        out = patches.apply_values(rules, "src/a.c", SOURCE)
        self.assertIn(b"return x * 25 + NW_HZ(25, 30) / NW_HZ(2.0f / 50.0f, 0.033f);", out)
        self.assertIn(b"return NW_HZ(25, 30);", out)
        self.assertIn(b"/* 25 in a comment does not count */", out)

    def test_a_count_that_differs_stops(self):
        rules = self.rules(['{ fn = "f", pal = "25", nth = 0, of = 3, ntsc = "30" }'])
        with self.assertRaisesRegex(patches.PatchError, "2 in the function, the rule counts 3"):
            patches.apply_values(rules, "src/a.c", SOURCE)
        rules = self.rules(['{ fn = "h", pal = "25", nth = 0, of = 1, ntsc = "30" }'])
        with self.assertRaisesRegex(patches.PatchError, "no function h"):
            patches.apply_values(rules, "src/a.c", SOURCE)


def git(repo: Path, *args: str) -> str:
    env = {**os.environ, **patches.IDENTITY, "GIT_EDITOR": ":"}
    return subprocess.run(["git", "-C", str(repo), "-c", "core.autocrlf=false", *args], capture_output=True,
                          check=True, env=env).stdout.decode()


class Work(unittest.TestCase):
    """setup, fixup, rebuild and rebase on a decomp git repository with one concern."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.decomp, self.patches, self.work = root / "decomp", root / "patches", root / "work"
        (self.decomp / "src").mkdir(parents=True)
        git(self.decomp, "init", "-q", "-b", "main")
        (self.decomp / "src" / "a.c").write_bytes(BASE)
        self.commit("retail")
        (self.patches / "10-x" / "src").mkdir(parents=True)
        (self.patches / "series.toml").write_text('schema = 1\n\n[[concern]]\nname = "10-x"\nreason = "x"\n',
                                                  newline="\n")
        (self.patches / "10-x" / "src" / "a.c.patch").write_bytes(hunk_patch("src/a.c", """
            @@ -4,3 +4,4 @@
             line 4
             line 5
            +host
             line 6
            """))
        patches.setup(self.decomp, self.patches, self.work)
        patches.rebuild(self.work, self.patches)  # series.toml in rebuild's form, as in the repository

    def tearDown(self):
        self.tmp.cleanup()

    def commit(self, message):
        git(self.decomp, "add", "-A")
        git(self.decomp, "commit", "-q", "-m", message)
        return git(self.decomp, "rev-parse", "HEAD").strip()

    def upstream(self, data, message):
        """A newer decomp commit; the checkout stays on the pinned one, as a fetched submodule does."""
        (self.decomp / "src" / "a.c").write_bytes(data)
        sha = self.commit(message)
        git(self.decomp, "checkout", "-q", "--detach", "HEAD~1")
        return sha

    def tree(self):
        values = self.decomp.parent / "values"
        return patches.patch_tree(patches.worktree_files(self.decomp), self.patches, values)["src/a.c"]

    def edit(self, old, new):
        f = self.work / "src" / "a.c"
        f.write_bytes(f.read_bytes().replace(old, new))

    def test_rebuild_is_stable(self):
        patches.rebuild(self.work, self.patches)
        before = patches.current_outputs(self.patches)
        self.assertEqual(patches.rebuild(self.work, self.patches), 0)
        self.assertEqual(patches.current_outputs(self.patches), before)
        self.assertIn(b"+host\n", before["10-x/src/a.c.patch"])
        self.assertIn(b'reason = "x"', before["series.toml"])

    def test_fixup_and_new_concern(self):
        self.edit(b"line 12\n", b"line 12 host\n")
        patches.fixup("10-x", work=self.work, patches=self.patches)
        self.assertIn(b"line 5\nhost\nline 6\n", self.tree())
        self.assertIn(b"line 12 host\n", self.tree())
        self.edit(b"line 18\n", b"line 18 wide\n")
        patches.fixup("20-y", "the y concern", self.work, self.patches)
        self.assertIn("20-y/src/a.c.patch", patches.current_outputs(self.patches))
        self.assertIn(b'name = "20-y"\nreason = "the y concern"', (self.patches / "series.toml").read_bytes())
        self.assertIn(b"line 18 wide\n", self.tree())
        with self.assertRaisesRegex(patches.PatchError, "no edits"):
            patches.fixup("10-x", work=self.work, patches=self.patches)

    def test_a_concern_folded_out_leaves_the_series(self):
        self.edit(b"line 18\n", b"line 18 wide\n")
        patches.fixup("20-y", "the y concern", self.work, self.patches)
        self.edit(b"line 5\nhost\n", b"line 5\n")
        patches.fixup("10-x", work=self.work, patches=self.patches)
        self.assertFalse((self.patches / "10-x").exists())
        self.assertNotIn(b'"10-x"', (self.patches / "series.toml").read_bytes())
        self.assertEqual(self.tree(), BASE.replace(b"line 18\n", b"line 18 wide\n"))

    def test_setup_keeps_unsaved_work(self):
        self.edit(b"line 2\n", b"edited\n")
        with self.assertRaisesRegex(patches.PatchError, "uncommitted edits"):
            patches.setup(self.decomp, self.patches, self.work)
        patches.setup(self.decomp, self.patches, self.work, force=True)
        self.assertNotIn(b"edited", (self.work / "src" / "a.c").read_bytes())

    def test_rebase_clean(self):
        patch = self.patches / "10-x" / "src" / "a.c.patch"
        self.assertIn(b"@@ -3,6 +3,7 @@", patch.read_bytes())
        sha = self.upstream(b"new first line\n" + BASE, "retail moves")
        self.assertTrue(patches.rebase(sha, self.decomp, self.patches, self.work))
        self.assertIn(b"@@ -4,6 +4,7 @@", patch.read_bytes())
        git(self.decomp, "checkout", "-q", sha)
        self.assertEqual(self.tree(), b"new first line\n" + BASE.replace(b"line 5\n", b"line 5\nhost\n"))

    def test_rebase_conflict_resolves(self):
        sha = self.upstream(BASE.replace(b"line 5\n", b"line five\n"), "retail renames")
        self.assertFalse(patches.rebase(sha, self.decomp, self.patches, self.work))
        with self.assertRaisesRegex(patches.PatchError, "rebase is in progress"):
            patches.rebuild(self.work, self.patches)
        (self.work / "src" / "a.c").write_bytes(BASE.replace(b"line 5\n", b"line five\nhost\n"))
        git(self.work, "add", "-A")
        git(self.work, "rebase", "--continue")
        patches.rebuild(self.work, self.patches)
        git(self.decomp, "checkout", "-q", sha)
        self.assertEqual(self.tree(), BASE.replace(b"line 5\n", b"line five\nhost\n"))


if __name__ == "__main__":
    unittest.main()
