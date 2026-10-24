#!/usr/bin/env python3
"""Pair the port's title cut frames (crashwoc-nuworld --capture) with PCSX2's (tools/pcsx2_capture.py).

Each PCSX2 screen <vblank>.png gets the port frame among vblank-3 .. vblank+1
that differs least from it (mean absolute difference of grey 160x120 thumbnails,
both shown 4:3; PCSX2 letterboxes the frame into 448 of its 480 rows, which are
cut out first) as its pair; the offset is the vblanks between the frame the EE
built and the one PCSX2 showed, and should be the same for every pair. Writes
<out>/<vblank>.png (PCSX2, the port, their difference x4, side by side) and prints
one line per pair. Standard library only.

  python tools/title_frames.py --pcsx2 <dir> --port <dir> --out <dir>
"""
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from collections import Counter
from pathlib import Path

WIDTH, HEIGHT = 640, 480  # both shown 4:3
PCSX2_ROWS = (16, 464)    # the frame in PCSX2's 640x480 screen
BEFORE, AFTER = 3, 1      # port/capture.h


def png_read(data: bytes) -> tuple[int, int, bytes]:
    """8-bit RGB or RGBA, not interlaced; returns width, height and the RGB bytes."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    at, idat, header = 8, [], None
    while at < len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", data[at + 8:at + 21])
        elif kind == b"IDAT":
            idat.append(data[at + 8:at + 8 + n])
        at += 12 + n
    if not header or header[2] != 8 or header[3] not in (2, 6) or header[6]:
        raise ValueError(f"unsupported PNG {header}")
    width, height, ctype = header[0], header[1], header[3]
    bpp = 3 if ctype == 2 else 4
    raw, stride = zlib.decompress(b"".join(idat)), width * bpp
    out, prev = bytearray(), bytearray(stride)
    for y in range(height):
        kind, line = raw[y * (stride + 1)], bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if kind == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif kind == 2:
            line = bytearray((a + b) & 255 for a, b in zip(line, prev))
        elif kind == 3:
            for i in range(stride):
                line[i] = (line[i] + ((line[i - bpp] if i >= bpp else 0) + prev[i]) // 2) & 255
        elif kind == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b, c = prev[i], prev[i - bpp] if i >= bpp else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line
        prev = line
    if bpp == 4:
        del out[3::4]
    return width, height, bytes(out)


def png_write(width: int, height: int, rgb: bytes) -> bytes:
    def chunk(kind: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body))

    rows = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(rows, 6)) + chunk(b"IEND", b""))


def resize(width: int, height: int, rgb: bytes, to_w: int, to_h: int) -> bytes:
    """Nearest neighbour."""
    cols = [x * width // to_w * 3 for x in range(to_w)]
    out = bytearray()
    for y in range(to_h):
        row = rgb[y * height // to_h * width * 3:]
        out += row[:width * 3] if width == to_w else bytes(b for c in cols for b in row[c:c + 3])
    return bytes(out)


def shown(path: Path, rows: tuple[int, int] | None = None) -> bytes:
    width, height, rgb = png_read(path.read_bytes())
    if rows:
        rgb, height = rgb[rows[0] * width * 3:rows[1] * width * 3], rows[1] - rows[0]
    return resize(width, height, rgb, WIDTH, HEIGHT)


def thumb(rgb: bytes) -> list[int]:
    return [(rgb[i] * 3 + rgb[i + 1] * 6 + rgb[i + 2]) // 10
            for y in range(0, HEIGHT, 4) for i in range(y * WIDTH * 3, (y + 1) * WIDTH * 3, 12)]


def difference(a: list[int], b: list[int]) -> float:
    return sum(abs(x - y) for x, y in zip(a, b)) / len(a)


def side_by_side(*images: bytes) -> bytes:
    rows = []
    for y in range(HEIGHT):
        rows.append(b"".join(img[y * WIDTH * 3:(y + 1) * WIDTH * 3] for img in images))
    return png_write(WIDTH * len(images), HEIGHT, b"".join(rows))


def pair(pcsx2: Path, port: Path, out: Path | None) -> list[tuple[int, int, float]]:
    """(vblank, offset, difference) per PCSX2 frame with port frames around it."""
    found = []
    for path in sorted(pcsx2.glob("*.png"), key=lambda p: int(p.stem)):
        vblank = int(path.stem)
        ref = shown(path, PCSX2_ROWS)
        ref_thumb, best = thumb(ref), None
        for offset in range(-BEFORE, AFTER + 1):
            candidate = port / f"{vblank + offset}.png"
            if candidate.exists():
                img = shown(candidate)
                d = difference(ref_thumb, thumb(img))
                if best is None or d < best[1]:
                    best = (offset, d, img)
        if best is None:
            continue
        found.append((vblank, best[0], best[1]))
        if out:
            diff = bytes(min(255, abs(a - b) * 4) for a, b in zip(ref, best[2]))
            (out / f"{vblank}.png").write_bytes(side_by_side(ref, best[2], diff))
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--pcsx2", type=Path, required=True)
    parser.add_argument("--port", type=Path, required=True)
    parser.add_argument("--out", type=Path, help="folder for the side-by-side PNGs")
    args = parser.parse_args()
    if args.out:
        args.out.mkdir(parents=True, exist_ok=True)
    found = pair(args.pcsx2, args.port, args.out)
    if not found:
        print("no frame pairs", file=sys.stderr)
        return 1
    for vblank, offset, d in found:
        print(f"vblank {vblank}: port {vblank + offset} ({offset:+d}), difference {d:.1f}")
    offsets = Counter(offset for _, offset, _ in found)
    print(f"{len(found)} pairs, offsets {dict(sorted(offsets.items()))}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
