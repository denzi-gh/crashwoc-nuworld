"""pcsx2_capture's state reading and labels, title_frames' PNG codec and pairing, title_capture's label comparison
(no PCSX2, no game data)."""
import os
import struct
import sys
import tempfile
import time
import unittest
import zipfile
import zlib
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "tests"))
import pcsx2_capture  # noqa: E402
import title_capture  # noqa: E402
import title_frames as tool  # noqa: E402

SYMBOLS = {"nuvideo_global_vbcnt": 0x62F02C, "ntal_first": 0x62EF34}


def ram_with(words: dict[int, int]) -> bytes:
    ram = bytearray(0x900000)
    for addr, v in words.items():
        ram[addr & 0x1FFFFFF:(addr & 0x1FFFFFF) + 4] = struct.pack("<I", v & 0xFFFFFFFF)
    return bytes(ram)


def png_paeth(width: int, height: int, rgba: bytes) -> bytes:
    """An RGBA PNG whose rows cycle through the filters 0-4, as libpng writes them."""
    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else b if pb <= pc else c

    stride, rows, prev = width * 4, b"", bytes(width * 4)
    for y in range(height):
        line, kind = rgba[y * stride:(y + 1) * stride], y % 5
        out = bytearray()
        for i, v in enumerate(line):
            a = line[i - 4] if i >= 4 else 0
            b, c = prev[i], prev[i - 4] if i >= 4 else 0
            pred = (0, a, b, (a + b) // 2, paeth(a, b, c))[kind]
            out.append((v - pred) & 255)
        rows += bytes([kind]) + bytes(out)
        prev = line

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b""))


class LabelTest(unittest.TestCase):
    def test_labels(self):
        ram = ram_with({
            0x62F02C: 1500, 0x633388: 1, 0x6FF704: 0x800000, 0x800070: struct.unpack("<I", struct.pack("<f", 25.5))[0],
            0x62EF34: 0x810000, 0x810000: 0x820000, 0x820014: 0x830000, 0x830000: 0x840000, 0x8300E4: 7,
            0x840008: int.from_bytes(b"titl", "little"), 0x84000C: int.from_bytes(b"e", "little"),
        })
        self.assertEqual(pcsx2_capture.labels(ram, SYMBOLS, 40), (1460, "frame 1460: world 1 time 25.50 tex 7"))
        ram = ram_with({0x62F02C: 40})
        self.assertEqual(pcsx2_capture.labels(ram, SYMBOLS, 40), (0, "frame 0: world 0 time -1.00 tex -1"))

    def test_read_state(self):
        with tempfile.TemporaryDirectory() as d:
            folder = Path(d)
            old = folder / "SLES-50386 (00000000).05.p2s"
            old.write_bytes(b"not a zip")
            os.utime(old, (time.time() - 100, time.time() - 100))
            with self.assertRaises(SystemExit):
                pcsx2_capture.read_state(folder, 5, time.time() - 10, timeout=0.2)
            old.unlink()
            with zipfile.ZipFile(folder / "SLES-50386 (00000000).05.p2s", "w") as z:
                z.writestr("Screenshot.png", b"png")
                z.writestr("eeMemory.bin", b"ram")
            self.assertEqual(pcsx2_capture.read_state(folder, 5, time.time() - 10), (b"png", b"ram"))
            self.assertEqual(list(folder.iterdir()), [])


class FramesTest(unittest.TestCase):
    def test_png(self):
        rgba = bytes((x * 7 + y * 13 + c * 50) & 255 for y in range(6) for x in range(5) for c in range(4))
        w, h, rgb = tool.png_read(png_paeth(5, 6, rgba))
        self.assertEqual((w, h), (5, 6))
        self.assertEqual(rgb, bytes(v for i, v in enumerate(rgba) if i % 4 != 3))
        self.assertEqual(tool.png_read(tool.png_write(5, 6, rgb)), (5, 6, rgb))

    def test_resize(self):
        rgb = bytes(range(2 * 2 * 3))
        self.assertEqual(tool.resize(2, 2, rgb, 4, 1), rgb[:3] * 2 + rgb[3:6] * 2)

    def test_pair(self):
        def frame(shade):
            return tool.png_write(640, 256, bytes([shade]) * (640 * 256 * 3))

        with tempfile.TemporaryDirectory() as d:
            pcsx2, port, out = Path(d) / "pcsx2", Path(d) / "port", Path(d) / "out"
            for p in (pcsx2, port, out):
                p.mkdir()
            # PCSX2's screen: the frame letterboxed into rows 16-463 of 480.
            band = bytes(640 * 16 * 3)
            (pcsx2 / "100.png").write_bytes(tool.png_write(640, 480, band + bytes([80]) * (640 * 448 * 3) + band))
            for v, shade in ((97, 10), (98, 79), (99, 120), (101, 0)):
                (port / f"{v}.png").write_bytes(frame(shade))
            self.assertEqual([(v, o) for v, o, _ in tool.pair(pcsx2, port, out)], [(100, -2)])
            self.assertEqual(tool.png_read((out / "100.png").read_bytes())[:2], (1920, 480))


class CompareTest(unittest.TestCase):
    def test_compare(self):
        pcsx2 = title_capture.labels(["# PCSX2", "frame 18: world 0 time 1.60 tex 0", "frame 1460: world 0 time 866.80 tex 4"])
        port = title_capture.labels(["frame 18: world 0 time 1.60 tex 0", "frame 1460: world 0 time 866.79 tex 4"])
        self.assertEqual(title_capture.compare(port, pcsx2), [])
        port[1460] = (0, 866.79, 3)
        del port[18]
        self.assertEqual(title_capture.compare(port, pcsx2), [
            "vblank 18: no port frame",
            "vblank 1460: port world 0 time 866.79 tex 3, PCSX2 world 0 time 866.80 tex 4",
        ])


if __name__ == "__main__":
    unittest.main()
