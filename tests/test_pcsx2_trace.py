"""pcsx2_trace: PINE's batched reads, the symbol lookup and the trace lines; cut_trace's comparison with PCSX2's
trace (no PCSX2, no game data)."""
import struct
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
sys.path.insert(0, str(REPO / "tests"))
import cut_trace  # noqa: E402
import pcsx2_trace  # noqa: E402


class FakeSocket:
    def __init__(self, reply: bytes):
        self.sent = b""
        self.reply = reply

    def sendall(self, data: bytes) -> None:
        self.sent += data

    def recv(self, n: int) -> bytes:
        out, self.reply = self.reply[:n], self.reply[n:]
        return out


def reply(body: bytes) -> bytes:
    return struct.pack("<I", len(body) + 4) + body


def values(vblank, hold=0, key=0, movie=0, world=0, on=1, req=0xFFFFFFFF, busy=0, cd=0):
    return {"vblank": vblank, "key": key, "movie": movie, "world": world, "hold": hold, "on": on, "req": req,
            "busy": busy, "cd": cd}


class PineTest(unittest.TestCase):
    def test_batch(self):
        sock = FakeSocket(reply(b"\0" + struct.pack("<I", 1234) + struct.pack("<Q", 0x0102)))
        out = pcsx2_trace.Pine(sock).batch([(pcsx2_trace.READ32, 0x62F02C), (pcsx2_trace.READ64, 0x4BFF00)])
        self.assertEqual(out, [1234, 0x0102])
        self.assertEqual(sock.sent, struct.pack("<I", 14) + b"\x02" + struct.pack("<I", 0x62F02C) + b"\x03" +
                         struct.pack("<I", 0x4BFF00))

    def test_errors(self):
        with self.assertRaises(pcsx2_trace.PineError):  # PCSX2 without a game answers 0xff
            pcsx2_trace.Pine(FakeSocket(reply(b"\xff"))).batch([(pcsx2_trace.READ32, 0)])
        with self.assertRaises(pcsx2_trace.PineError):
            pcsx2_trace.Pine(FakeSocket(reply(b"\0\1\2"))).batch([(pcsx2_trace.READ32, 0)])
        with self.assertRaises(pcsx2_trace.PineError):
            pcsx2_trace.Pine(FakeSocket(b"")).batch([(pcsx2_trace.READ32, 0)])


class TraceTest(unittest.TestCase):
    def test_symbols(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "symbol_addrs.txt"
            path.write_text("// generated\nSOUND_SPUKeyStatus = 0x004bff00;\ncutmovie = 0x00632bdc; // s32\n")
            symbols = pcsx2_trace.load_symbols(path)
        self.assertEqual(pcsx2_trace.address(symbols, "SOUND_SPUKeyStatus"), 0x4BFF00)
        self.assertEqual(pcsx2_trace.address(symbols, "cutmovie"), 0x632BDC)
        self.assertEqual(pcsx2_trace.address(symbols, "D_00633388"), 0x633388)
        with self.assertRaises(SystemExit):
            pcsx2_trace.address(symbols, "cut_on")

    def test_tracer(self):
        t = pcsx2_trace.Tracer(10)
        self.assertEqual(t.feed(values(100)), ([], False))
        self.assertEqual(t.feed(values(101, hold=1, movie=1)), ([], False))  # not the logos movie
        self.assertEqual(t.feed(values(102, hold=0)), ([], False))
        lines, done = t.feed(values(103, hold=1))
        self.assertEqual(lines, ["vblank 0: key 000000 movie 0 world 0 hold 1 on 1 req -1 busy 0 cd 0"])
        self.assertEqual(t.feed(values(104, hold=1)), ([], False))
        lines, _ = t.feed(values(105, hold=1, key=2 << 32, req=4))
        self.assertEqual(lines, ["vblank 2: key 000020 movie 0 world 0 hold 1 on 1 req 4 busy 0 cd 0"])
        done = [t.feed(values(v, hold=1, key=2 << 32, req=4))[1] for v in range(106, 114)]
        self.assertEqual(done, [False] * 7 + [True])


class CompareTest(unittest.TestCase):
    PCSX2 = [
        "vblank 0: key 000000 movie 0 world 0 hold 1 on 1 req -1 busy 0 cd 0",
        "vblank 18: key 000010 movie 0 world 0 hold 0 on 1 req -1 busy 1 cd 0",
        "vblank 1618: key 000010 movie 0 world 1 hold 0 on 1 req -1 busy 0 cd 0",
        "vblank 1636: key 100010 movie 0 world 1 hold 0 on 1 req -1 busy 1 cd 1",
        "vblank 1651: key 110010 movie 0 world 1 hold 0 on 1 req -1 busy 1 cd 4",
        "vblank 1830: key 110000 movie 0 world 1 hold 0 on 1 req -1 busy 1 cd 1",
    ]

    def test_music_either_order(self):
        port = self.PCSX2[:3] + [
            "vblank 1636: key 010010 movie 0 world 1 hold 0 on 1 req -1 busy 1 cd 4",
            "vblank 1652: key 110010 movie 0 world 1 hold 0 on 1 req -1 busy 1 cd 1",
            "vblank 1830: key 110000 movie 0 world 1 hold 0 on 1 req -1 busy 1 cd 4",
        ]
        self.assertEqual(cut_trace.compare(port, self.PCSX2), [])

    def test_differences(self):
        port = list(self.PCSX2)
        port[1] = port[1].replace("vblank 18", "vblank 19")
        port[4] = port[4].replace("vblank 1651", "vblank 1653")
        port[5] = port[5].replace("vblank 1830", "vblank 1831")
        self.assertEqual(cut_trace.compare(port, self.PCSX2), [
            "the cut up to logos2 differs",
            "logos stream ends at 1831, PCSX2 1830",
            "music keyed on at [1636, 1653], PCSX2 [1636, 1651]",
        ])


if __name__ == "__main__":
    unittest.main()
