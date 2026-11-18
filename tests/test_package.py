"""tools/package.py and tools/deny_gamedata.py on a stand-in build: staged files, archives, checksums, the deny-list."""
import contextlib
import hashlib
import io
import re
import sys
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import deny_gamedata  # noqa: E402
import package  # noqa: E402


def quiet(fn, *args):
    with contextlib.redirect_stdout(io.StringIO()):
        return fn(*args)


class Package(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.build = self.root / "build"
        for dep in ("sdl3", "imgui"):
            lic = self.build / "_deps" / f"{dep}-src" / "LICENSE.txt"
            lic.parent.mkdir(parents=True)
            lic.write_text(f"{dep} licence\n")
        (self.build / "NuWorld.exe").write_bytes(b"MZ" + bytes(64))
        (self.build / "NuWorld").write_bytes(b"\x7fELF\x02\x01\x01" + bytes(64))

    def tearDown(self):
        self.tmp.cleanup()

    def test_version(self):
        self.assertRegex(package.version(), r"^\d+\.\d+\.\d+$")
        self.assertTrue(package.version(full=True).startswith(package.version()))

    def test_windows(self):
        out = self.root / "dist"
        self.assertTrue(quiet(package.stage, self.build, "windows-x64", out))
        staged = out / "crashwoc-nuworld-windows-x64"
        names = sorted(p.name for p in staged.iterdir())
        self.assertEqual(names, ["LICENSE.txt", "NuWorld.exe", "README.txt", "THIRD_PARTY_NOTICES.txt"])
        readme = (staged / "README.txt").read_bytes()
        self.assertIn(package.version(full=True).encode(), readme)
        self.assertNotIn(b"{version}", readme)
        self.assertEqual(readme.count(b"\n"), readme.count(b"\r\n"))
        notices = (staged / "THIRD_PARTY_NOTICES.txt").read_text()
        self.assertIn("sdl3 licence", notices)
        self.assertIn("imgui licence", notices)
        self.assertRegex(notices, r"SDL release-\S+ \(zlib License\)")

        self.assertTrue(quiet(package.archive, "CrashWOC-NuWorld-v9.9.9", self.root / "out", [staged]))
        zip_path = self.root / "out" / "CrashWOC-NuWorld-v9.9.9-windows-x64.zip"
        with zipfile.ZipFile(zip_path) as z:
            self.assertIn("CrashWOC-NuWorld-v9.9.9-windows-x64/NuWorld.exe", z.namelist())
            self.assertEqual(len(z.namelist()), 4)
        sums = (self.root / "out" / "SHA256SUMS").read_text()
        self.assertEqual(sums, f"{hashlib.sha256(zip_path.read_bytes()).hexdigest()}  {zip_path.name}\n")

    def test_linux(self):
        out = self.root / "dist"
        self.assertTrue(quiet(package.stage, self.build, "linux-x86_64", out))
        staged = out / "crashwoc-nuworld-linux-x86_64"
        self.assertTrue((staged / "nuworld.desktop").is_file())
        self.assertTrue((staged / "nuworld.png").is_file())
        self.assertNotIn(b"\r\n", (staged / "README.txt").read_bytes())
        self.assertTrue(quiet(package.archive, "b", self.root / "out", [staged]))
        with tarfile.open(self.root / "out" / "b-linux-x86_64.tar.gz") as t:
            modes = {m.name: m.mode for m in t.getmembers()}
        self.assertEqual(modes["b-linux-x86_64/NuWorld"], 0o755)
        self.assertEqual(modes["b-linux-x86_64/README.txt"], 0o644)
        self.assertEqual(len(modes), 6)

    def test_game_data_is_refused(self):
        out = self.root / "dist"
        quiet(package.stage, self.build, "windows-x64", out)
        staged = out / "crashwoc-nuworld-windows-x64"
        (staged / "SYSTEM.CNF").write_text("BOOT2 = cdrom0:\\SLES_503.86;1\n")
        self.assertFalse(quiet(package.archive, "x", self.root / "out", [staged]))
        self.assertFalse((self.root / "out" / "x-windows-x64.zip").exists())


class DenyPaths(unittest.TestCase):
    def test_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "sub").mkdir()
            (root / "readme.txt").write_text("hello")
            (root / "sub" / "NuWorld").write_bytes(b"\x7fELF\x02\x01\x01" + bytes(64))
            self.assertTrue(quiet(deny_gamedata.check, deny_gamedata.files_under([tmp])))
            (root / "sub" / "SLES_503.86").write_bytes(b"\x7fELF\x01\x01\x01" + bytes(64))
            report = io.StringIO()
            with contextlib.redirect_stdout(report):
                self.assertFalse(deny_gamedata.check(deny_gamedata.files_under([tmp])))
            self.assertEqual(len(re.findall(r"SLES_503\.86: ", report.getvalue())), 2)  # name and ELF magic


if __name__ == "__main__":
    unittest.main()
