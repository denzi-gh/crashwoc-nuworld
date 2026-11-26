import contextlib
import hashlib
import io
import re
import struct
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


def pe(imports=(), delayed=()):
    names, at = b"", {}
    for name in (*imports, *delayed):
        at[name] = 0x1100 + len(names)
        names += name.encode() + b"\0"
    table = b"".join(struct.pack("<5I", 0, 0, 0, at[n], 0) for n in imports) + bytes(20)
    delay = b"".join(struct.pack("<8I", 1, at[n], 0, 0, 0, 0, 0, 0) for n in delayed) + bytes(32)
    body = table.ljust(0x80, b"\0") + delay.ljust(0x80, b"\0") + names
    head = bytearray(0x200)
    head[:2] = b"MZ"
    struct.pack_into("<I", head, 0x3C, 0x40)
    head[0x40:0x44] = b"PE\0\0"
    struct.pack_into("<HH", head, 0x44, 0x8664, 1)
    struct.pack_into("<H", head, 0x54, 240)
    struct.pack_into("<H", head, 0x58, 0x20B)
    struct.pack_into("<I", head, 0x58 + 108, 16)
    if imports:
        struct.pack_into("<II", head, 0x58 + 112 + 8, 0x1000, len(table))
    if delayed:
        struct.pack_into("<II", head, 0x58 + 112 + 8 * 13, 0x1080, len(delay))
    struct.pack_into("<8sIIII", head, 0x58 + 240, b".rdata", len(body), 0x1000, len(body), 0x200)
    return bytes(head) + body


class Package(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.build = self.root / "build"
        for dep, file in (("sdl3", "LICENSE.txt"), ("imgui", "LICENSE.txt"), ("miniz", "LICENSE"),
                          ("monocypher", "LICENCE.md")):
            lic = self.build / "_deps" / f"{dep}-src" / file
            lic.parent.mkdir(parents=True)
            lic.write_text(f"{dep} licence\n")
        for _name, _fetch, _licence, _url, file in package.CRASHPAD:
            lic = self.build / "_deps" / "crashpad-src" / file
            lic.parent.mkdir(parents=True, exist_ok=True)
            lic.write_text(f"{file} licence\n")
        self.runtime = self.root / "redist" / "x64" / "Microsoft.VC143.CRT"
        self.runtime.mkdir(parents=True)
        (self.runtime / "vcruntime140.dll").write_bytes(pe(["api-ms-win-crt-runtime-l1-1-0.dll", "KERNEL32.dll"]))
        (self.runtime / "msvcp140.dll").write_bytes(pe(["VCRUNTIME140.dll"]))
        (self.runtime / "concrt140.dll").write_bytes(pe(["VCRUNTIME140.dll"]))
        (self.build / "NuWorld.exe").write_bytes(pe(["SDL3.dll", "KERNEL32.dll", "VCRUNTIME140.dll"], ["WINHTTP.dll"]))
        (self.build / "crashpad_handler.exe").write_bytes(pe(["POWRPROF.dll", "MSVCP140.dll"]))
        (self.build / "NuWorldUpdater.exe").write_bytes(pe(["SDL3.dll", "WINHTTP.dll"]))
        (self.build / "NuWorldUpdater").write_bytes(b"\x7fELF\x02\x01\x01" + bytes(64))
        (self.build / "SDL3.dll").write_bytes(pe(["USER32.dll", "ole32.dll"]))
        for name in ("NuWorld.sym", "NuWorld.pdb", "NuWorld.map"):
            (self.build / name).write_text(name)
        (self.build / "NuWorld").write_bytes(b"\x7fELF\x02\x01\x01" + bytes(64))
        (self.build / "shaders").mkdir()
        for name in package.SHADERS:
            (self.build / "shaders" / name).write_text(f"// {name}\nvoid main() {{}}\n")

    def tearDown(self):
        self.tmp.cleanup()

    def test_version(self):
        self.assertRegex(package.version(), r"^\d+\.\d+\.\d+$")
        self.assertTrue(package.version(full=True).startswith(package.version()))

    def test_imports(self):
        self.assertEqual(package.pe_imports(pe(["a.dll", "B.DLL"], ["late.dll"])), ["a.dll", "B.DLL", "late.dll"])
        self.assertEqual(package.pe_imports(pe()), [])
        for bad in (b"", b"MZ" + bytes(64)):
            with self.assertRaises((ValueError, struct.error)):
                package.pe_imports(bad)

    def test_sdl_versions_agree(self):
        tag = package.fetch_tags()["SDL3"]
        self.assertIn(f"/{tag}/SDL3-devel-{tag.removeprefix('release-')}-VC.zip", package.cmake_text())

    def test_unknown_library(self):
        (self.build / "NuWorld.exe").write_bytes(pe(["KERNEL32.dll", "other.dll"]))
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            self.assertFalse(quiet(package.stage, self.build, "windows-x64", self.root / "dist", [self.runtime]))
        self.assertIn("NuWorld.exe loads other.dll", report.getvalue())

    def test_windows(self):
        out = self.root / "dist"
        self.assertTrue(quiet(package.stage, self.build, "windows-x64", out, [self.runtime]))
        staged = out / "crashwoc-nuworld-windows-x64"
        names = sorted(p.name for p in staged.iterdir())
        self.assertEqual(names, ["LICENSE.txt", "NuWorld.exe", "NuWorld.sym", "NuWorldUpdater.exe", "README.txt",
                                 "SDL3.dll", "THIRD_PARTY_NOTICES.txt", "crashpad_handler.exe", "msvcp140.dll",
                                 "shaders", "vcruntime140.dll"])
        for name in package.SHADERS:
            self.assertEqual((staged / "shaders" / name).read_bytes(), (self.build / "shaders" / name).read_bytes())
        symbols = out / "crashwoc-nuworld-windows-x64-symbols"
        self.assertEqual(sorted(p.name for p in symbols.iterdir()), ["NuWorld.map", "NuWorld.pdb"])
        readme = (staged / "README.txt").read_bytes()
        self.assertIn(package.version(full=True).encode(), readme)
        self.assertNotIn(b"{version}", readme)
        self.assertEqual(readme.count(b"\n"), readme.count(b"\r\n"))
        notices = (staged / "THIRD_PARTY_NOTICES.txt").read_text()
        self.assertIn("sdl3 licence", notices)
        self.assertIn("imgui licence", notices)
        self.assertRegex(notices, r"miniz \S+ \(MIT License\)")
        self.assertIn("monocypher licence", notices)
        self.assertRegex(notices, r"SDL release-\S+ \(zlib License\)")
        self.assertRegex(notices, r"Crashpad [0-9a-f]{40} \(Apache License 2\.0\)")
        self.assertIn("mini_chromium (BSD 3-Clause License)", notices)
        self.assertIn("third_party/getopt/LICENSE licence", notices)

        self.assertTrue(quiet(package.archive, "CrashWOC-NuWorld-v9.9.9", self.root / "out", [staged, symbols]))
        zip_path = self.root / "out" / "CrashWOC-NuWorld-v9.9.9-windows-x64.zip"
        with zipfile.ZipFile(zip_path) as z:
            self.assertIn("CrashWOC-NuWorld/NuWorld.exe", z.namelist())
            for name in package.SHADERS:
                self.assertIn(f"CrashWOC-NuWorld/shaders/{name}", z.namelist())
            self.assertEqual(len(z.namelist()), 10 + len(package.SHADERS))
        symbols_path = self.root / "out" / "CrashWOC-NuWorld-v9.9.9-windows-x64-symbols.zip"
        with zipfile.ZipFile(symbols_path) as z:
            self.assertEqual(sorted(z.namelist()), ["CrashWOC-NuWorld/NuWorld.map", "CrashWOC-NuWorld/NuWorld.pdb"])
        sums = (self.root / "out" / "SHA256SUMS").read_text()
        self.assertEqual(sums, "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n"
                                       for p in (symbols_path, zip_path)))

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
        self.assertEqual(modes["CrashWOC-NuWorld/NuWorld"], 0o755)
        self.assertEqual(modes["CrashWOC-NuWorld/NuWorldUpdater"], 0o755)
        self.assertEqual(modes["CrashWOC-NuWorld/README.txt"], 0o644)
        for name in package.SHADERS:
            self.assertEqual(modes[f"CrashWOC-NuWorld/shaders/{name}"], 0o644)
        self.assertEqual(len(modes), 7 + len(package.SHADERS))

    def test_missing_shader_is_refused(self):
        (self.build / "shaders" / package.SHADERS[0]).unlink()
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            self.assertFalse(quiet(package.stage, self.build, "linux-x86_64", self.root / "dist"))
        self.assertIn(package.SHADERS[0], report.getvalue())

    def test_game_data_is_refused(self):
        out = self.root / "dist"
        quiet(package.stage, self.build, "windows-x64", out, [self.runtime])
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
