#!/usr/bin/env python3
"""The NuWorld mark as a .ghg for the title menu: one unlit slab joint per SVG shard under the title's 'Character'.
Title space: x right, y towards the camera, z down in the cut's curves, up in the file.

    python tools/title_logo.py assets/branding/nuworld-icon.svg <out.ghg>
"""
from __future__ import annotations

import math
import re
import struct
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nughg  # noqa: E402
import nuvif  # noqa: E402

SIZE = 3.6               # the ring is about 2.4 wide
CENTRE = (-0.23, 0.2)   # x, z (down)
DEPTH = -0.32            # behind the ring and the letters
THICK = 0.12
SIDE = 0.58              # shades of the front colour
BACK = 0.38
NAMES = ("earth", "water", "fire", "air", "space")

# numtl_s offsets
MTL_RS_ATTR, MTL_ATTRIB, MTL_DIFFUSE, MTL_POWER, MTL_ALPHA, MTL_K = 0x154, 0x16C, 0x180, 0x19C, 0x1A0, 0x1AC
# numtlattrib_s: filter 1, lighting 2 (unlit), colour 1 (vertex), atst 5, aref 126, afail 2, uvmode 1, date 1
ATTRIB = (1 << 4) | (2 << 16) | (1 << 18) | (5 << 20) | (126 << 23) | (2 << 31) | (1 << 33) | (1 << 36)


def shards(svg: Path) -> list[tuple[str, tuple[int, int, int], list[tuple[float, float]]]]:
    """(name, colour, points) of every '<element>-<layer>' path."""
    out = []
    for el in ET.parse(svg).iter():
        pid = el.get("id", "")
        if not el.tag.endswith("path") or pid.split("-")[0] not in NAMES:
            continue
        fill = el.get("fill").lstrip("#")
        nums = [float(x) for x in re.findall(r"-?\d+(?:\.\d+)?", el.get("d"))]
        out.append((pid.replace("-", "_"), tuple(int(fill[i:i + 2], 16) for i in (0, 2, 4)),
                    list(zip(nums[0::2], nums[1::2]))))
    if len(out) != 10:
        raise ValueError("%s: %d shards, not 10" % (svg, len(out)))
    return out


def material() -> bytes:
    m = bytearray(nughg.MTL)
    struct.pack_into("<I", m, MTL_RS_ATTR, 0x3C)
    struct.pack_into("<Q", m, MTL_ATTRIB, ATTRIB)
    struct.pack_into("<3f", m, MTL_DIFFUSE, 1.0, 1.0, 1.0)
    struct.pack_into("<ff", m, MTL_POWER, 1.0, 1.0)
    struct.pack_into("<h", m, MTL_K, -90)
    return bytes(m)


def slab(pts: list[tuple[float, float]], colour: tuple[int, int, int]) -> list[nuvif.Corner]:
    half = THICK / 2
    front = [(x, half, z) for x, z in pts]
    back = [(x, -half, z) for x, z in pts]
    tris, shades = [], []

    def quad(a, b, c, d, nrm, shade):
        tris.extend([((a, b, c), nrm, ((0, 0),) * 3), ((a, c, d), nrm, ((0, 0),) * 3)])
        shades.extend([shade, shade])

    quad(*front, (0.0, 1.0, 0.0), 1.0)
    quad(*reversed(back), (0.0, -1.0, 0.0), BACK)
    for i in range(4):
        (x0, z0), (x1, z1) = pts[i], pts[(i + 1) % 4]
        n = math.hypot(z1 - z0, x0 - x1) or 1.0
        quad(back[i], back[(i + 1) % 4], front[(i + 1) % 4], front[i], ((z1 - z0) / n, 0.0, (x0 - x1) / n), SIDE)

    def colour_of(tri, _k):
        return tuple(min(255, round(c * shades[tri])) for c in colour) + (0x80,)

    return nuvif.corners(tris, colour_of)


def bounds(points: list[tuple[float, float, float]]) -> bytes:
    """gobj r, r2, min, max, cntr, cntr_r, cntr_r2"""
    lo = [min(p[i] for p in points) for i in range(3)]
    hi = [max(p[i] for p in points) for i in range(3)]
    c = [(a + b) / 2 for a, b in zip(lo, hi)]
    r = max(math.dist(p, (0, 0, 0)) for p in points)
    cr = max(math.dist(p, c) for p in points)
    return struct.pack("<2f3f3f3f2f", r, r * r, *lo, *hi, *c, cr, cr * cr)


def build(svg: Path) -> bytes:
    parts = shards(svg)
    xs = [x for _, _, pts in parts for x, _ in pts]
    ys = [y for _, _, pts in parts for _, y in pts]
    mid = ((min(xs) + max(xs)) / 2, (min(ys) + max(ys)) / 2)
    scale = SIZE / max(max(xs) - min(xs), max(ys) - min(ys))

    g = nughg.Ghg(materials=[material()])
    names = ["Character"] + [name for name, _, _ in parts]
    strings = b""
    offsets = []
    for name in names:
        offsets.append(len(strings))
        strings += name.encode() + b"\0"
    g.strings = strings
    ident = (1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0, 0, 0, 0, 0, 1.0)

    def joint(j: int, parent: int, flags: int) -> bytes:
        return struct.pack("<16f3fibB14x", *ident, 0, 0, 0, offsets[j], parent, flags)

    def translate(x: float, y: float, z: float) -> bytes:
        return struct.pack("<16f", *ident[:12], x, y, z, 1.0)

    g.joints = [joint(0, -1, 8)]
    g.t, g.inv_wt = translate(0, 0, 0), translate(0, 0, 0)
    chains: list[list[nughg.Gobj]] = [[]]
    every = []
    for j, (_, colour, pts) in enumerate(parts, 1):
        world = [(CENTRE[0] + (x - mid[0]) * scale, CENTRE[1] + (y - mid[1]) * scale) for x, y in pts]
        cx = sum(x for x, _ in world) / 4
        cz = sum(z for _, z in world) / 4
        local = [(x - cx, -(z - cz)) for x, z in world]
        corners = slab(local, colour)
        g.joints.append(joint(j, 0, 1))
        g.t += translate(cx, DEPTH, -cz)
        g.inv_wt += translate(-cx, -DEPTH, cz)
        verts = [c.pos for c in corners]
        head = struct.pack("<5i", 0, 0, 0, 0, 0) + bounds(verts) + struct.pack("<2i3f", 1, 0, 0, 0, 0)
        chains.append([nughg.Gobj(head, [nughg.Geom(0, nuvif.chain([corners]))])])
        every += [(x + cx, y + DEPTH, z - cz) for x, y, z in verts]
    g.layers = [nughg.Layer("defaultLayer", chains)]

    lo = [min(p[i] for p in every) for i in range(3)]
    hi = [max(p[i] for p in every) for i in range(3)]
    radius = max(math.dist(p, (0, 0, 0)) for p in every)
    # sphere radius, sphere y, box, cylinder y, height, radius
    g.bounds = struct.pack("<2f3f3f3f", radius, 0.0, *lo, *hi, lo[1], hi[1] - lo[1], radius)
    return nughg.write(g)


def main(argv: list[str]) -> int:
    if len(argv) != 2:
        print(__doc__.strip().splitlines()[-1].strip(), file=sys.stderr)
        return 2
    data = build(Path(argv[0]))
    out = Path(argv[1])
    if not out.exists() or out.read_bytes() != data:
        out.write_bytes(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
