#!/usr/bin/env python3
"""Trace the sound key status and the title cut of a running PCSX2, per vblank.

Polls FIELDS over PINE (Settings > Advanced > Enable PINE) faster than the vblank and prints a line per change, counted
from StartCutMovie (cutmovie_hold 0 -> 1); `crashwoc-nuworld --trace-sound` prints the same lines, for a diff.
"""
from __future__ import annotations

import argparse
import re
import socket
import struct
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SYMBOLS = ROOT / "decomp" / "config" / "pal103" / "symbol_addrs.txt"

READ8, READ32, READ64, SAVE_STATE, STATUS = 0x00, 0x02, 0x03, 0x09, 0x0F
SIZES = {READ8: 1, READ32: 4, READ64: 8, STATUS: 4}
CHANNELS = 6

# (field, symbol, opcode); cutworldix has no name in the registry.
FIELDS = (
    ("vblank", "nuvideo_global_vbcnt", READ32),
    ("key", "SOUND_SPUKeyStatus", READ64),
    ("movie", "cutmovie", READ32),
    ("world", "D_00633388", READ32),
    ("hold", "cutmovie_hold", READ32),
    ("on", "cut_on", READ32),
    ("req", "SOUND_IOP_REQUEST_LOAD", READ32),
    ("busy", "SOUND_CD_STATUS", READ32),
    ("cd", "SOUND_STREAM_CD_ACCESS", READ32),
    ("ntal", "ntal_first", READ32),
)

# nutexanm.c: NuTexAnimList (anims, next), nutexanim_s (succ, env), nutexanimenv_s (prog, tex_ix), the program's name.
LIST_NEXT, ANIM_ENV, ENV_TEX, PROG_NAME = 0x04, 0x14, 0xE4, 0x08


class PineError(RuntimeError):
    pass


class Pine:
    """PINE's batched form: one message of several commands, one reply with their results in order."""

    def __init__(self, sock: socket.socket):
        self.sock = sock

    @classmethod
    def connect(cls, slot: int, timeout: float = 2.0) -> "Pine":
        try:
            sock = socket.create_connection(("127.0.0.1", slot), timeout=timeout)
        except OSError as exc:
            raise PineError(f"no PCSX2 on PINE slot {slot}: {exc}") from exc
        return cls(sock)

    def batch(self, ops: list[tuple[int, int]]) -> list[int]:
        payload = b"".join(bytes([op]) + (struct.pack("<I", addr) if op != STATUS else b"") for op, addr in ops)
        try:
            self.sock.sendall(struct.pack("<I", len(payload) + 4) + payload)
            total = struct.unpack("<I", self._recv(4))[0]
            body = self._recv(total - 4)
        except OSError as exc:
            raise PineError(f"PINE: {exc}") from exc
        if not body or body[0] != 0:
            raise PineError(f"PINE: error reply {body[:1].hex()}")
        out, at = [], 1
        for op, _ in ops:
            n = SIZES[op]
            if at + n > len(body):
                raise PineError("PINE: short reply")
            out.append(int.from_bytes(body[at:at + n], "little"))
            at += n
        return out

    def save_state(self, slot: int) -> None:
        """Asks PCSX2 to save a state to slot; it is written asynchronously."""
        try:
            self.sock.sendall(struct.pack("<I", 6) + bytes([SAVE_STATE, slot]))
            total = struct.unpack("<I", self._recv(4))[0]
            body = self._recv(total - 4)
        except OSError as exc:
            raise PineError(f"PINE: {exc}") from exc
        if not body or body[0] != 0:
            raise PineError(f"PINE: save state to slot {slot} refused")

    def _recv(self, n: int) -> bytes:
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise PineError("PINE: PCSX2 closed the connection")
            buf += chunk
        return buf


def load_symbols(path: Path) -> dict[str, int]:
    out = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^(\w+)\s*=\s*0x([0-9a-fA-F]+);", line)
        if m:
            out[m.group(1)] = int(m.group(2), 16)
    return out


def address(symbols: dict[str, int], name: str) -> int:
    m = re.fullmatch(r"D_([0-9A-Fa-f]{8})", name)
    if m:
        return int(m.group(1), 16)
    if name not in symbols:
        raise SystemExit(f"{name}: not in {SYMBOLS}")
    return symbols[name]


def signed32(v: int) -> int:
    return v - (1 << 32) if v >= 1 << 31 else v


def state_line(vblank: int, values: dict[str, int]) -> str:
    keys = "".join(str(values["key"] >> (8 * c) & 0xff) for c in range(CHANNELS))
    return (f"vblank {vblank}: key {keys} movie {values['movie']} world {values['world']} "
            f"hold {values['hold']} on {values['on']} req {signed32(values['req'])} busy {values['busy']} "
            f"cd {values['cd']} tex {values['tex']}")


class TexWatch:
    """Address of the title flipbook's tex_ix (the env running "title"), re-found when the texanim list head moves."""

    def __init__(self, pine: Pine):
        self.pine = pine
        self.head = None
        self.addr = None

    def _read32(self, addr: int) -> int:
        return self.pine.batch([(READ32, addr)])[0]

    def _name(self, addr: int) -> str:
        raw = b"".join(struct.pack("<Q", v) for v in self.pine.batch([(READ64, addr + 8 * i) for i in range(4)]))
        return raw.split(b"\0", 1)[0].decode("latin-1").lower()

    def update(self, head: int) -> None:
        if head == self.head:
            return
        self.head, self.addr = head, None
        lists = 0
        while head and lists < 64 and self.addr is None:
            anim, anims = self._read32(head), 0
            while anim and anims < 256 and self.addr is None:
                env = self._read32(anim + ANIM_ENV)
                prog = self._read32(env) if env else 0
                if prog and self._name(prog + PROG_NAME) == "title":
                    self.addr = env + ENV_TEX
                anim, anims = self._read32(anim), anims + 1
            head, lists = self._read32(head + LIST_NEXT), lists + 1


class Tracer:
    """Turns polls into lines: nothing before the sync, then one line per change of state."""

    def __init__(self, vblanks: int):
        self.vblanks = vblanks
        self.base = None
        self.last_vblank = None
        self.last_hold = 0
        self.last_line = None

    def feed(self, values: dict[str, int]) -> tuple[list[str], bool]:
        vbcnt, lines = values["vblank"], []
        if self.last_vblank is not None and vbcnt - self.last_vblank > 1 and self.base is not None:
            print(f"# missed vblanks {self.last_vblank - self.base + 1}-{vbcnt - self.base - 1}", file=sys.stderr)
        self.last_vblank = vbcnt
        if self.base is None:
            if values["hold"] == 1 and self.last_hold == 0 and values["movie"] == 0:
                self.base = vbcnt
            self.last_hold = values["hold"]
            if self.base is None:
                return lines, False
        line = state_line(vbcnt - self.base, values)
        state = line.split(":", 1)[1]
        if self.last_line != state:
            lines.append(line)
            self.last_line = state
        return lines, vbcnt - self.base >= self.vblanks


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--slot", type=int, default=28011, help="PINE slot (default 28011)")
    parser.add_argument("--vblanks", type=int, default=2500, help="vblanks to trace after the sync")
    parser.add_argument("--wait", type=float, default=600, help="seconds to wait for the cut (default 600)")
    parser.add_argument("--symbols", type=Path, default=SYMBOLS)
    args = parser.parse_args()

    symbols = load_symbols(args.symbols)
    ops = [(op, address(symbols, name)) for _, name, op in FIELDS]
    pine = Pine.connect(args.slot)
    tracer = Tracer(args.vblanks)
    tex = TexWatch(pine)
    start = time.monotonic()
    while True:
        addr = tex.addr
        out = pine.batch(ops + ([(READ32, addr)] if addr is not None else []))
        values = dict(zip((f for f, _, _ in FIELDS), out))
        values["tex"] = signed32(out[-1]) if addr is not None else -1
        tex.update(values["ntal"])
        lines, done = tracer.feed(values)
        for line in lines:
            print(line, flush=True)
        if done:
            return 0
        if tracer.base is None and time.monotonic() - start > args.wait:
            print(f"no cut movie start within {args.wait:.0f} s", file=sys.stderr)
            return 1
        time.sleep(0.001)


if __name__ == "__main__":
    sys.exit(main())
