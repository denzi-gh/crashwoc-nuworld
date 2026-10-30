#!/usr/bin/env python3
"""Save PCSX2's EE RAM at chosen vblanks of the running game for tools/ps2_diff.py, poking it at others.

Vblanks count from the title cut's start (pokes always, states unless --after). Prints the pokes as they landed (for
the port's --poke) and the vblanks the states hold (for its --dump); a state can land later than asked.
"""
from __future__ import annotations

import argparse
import re
import struct
import sys
import time
from pathlib import Path

from pcsx2_capture import SLOTS, SSTATES, read_state, slot_states, wait_sync
from pcsx2_trace import READ32, SYMBOLS, Pine, PineError, address, load_symbols

WRITE32 = 0x06
POKE = re.compile(r"^(-?\d+):(0x[0-9a-fA-F]+|[A-Za-z_]\w*)(?:\+(0x[0-9a-fA-F]+|\d+))?=(-?(?:0x[0-9a-fA-F]+|\d+))$")


def parse_pokes(text: str, symbols: dict[str, int]) -> list[tuple[int, str, int, int]]:
    """(vblank, text after the vblank, address, value) per comma-separated item."""
    out = []
    for item in filter(None, (s.strip() for s in text.split(","))):
        m = POKE.match(item)
        if not m:
            raise SystemExit(f"--poke: cannot read {item!r} (vblank:symbol[+offset]=value)")
        name = m.group(2)
        addr = int(name, 16) if name.startswith("0x") else address(symbols, name)
        addr += int(m.group(3), 0) if m.group(3) else 0
        out.append((int(m.group(1)), item.split(":", 1)[1], addr, int(m.group(4), 0) & 0xFFFFFFFF))
    return sorted(out)


def write32(pine: Pine, addr: int, value: int) -> None:
    try:
        pine.sock.sendall(struct.pack("<IBII", 13, WRITE32, addr, value))
        total = struct.unpack("<I", pine._recv(4))[0]
        body = pine._recv(total - 4)
    except OSError as exc:
        raise PineError(f"PINE: {exc}") from exc
    if not body or body[0] != 0:
        raise PineError(f"PINE: write to 0x{addr:08x} refused")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--out", type=Path, required=True, help="folder for the RAM files (<vblank>.ee)")
    parser.add_argument("--vblanks", required=True, help="comma-separated trace vblanks")
    parser.add_argument("--poke", default="", help="comma-separated vblank:symbol[+offset]=value writes")
    parser.add_argument("--after", help="symbol=value: the state vblanks count from the first vblank it holds on")
    parser.add_argument("--sstates", type=Path, default=SSTATES, help=f"PCSX2's state folder (default {SSTATES})")
    parser.add_argument("--slot", type=int, default=28011, help="PINE slot (default 28011)")
    parser.add_argument("--slots", default=f"{SLOTS.start}-{SLOTS.stop - 1}",
                        help=f"state slots to use in turn (default {SLOTS.start}-{SLOTS.stop - 1})")
    parser.add_argument("--wait", type=float, default=600, help="seconds to wait for the cut (default 600)")
    parser.add_argument("--symbols", type=Path, default=SYMBOLS)
    args = parser.parse_args()

    if sys.version_info < (3, 14):
        raise SystemExit("needs Python 3.14 for the states' zstd entries")
    first, _, last = args.slots.partition("-")
    slots = range(int(first), int(last or first) + 1)
    taken = [p.name for s in slots for p in slot_states(args.sstates, s)]
    if taken:
        raise SystemExit(f"{args.sstates} already holds states in slots {args.slots}: {taken}")
    symbols = load_symbols(args.symbols)
    vblanks = sorted({int(v) for v in args.vblanks.split(",") if v.strip()})
    pokes = parse_pokes(args.poke, symbols)
    after = parse_pokes(f"0:{args.after}", symbols)[0][2:] if args.after else None
    args.out.mkdir(parents=True, exist_ok=True)
    vbcnt = address(symbols, "nuvideo_global_vbcnt")
    pine = Pine.connect(args.slot)
    base = wait_sync(pine, symbols, args.wait)

    def now() -> int:
        return pine.batch([(READ32, vbcnt)])[0] - base

    pending: dict[int, float] = {}
    saved: list[int] = []
    zero = None if after else 0  # the vblank the states count from

    def collect(slot: int) -> None:
        _, ram = read_state(args.sstates, slot, pending.pop(slot))
        vblank = int.from_bytes(ram[vbcnt & 0x1FFFFFF:(vbcnt & 0x1FFFFFF) + 4], "little") - base - zero
        (args.out / f"{vblank}.ee").write_bytes(ram)
        saved.append(vblank)

    states = 0
    while vblanks:
        v = now()
        while pokes and pokes[0][0] <= v:
            _, what, addr, value = pokes.pop(0)
            write32(pine, addr, value)
            print(f"poke {now()}:{what}", flush=True)
        if zero is None and pine.batch([(READ32, after[0])])[0] == after[1]:
            zero = v
        if zero is not None and vblanks[0] + zero <= v:
            vblanks.pop(0)
            slot = slots[states % len(slots)]
            states += 1
            if slot in pending:
                collect(slot)
            pending[slot] = time.time() - 1.0  # mtime resolution
            pine.save_state(slot)
        time.sleep(0.001)
    for slot in list(pending):
        collect(slot)
    for vblank in sorted(saved):
        print(f"vblank {vblank}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except PineError as exc:
        sys.exit(str(exc))
