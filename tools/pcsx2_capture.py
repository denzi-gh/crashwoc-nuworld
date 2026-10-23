#!/usr/bin/env python3
"""Capture frames of the title cut from a running PCSX2, for a side by side with crashwoc-nuworld --capture.

Waits over PINE for the trace's start (tools/pcsx2_trace.py: the vblank on which
the logos cut first holds for its audio), then saves a state at each requested
vblank (PINE MsgSaveState, slots 4-10 in turn) and takes from it the screen
(Screenshot.png, written to <out>/<vblank>.png) and the labels the port prints
for its frames: the vblank the state holds, cutworldix, the time of that cut and
the title logo's texanim frame. The label lines on stdout are the fixture
tests/fixtures/pcsx2_title_frames.txt; the PNGs are game imagery and stay local.
A state is saved after its request, so a label's vblank can be later than asked.

  python tools/pcsx2_capture.py --out <data root>/captures/pcsx2 > frames.txt

Needs Python 3.14 (the states' zstd entries). Refuses to start while PCSX2's
state folder holds a state in one of the slots it uses.
"""
from __future__ import annotations

import argparse
import io
import struct
import sys
import time
import zipfile
from pathlib import Path

from pcsx2_trace import READ32, READ64, SYMBOLS, Pine, PineError, TexWatch, address, load_symbols

SLOTS = range(4, 11)
SSTATES = Path.home() / "Documents" / "PCSX2" / "sstates"
CUT_TIME = 0x70  # instNUGCUTSCENE_s.time
VBLANKS = sorted({0, 18, 40, *range(100, 1401, 100), *range(1440, 1621, 10), *range(1618, 1623), 1700, 1785,
                  1830, 1900, 1951})


class EeMemory:
    """Pine.batch over a savestate's eeMemory.bin, for TexWatch."""

    def __init__(self, ram: bytes):
        self.ram = ram

    def read(self, addr: int, size: int) -> int:
        at = addr & 0x1FFFFFF
        return int.from_bytes(self.ram[at:at + size], "little")

    def batch(self, ops: list[tuple[int, int]]) -> list[int]:
        return [self.read(addr, 8 if op == READ64 else 4) for op, addr in ops]


def labels(ram: bytes, symbols: dict[str, int], base: int) -> tuple[int, str]:
    """The trace vblank the state holds and its label line."""
    mem = EeMemory(ram)
    vblank = mem.read(address(symbols, "nuvideo_global_vbcnt"), 4) - base
    world = mem.read(address(symbols, "D_00633388"), 4)
    cut = mem.read(address(symbols, "D_006FF700") + 4 * world, 4)
    when = struct.unpack("<f", struct.pack("<I", mem.read(cut + CUT_TIME, 4)))[0] if cut else -1.0
    tex = TexWatch(mem)
    tex.update(mem.read(address(symbols, "ntal_first"), 4))
    frame = mem.read(tex.addr, 4) if tex.addr is not None else -1
    frame = frame - (1 << 32) if frame >= 1 << 31 else frame
    return vblank, f"frame {vblank}: world {world} time {when:.2f} tex {frame}"


def slot_states(folder: Path, slot: int) -> list[Path]:
    return sorted(folder.glob(f"*.{slot:02d}.p2s"))


def read_state(folder: Path, slot: int, asked: float, timeout: float = 30.0) -> tuple[bytes, bytes]:
    """Waits for the state written to slot after `asked`; returns its screen PNG and EE RAM and deletes it."""
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        for path in slot_states(folder, slot):
            try:
                if path.stat().st_mtime < asked:
                    continue
                with zipfile.ZipFile(path) as z:
                    png, ram = z.read("Screenshot.png"), z.read("eeMemory.bin")
            except (OSError, zipfile.BadZipFile, KeyError, EOFError):
                continue
            path.unlink()
            return png, ram
        time.sleep(0.05)
    raise SystemExit(f"no state in slot {slot} within {timeout:.0f} s")


def wait_sync(pine: Pine, symbols: dict[str, int], wait: float) -> int:
    """The vblank count on which the logos cut first holds (cutmovie_hold 0 -> 1, cutmovie 0)."""
    ops = [(READ32, address(symbols, n)) for n in ("nuvideo_global_vbcnt", "cutmovie_hold", "cutmovie")]
    last_hold, start = None, time.monotonic()
    while time.monotonic() - start < wait:
        try:
            vbcnt, hold, movie = pine.batch(ops)
        except PineError:  # no VM while PCSX2 boots
            time.sleep(0.1)
            continue
        if hold == 1 and last_hold == 0 and movie == 0:
            return vbcnt
        last_hold = hold
        time.sleep(0.001)
    raise SystemExit(f"no cut movie start within {wait:.0f} s")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True, help="folder for the screens (<vblank>.png)")
    parser.add_argument("--vblanks", help="comma-separated trace vblanks (default: the title cut's set)")
    parser.add_argument("--sstates", type=Path, default=SSTATES, help=f"PCSX2's state folder (default {SSTATES})")
    parser.add_argument("--slot", type=int, default=28011, help="PINE slot (default 28011)")
    parser.add_argument("--wait", type=float, default=600, help="seconds to wait for the cut (default 600)")
    parser.add_argument("--symbols", type=Path, default=SYMBOLS)
    args = parser.parse_args()

    if sys.version_info < (3, 14):
        raise SystemExit("needs Python 3.14 for the states' zstd entries")
    taken = [p.name for s in SLOTS for p in slot_states(args.sstates, s)]
    if taken:
        raise SystemExit(f"{args.sstates} already holds states in slots {SLOTS.start}-{SLOTS.stop - 1}: {taken}")
    vblanks = sorted({int(v) for v in args.vblanks.split(",")}) if args.vblanks else VBLANKS
    symbols = load_symbols(args.symbols)
    args.out.mkdir(parents=True, exist_ok=True)
    vbcnt_op = [(READ32, address(symbols, "nuvideo_global_vbcnt"))]
    pine = Pine.connect(args.slot)
    base = wait_sync(pine, symbols, args.wait)

    pending: dict[int, float] = {}  # slot -> when its state was asked for
    lines: dict[int, str] = {}

    def collect(slot: int) -> None:
        png, ram = read_state(args.sstates, slot, pending.pop(slot))
        vblank, line = labels(ram, symbols, base)
        (args.out / f"{vblank}.png").write_bytes(png)
        lines[vblank] = line

    for i, target in enumerate(vblanks):
        slot = SLOTS[i % len(SLOTS)]
        if slot in pending:
            collect(slot)
        while pine.batch(vbcnt_op)[0] - base < target:
            time.sleep(0.001)
        pending[slot] = time.time() - 1.0  # mtime resolution
        pine.save_state(slot)
    for slot in list(pending):
        collect(slot)
    print("# The title cut on PCSX2 2.8.2 (PAL v1.03, PINE savestates): tools/pcsx2_capture.py")
    for vblank in sorted(lines):
        print(lines[vblank])
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except PineError as exc:
        sys.exit(str(exc))
