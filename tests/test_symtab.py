import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import symtab  # noqa: E402

BASE = 0x140000000
MAP = """ hello

 Timestamp is 6afce5d6 (Wed Nov 18 00:40:06 2026)

 Preferred load address is 0000000140000000

 Start         Length     Name                   Class
 0001:00000000 00001ce0H .text                   CODE

  Address         Publics by Value              Rva+Base               Lib:Object

 0000:00000000       __guard_flags              0000000000000100     <absolute>
 0001:00000500       big                        0000000140001500 f   hello.c.obj
 0001:00000530       WinMain                    0000000140001530 f   hello.c.obj
 0001:0000207c       ?set@policy@@SAHXZ         000000014000307c f i msvcrt:exe_winmain.obj
 0002:00000010       __imp_GetTickCount         0000000140006010     kernel32:KERNEL32.dll

 entry point at        0001:00000914

 Static symbols

 0001:00000080       helper                     0000000140001080 f   hello.c.obj
 0001:00000500       alias                      0000000140001500 f   hello.c.obj
"""


def pe(stamp, size, magic=0x20B):
    data = bytearray(0x200)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<I", data, 0x88, stamp)
    struct.pack_into("<H", data, 0x98, magic)
    struct.pack_into("<Q", data, 0x98 + 24, BASE)
    struct.pack_into("<I", data, 0x98 + 56, size)
    return bytes(data)


class Symtab(unittest.TestCase):
    def test_image(self):
        self.assertEqual(symtab.image(pe(0x6AFCE5D6, 0x9000)), (0x6AFCE5D6, BASE, 0x9000))
        for bad in (b"", b"MZ" + bytes(0x100), pe(1, 2, magic=0x10B)):
            with self.assertRaises(ValueError):
                symtab.image(bad)

    def test_symbols(self):
        self.assertEqual(symtab.symbols(MAP, BASE, 0x9000),
                         [(0x1080, "helper"), (0x1500, "big"), (0x1530, "WinMain"), (0x307C, "?set@policy@@SAHXZ")])
        self.assertEqual(symtab.symbols(MAP, BASE, 0x1100), [(0x1080, "helper")])

    def test_render(self):
        self.assertEqual(symtab.render(0x6AFCE5D6, 0x9000, [(0x1080, "helper"), (0x1500, "big")]),
                         "nwsym 6afce5d6 00009000\n1080 helper\n1500 big\n")

    def test_cli(self):
        with tempfile.TemporaryDirectory() as tmp:
            tmp = Path(tmp)
            (tmp / "a.exe").write_bytes(pe(0x6AFCE5D6, 0x9000))
            (tmp / "a.map").write_text(MAP)
            tool = [sys.executable, str(REPO / "tools" / "symtab.py")]
            run = subprocess.run([*tool, str(tmp / "a.map"), str(tmp / "a.exe"), str(tmp / "a.sym")],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertEqual((tmp / "a.sym").read_bytes().split(b"\n")[:2], [b"nwsym 6afce5d6 00009000", b"1080 helper"])
            (tmp / "empty.map").write_text(" hello\n")
            run = subprocess.run([*tool, str(tmp / "empty.map"), str(tmp / "a.exe"), str(tmp / "b.sym")],
                                 capture_output=True, text=True)
            self.assertEqual(run.returncode, 1)
            self.assertIn("lists no functions", run.stderr)
            self.assertFalse((tmp / "b.sym").exists())


if __name__ == "__main__":
    unittest.main()
