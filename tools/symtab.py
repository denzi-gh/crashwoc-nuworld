#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import struct
import sys
from pathlib import Path

ROW = re.compile(r"^ [0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16}) f ")


def image(exe: bytes) -> tuple[int, int, int]:
    if exe[:2] != b"MZ" or len(exe) < 0x40:
        raise ValueError("not a PE image")
    pe = struct.unpack_from("<I", exe, 0x3C)[0]
    if exe[pe:pe + 4] != b"PE\0\0" or struct.unpack_from("<H", exe, pe + 24)[0] != 0x20B:
        raise ValueError("not a PE32+ image")
    stamp = struct.unpack_from("<I", exe, pe + 8)[0]
    base = struct.unpack_from("<Q", exe, pe + 24 + 24)[0]
    size = struct.unpack_from("<I", exe, pe + 24 + 56)[0]
    return stamp, base, size


def symbols(text: str, base: int, size: int) -> list[tuple[int, str]]:
    out: dict[int, str] = {}
    for line in text.splitlines():
        m = ROW.match(line)
        if m and base <= int(m.group(2), 16) < base + size:
            out.setdefault(int(m.group(2), 16) - base, m.group(1))
    return sorted(out.items())


def render(stamp: int, size: int, syms: list[tuple[int, str]]) -> str:
    return f"nwsym {stamp:08x} {size:08x}\n" + "".join(f"{rva:x} {name}\n" for rva, name in syms)


def main() -> int:
    parser = argparse.ArgumentParser(description="The function names of a linker map as <exe>.sym")
    parser.add_argument("map", type=Path)
    parser.add_argument("exe", type=Path)
    parser.add_argument("out", type=Path)
    args = parser.parse_args()
    try:
        stamp, base, size = image(args.exe.read_bytes())
        syms = symbols(args.map.read_text(encoding="latin-1"), base, size)
    except (OSError, ValueError, struct.error) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    if not syms:
        print(f"error: {args.map} lists no functions", file=sys.stderr)
        return 1
    args.out.write_bytes(render(stamp, size, syms).encode("ascii", "replace"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
