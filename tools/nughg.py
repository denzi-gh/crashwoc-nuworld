#!/usr/bin/env python3
""".ghg read/write in NuGHGWrite's order; retail files round trip byte-identical.

    python tools/nughg.py <file.ghg>...   check the round trip
"""
from __future__ import annotations

import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

HEADER = 0x90
MTL = 0x1D0
JOINT = 0x60
MTX = 0x40
POI = 0x50
LAYER = 0x14
GOBJ = 0x5C  # nufgobj_s
GEOM = 0x10  # nufgeomps2_s: next, mtl_id, dmastream, skindma
TEXANIM = 0x20
SHADOW = 0x10
SHADOW_MESH = 8
SHADOW_VTX = 0x10
ELLIPSOID = 0x30
CYLINDER = 0x40
COLLIDE_SPHERE = 0x14

# Header offsets (nuhghhdr_s).
H_SIZE, H_VERSION, H_NUMTID, H_TIDS, H_NUMMTL, H_MTLS = 0x00, 0x04, 0x08, 0x0C, 0x10, 0x14
H_NJOINTS, H_JOINTS, H_T, H_INVWT, H_NJIXS, H_JIXS = 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C
H_STRTAB, H_STRSIZE, H_NPOIIXS, H_POIIXS, H_NPOIS, H_POIS = 0x30, 0x34, 0x38, 0x3C, 0x40, 0x44
H_NLAYERS, H_LAYERS, H_SHADOW, H_BOUNDS, H_COLLIDE, H_SCALE = 0x48, 0x4C, 0x50, 0x54, 0x80, 0x84
H_NTEXANIMS, H_TEXANIMS = 0x88, 0x8C
BOUNDS = H_COLLIDE - H_BOUNDS  # sphere, box, cylinder


def align(n: int, a: int) -> int:
    return (n + a - 1) & ~(a - 1)


@dataclass
class Geom:
    mtl: int
    dma: bytes | None = None
    skin: bytes | None = None


@dataclass
class Gobj:
    head: bytes                # nufgobj_s, pointers rewritten
    geoms: list[Geom] = field(default_factory=list)


@dataclass
class Layer:
    name: str | None = None
    gobjs: list[list[Gobj]] | None = None        # a chain per joint
    blend_gobjs: list[list[Gobj]] | None = None
    skin: list[Gobj] = field(default_factory=list)
    blend_skin: list[Gobj] = field(default_factory=list)


@dataclass
class TexAnim:
    rec: bytes                 # nutexanim_s, pointers rewritten
    mtl: int
    ntaname: int               # string table offsets
    scriptname: int
    tids: list[int]


@dataclass
class Shadow:
    rec: bytes                 # NUSHADOWDATA_s, pointers rewritten
    ellipsoids: bytes = b""
    cylinders: bytes = b""
    meshes: list[tuple[bytes, bytes, bytes]] = field(default_factory=list)  # (record, normal_counts, vert_counts)


@dataclass
class Ghg:
    version: int = 0
    scale: bytes = struct.pack("<f", 1.0)
    textures: list[bytes] = field(default_factory=list)
    materials: list[bytes] = field(default_factory=list)   # numtl_s, tid = texture index + 1 or 0
    strings: bytes = b"\0"
    joints: list[bytes] = field(default_factory=list)      # NUJOINTDATA_s, name = string table offset or -1
    t: bytes = b""
    inv_wt: bytes = b""
    joint_ixs: bytes = b""
    poi_ixs: bytes = b""
    pois: list[bytes] = field(default_factory=list)        # NUPOINTOFINTEREST_s
    layers: list[Layer] = field(default_factory=list)
    bounds: bytes = bytes(BOUNDS)
    collide: tuple[bytes, list[tuple[bytes, bytes]]] | None = None  # (nuhcollide_s, [(NUCOLLISIONHDR_s, data)])
    texanims: list[TexAnim] = field(default_factory=list)
    shadows: list[Shadow] = field(default_factory=list)
    shadow_end: bytes = b""


def _i32(d: bytes, o: int) -> int:
    return struct.unpack_from("<i", d, o)[0]


def _cstr(d: bytes, o: int) -> str:
    return d[o:d.index(b"\0", o)].decode("latin-1")


class _Reader:
    """A blob runs to the next record's start: NuGHGWrite leaves no gaps but alignment."""

    def __init__(self, d: bytes):
        self.d = d
        self.starts = {len(d)}

    def mark(self, o: int) -> int:
        if o:
            self.starts.add(o)
        return o

    def blob(self, o: int) -> bytes:
        end = min(s for s in self.starts if s > o)
        return self.d[o:end]


def _chain(r: _Reader, off: int, blobs: list) -> list[Gobj]:
    out = []
    while off:
        g = Gobj(r.d[off:off + GOBJ])
        r.mark(off)
        geom = _i32(r.d, off + 0x0C)
        while geom:
            nxt, mtl, dma, skin = struct.unpack_from("<4i", r.d, geom)
            r.mark(geom)
            item = Geom(mtl)
            for k, at in (("dma", dma), ("skin", skin)):
                if at:
                    r.mark(at)
                    blobs.append((item, k, at))
            g.geoms.append(item)
            geom = nxt
        out.append(g)
        off = _i32(r.d, off + 0x4C)
    return out


def read(d: bytes) -> Ghg:
    if len(d) < HEADER or _i32(d, H_SIZE) != len(d):
        raise ValueError("not a .ghg: header size %d, file %d" % (_i32(d, H_SIZE) if len(d) >= 4 else -1, len(d)))
    r = _Reader(d)
    h = lambda o: _i32(d, o)  # noqa: E731
    g = Ghg(version=h(H_VERSION), scale=d[H_SCALE:H_SCALE + 4], bounds=d[H_BOUNDS:H_COLLIDE])
    tex = [r.mark(_i32(d, h(H_TIDS) + 4 * i)) for i in range(h(H_NUMTID))]
    strtab = r.mark(h(H_STRTAB))
    g.strings = d[strtab:strtab + h(H_STRSIZE)]
    for o in (H_MTLS, H_JOINTS, H_T, H_INVWT, H_JIXS, H_POIIXS, H_POIS, H_LAYERS, H_SHADOW, H_COLLIDE, H_TEXANIMS):
        r.mark(h(o))
    mtls = [r.mark(_i32(d, h(H_MTLS) + 4 * i)) for i in range(h(H_NUMMTL))]
    g.materials = [d[o:o + MTL] for o in mtls]

    def named(rec: bytes, at: int) -> bytes:
        name = _i32(rec, at)
        return rec[:at] + struct.pack("<i", name - strtab if name else -1) + rec[at + 4:]

    nj = h(H_NJOINTS)
    g.joints = [named(d[h(H_JOINTS) + j * JOINT:h(H_JOINTS) + (j + 1) * JOINT], 0x4C) for j in range(nj)]
    g.t = d[h(H_T):h(H_T) + nj * MTX]
    g.inv_wt = d[h(H_INVWT):h(H_INVWT) + nj * MTX]
    g.joint_ixs = d[h(H_JIXS):h(H_JIXS) + h(H_NJIXS)]
    if h(H_NPOIIXS):
        g.poi_ixs = d[h(H_POIIXS):h(H_POIIXS) + h(H_NPOIIXS)]
    g.pois = [named(d[h(H_POIS) + i * POI:h(H_POIS) + (i + 1) * POI], 0x4C) for i in range(h(H_NPOIS))]

    blobs: list = []
    for i in range(h(H_NLAYERS)):
        name, gobjs, skin, blend, bskin = struct.unpack_from("<5i", d, h(H_LAYERS) + i * LAYER)
        layer = Layer(_cstr(d, r.mark(name)) if name else None)
        if gobjs:
            layer.gobjs = [_chain(r, _i32(d, r.mark(gobjs) + 4 * j), blobs) for j in range(nj)]
        if blend:
            layer.blend_gobjs = [_chain(r, _i32(d, r.mark(blend) + 4 * j), blobs) for j in range(nj)]
        layer.skin = _chain(r, skin, blobs)
        layer.blend_skin = _chain(r, bskin, blobs)
        g.layers.append(layer)

    if h(H_COLLIDE):
        head = d[h(H_COLLIDE):h(H_COLLIDE) + 8]
        n, entries = struct.unpack_from("<Ii", head)
        r.mark(entries)
        items = []
        for i in range(n):
            e = d[entries + 8 * i:entries + 8 * i + 8]
            typ, data = struct.unpack_from("<ii", e)
            r.mark(data)
            items.append((e, d[data:data + COLLIDE_SPHERE] if typ == 1 else b""))
        g.collide = (head, items)

    if h(H_NTEXANIMS):
        base = h(H_TEXANIMS)
        n = h(H_NTEXANIMS)
        for i in range(n):
            rec = d[base + i * TEXANIM:base + (i + 1) * TEXANIM]
            tids, count = _i32(rec, 0x08), struct.unpack_from("<h", rec, 0x0C)[0]
            g.texanims.append(TexAnim(rec, _i32(rec, 0x10), _i32(rec, 0x18) - strtab, _i32(rec, 0x1C) - strtab,
                                      list(struct.unpack_from("<%dh" % count, d, tids))))

    if h(H_SHADOW):
        at = h(H_SHADOW)
        while d[at + 0xF] != 0xFF:
            rec = d[at:at + SHADOW]
            s = Shadow(rec)
            ne, nc, nm = rec[0xC], rec[0xD], rec[0xE]
            ell, cyl, meshes = struct.unpack_from("<3i", rec)
            s.ellipsoids = d[ell:ell + ne * ELLIPSOID] if ne else b""
            s.cylinders = d[cyl:cyl + nc * CYLINDER] if nc else b""
            for m in range(nm):
                mrec = d[meshes + m * SHADOW_MESH:meshes + (m + 1) * SHADOW_MESH]
                normals, verts = struct.unpack_from("<ii", mrec)
                s.meshes.append((mrec, normals, verts))
            g.shadows.append(s)
            at += SHADOW
        g.shadow_end = d[at:at + SHADOW]
        # vertex counts run back to back
        for s in g.shadows:
            for m in s.meshes:
                r.mark(m[1])
                r.mark(m[2])
        for s in g.shadows:
            s.meshes = [(mrec, r.blob(n), r.blob(v)) for mrec, n, v in s.meshes]

    g.textures = [r.blob(o) for o in tex]
    for item, k, at in blobs:
        setattr(item, k, r.blob(at))
    return g


class _Writer:
    def __init__(self):
        self.b = bytearray(HEADER)

    @property
    def ptr(self) -> int:
        return len(self.b)

    def align(self, a: int) -> int:
        self.b += bytes(align(self.ptr, a) - self.ptr)
        return self.ptr

    def put(self, data: bytes) -> int:
        at = self.ptr
        self.b += data
        return at

    def i32(self, at: int, v: int):
        struct.pack_into("<i", self.b, at, v)


def _gobjs(w: _Writer, chain: list[Gobj]) -> int:
    first, last = 0, None
    for g in chain:
        out = w.align(4)
        if last is None:
            first = out
        else:
            w.i32(last + 0x4C, out)
        w.put(g.head)
        w.i32(out + 0x10, 0)
        lastgeom = None
        for geom in g.geoms:
            og = w.align(4)
            if lastgeom is None:
                w.i32(out + 0x0C, og)
            else:
                w.i32(lastgeom, og)
            w.put(struct.pack("<4i", 0, geom.mtl, 0, 0))
            if geom.dma is not None:
                w.i32(og + 8, w.align(16))
                w.put(geom.dma)
            if geom.skin is not None:
                w.i32(og + 12, w.align(16))
                w.put(geom.skin)
            lastgeom = og
        last = out
    return first


def write(g: Ghg) -> bytes:
    w = _Writer()
    w.i32(H_VERSION, g.version)
    w.b[H_SCALE:H_SCALE + 4] = g.scale
    w.i32(H_NUMTID, len(g.textures))
    if g.textures:
        offs = w.put(bytes(4 * len(g.textures)))
        w.i32(H_TIDS, offs)
        for i, t in enumerate(g.textures):
            w.i32(offs + 4 * i, w.align(16))
            w.put(t)
    offs = w.align(4)
    w.i32(H_NUMMTL, len(g.materials))
    w.i32(H_MTLS, offs)
    w.put(bytes(4 * len(g.materials)))
    for i, m in enumerate(g.materials):
        w.i32(offs + 4 * i, w.align(16))
        w.put(m)
    strtab = w.put(g.strings)
    w.i32(H_STRTAB, strtab)
    w.i32(H_STRSIZE, len(g.strings))

    def named(rec: bytes, at: int) -> bytes:
        name = _i32(rec, at)
        return rec[:at] + struct.pack("<i", name + strtab if name >= 0 else 0) + rec[at + 4:]

    w.i32(H_NJOINTS, len(g.joints))
    w.i32(H_JOINTS, w.align(16))
    for j in g.joints:
        w.put(named(j, 0x4C))
    w.i32(H_T, w.align(16))
    w.put(g.t)
    w.i32(H_INVWT, w.align(16))
    w.put(g.inv_wt)
    w.i32(H_NJIXS, len(g.joint_ixs))
    w.i32(H_JIXS, w.put(g.joint_ixs))
    if g.poi_ixs:
        w.i32(H_NPOIIXS, len(g.poi_ixs))
        w.i32(H_POIIXS, w.put(g.poi_ixs))
    if g.pois:
        w.i32(H_NPOIS, len(g.pois))
        w.i32(H_POIS, w.align(16))
        for p in g.pois:
            w.put(named(p, 0x4C))

    layers = w.align(4)
    w.i32(H_NLAYERS, len(g.layers))
    w.i32(H_LAYERS, layers)
    w.put(bytes(LAYER * len(g.layers)))
    for i, layer in enumerate(g.layers):
        rec = layers + i * LAYER
        if layer.name is not None:
            w.i32(rec, w.put(layer.name.encode("latin-1") + b"\0"))
        for slot, chains in ((4, layer.gobjs), (12, layer.blend_gobjs)):
            if chains is not None:
                offs = w.align(4)
                w.i32(rec + slot, offs)
                w.put(bytes(4 * len(g.joints)))
                for j, chain in enumerate(chains):
                    w.i32(offs + 4 * j, _gobjs(w, chain))
        w.i32(rec + 8, _gobjs(w, layer.skin))
        w.i32(rec + 16, _gobjs(w, layer.blend_skin))

    w.b[H_BOUNDS:H_COLLIDE] = g.bounds
    if g.collide:
        head, items = g.collide
        at = w.align(4)
        w.i32(H_COLLIDE, at)
        w.put(head)
        entries = w.put(b"".join(e for e, _ in items))
        w.i32(at + 4, entries)
        w.align(4)
        for i, (e, data) in enumerate(items):
            w.i32(entries + 8 * i + 4, w.put(data))

    w.i32(H_NTEXANIMS, len(g.texanims))
    if g.texanims:
        anims = w.align(16)
        w.i32(H_TEXANIMS, anims)
        w.put(b"".join(a.rec for a in g.texanims))
        for i, a in enumerate(g.texanims):
            rec = anims + i * TEXANIM
            w.i32(rec + 0x08, w.ptr)
            w.i32(rec + 0x10, a.mtl)
            w.i32(rec + 0x18, a.ntaname + strtab)
            w.i32(rec + 0x1C, a.scriptname + strtab)
            w.put(struct.pack("<%dh" % len(a.tids), *a.tids))

    if g.shadows:
        out = w.align(16)
        w.i32(H_SHADOW, out)
        w.put(b"".join(s.rec for s in g.shadows) + g.shadow_end)
        sizes = [sum(len(x) for x in parts) for parts in
                 ([s.ellipsoids for s in g.shadows], [s.cylinders for s in g.shadows])]
        ell = w.align(16) if sizes[0] else 0
        w.put(b"".join(s.ellipsoids for s in g.shadows))
        cyl = w.align(16) if sizes[1] else 0
        w.put(b"".join(s.cylinders for s in g.shadows))
        nmeshes = sum(len(s.meshes) for s in g.shadows)
        meshes = w.align(16) if nmeshes else 0
        w.put(b"".join(m[0] for s in g.shadows for m in s.meshes))
        if any(m[1] or m[2] for s in g.shadows for m in s.meshes):
            w.align(16)
        for i, s in enumerate(g.shadows):
            rec = out + i * SHADOW
            if s.ellipsoids:
                w.i32(rec, ell)
                ell += len(s.ellipsoids)
            if s.cylinders:
                w.i32(rec + 4, cyl)
                cyl += len(s.cylinders)
            if s.meshes:
                w.i32(rec + 8, meshes)
                for mrec, normals, verts in s.meshes:
                    w.i32(meshes, w.put(normals))
                    w.i32(meshes + 4, w.put(verts))
                    meshes += SHADOW_MESH

    w.i32(H_SIZE, w.ptr)
    return bytes(w.b)


def main(argv: list[str]) -> int:
    bad = 0
    for name in argv:
        d = Path(name).read_bytes()
        if write(read(d)) != d:
            print("%s: round trip differs" % name)
            bad += 1
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
