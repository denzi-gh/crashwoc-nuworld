#!/usr/bin/env python3
"""Rigid triangle-list DMA as NuCvtDmaTriCFromIdx writes it (UVs on, no compressed points). A corner's w word is
the normal's x with the ADC flag in its low half.

    python tools/nuvif.py <file.ghg>...   re-encode every rigid triangle-list geom
"""
from __future__ import annotations

import math
import struct
import sys
from pathlib import Path
from typing import NamedTuple

BATCH = 42
TAG_CNT = 0x10000000
TAG_RET = 0x60000000
TAG_FLAG = 5  # NuCvtDmaTriCFromIdx's p[-14]
FAN = struct.pack("<4I", 0x00008000, 0x3002C000, 0x512, 0)
STRIP = (0x00008000, 0x30024000, 0x512, 0)
VIF_FAN = 0x6C010056
VIF_STRIP = 0x6C0180D2
VIF_HEADER = 0x6D018002
STCYCL_3 = 0x01000103
STCYCL_1 = 0x01000101
STMOD_ROW = 0x05000001
STMOD_OFF = 0x05000000
VIF_POS = 0x6C008003
VIF_TEX = 0x6D008004
VIF_COL = 0x6E00C005
MSCAL_TRI = 0x14000300


class Corner(NamedTuple):
    pos: tuple[float, float, float]
    w: int
    tex: tuple[int, int, int, int]
    col: tuple[int, int, int, int]


def _f32(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def _short(x: float) -> int:
    """C's (short) of a float."""
    return ((int(x) + 0x8000) & 0xFFFF) - 0x8000


def corners(tris, colour) -> list[Corner]:
    """tris: [(p0, p1, p2), normal, (uv0, uv1, uv2)]; colour(tri, k) -> (r, g, b, a)."""
    out = []
    for pts, nrm, uvs in tris:
        minu = math.floor(min(u for u, _ in uvs))
        minv = math.floor(min(v for _, v in uvs))
        for k in range(3):
            flag = 0x8001 if k < 2 else 0
            out.append(Corner(tuple(pts[k]), (_f32(nrm[0]) & 0xFFFF0000) | flag,
                              (_short((uvs[k][0] - minu) * 4096.0), _short((uvs[k][1] - minv) * 4096.0),
                               _short(nrm[1] * 4096.0), _short(nrm[2] * 4096.0)), colour(len(out) // 3, k)))
    return out


def packet(cs: list[Corner]) -> bytes:
    p = bytearray(struct.pack("<I", VIF_FAN) + FAN)
    for at in range(0, len(cs), BATCH):
        batch = cs[at:at + BATCH]
        n = len(batch)
        p += struct.pack("<I4I", VIF_STRIP, STRIP[0] | n, *STRIP[1:])
        p += struct.pack("<I4H", VIF_HEADER, n * 3, 0, 0, 0)
        p += struct.pack("<II", STCYCL_3, VIF_POS | n << 16)
        for c in batch:
            p += struct.pack("<3fI", *c.pos, c.w)
        p += struct.pack("<II", STMOD_ROW, VIF_TEX | n << 16)
        for c in batch:
            p += struct.pack("<4h", *c.tex)
        p += struct.pack("<II", STMOD_OFF, VIF_COL | n << 16)
        for c in batch:
            p += bytes(c.col)
        p += struct.pack("<II", STCYCL_1, MSCAL_TRI)
    p += bytes(-len(p) % 16)
    qwc = len(p) // 16
    return struct.pack("<HBBIII", qwc, TAG_FLAG, TAG_CNT >> 24, 0, 0, 0) + bytes(p)


def chain(packets: list[list[Corner]]) -> bytes:
    return b"".join(packet(cs) for cs in packets) + struct.pack("<4I", TAG_RET, 0, 0, 0)


def decode(dma: bytes) -> list[list[Corner]] | None:
    """None for any other format."""
    out, off = [], 0
    while off + 16 <= len(dma):
        lo, addr, v0, v1 = struct.unpack_from("<4I", dma, off)
        if lo == TAG_RET and not (addr or v0 or v1):
            return out if off + 16 == len(dma) or not any(dma[off + 16:]) else None
        if lo >> 16 != (TAG_CNT >> 16) | TAG_FLAG or addr or v0 or v1:
            return None
        end = off + 16 + (lo & 0xFFFF) * 16
        s, p, cs = dma[off + 16:end], 0, []
        if s[:20] != struct.pack("<I", VIF_FAN) + FAN:
            return None
        p = 20
        while p + 4 <= len(s) and struct.unpack_from("<I", s, p)[0] == VIF_STRIP:
            n = struct.unpack_from("<I", s, p + 4)[0] & 0x7FFF
            batch_pos = p + 4 + 16 + 4 + 8 + 8
            pos = [struct.unpack_from("<3fI", s, batch_pos + 16 * i) for i in range(n)]
            q = batch_pos + 16 * n + 8
            tex = [struct.unpack_from("<4h", s, q + 8 * i) for i in range(n)]
            q += 8 * n + 8
            col = [tuple(s[q + 4 * i:q + 4 * i + 4]) for i in range(n)]
            q += 4 * n + 8
            cs += [Corner(tuple(pos[i][:3]), pos[i][3], tex[i], col[i]) for i in range(n)]
            p = q
        if any(s[p:]):
            return None
        out.append(cs)
        off = end
    return None


def main(argv: list[str]) -> int:
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import nughg

    seen = same = 0
    for name in argv:
        g = nughg.read(Path(name).read_bytes())
        for layer in g.layers:
            for chain_ in layer.gobjs or []:
                for gobj in chain_:
                    for geom in gobj.geoms:
                        packets = decode(geom.dma) if geom.dma else None
                        if packets is None:
                            continue
                        seen += 1
                        dma = chain(packets)
                        same += geom.dma[:len(dma)] == dma and not any(geom.dma[len(dma):])
    print("%d of %d triangle-list geoms re-encode byte-exact" % (same, seen))
    return 0 if same == seen else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
