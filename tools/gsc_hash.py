#!/usr/bin/env python3
"""The --check-scenes lines of every .gsc, decoded by the crashwoc tooling (nus_scene.py, nus_ps2.py, rnc.py).

  python tools/gsc_hash.py --data-dir <root> [--tooling <dir>] [--compare <crashwoc-nuworld>]

The tooling comes from --tooling or $CRASHWOC_TOOLING; without it the exit code is 77 (skipped).
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import math
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
MANIFEST = REPO / "src" / "vfs" / "disc_manifest.inc"
SKIP = 77
VERSION = 6
# The blocks NuGScnLoadFix reads.
KNOWN = {b"TST2", b"TST0", b"MS02", b"MS00", b"OBJ0", b"INST", b"NTBL", b"SPEC", b"SST0", b"IABL", b"ALIB",
         b"INID", b"TAS0"}
MTL_SIZE = 0x1D0
EMPTY = hashlib.sha256().hexdigest()


def scene_paths(manifest: Path = MANIFEST) -> list[str]:
    """The .GSC entries of the disc manifest, in its order (NW_MANIFEST_SKIP ones left out)."""
    out = []
    for m in re.finditer(r'\{ "([^"]+)", (\d+)u, "[0-9a-f]{64}", ([^ ]+) \}', manifest.read_text()):
        if m.group(1).upper().endswith(".GSC") and m.group(3) == "0":
            out.append(m.group(1))
    return out


def blocks(nu: bytes) -> tuple[list[tuple[bytes, int, int]], int, bool]:
    """The block list as nw_gsc_open walks it: (id, data offset, data size), the unknown count, truncated."""
    if nu[:4] != b"NU20" or len(nu) < 16:
        raise ValueError("not an NU20 scene")
    if struct.unpack_from("<I", nu, 8)[0] != VERSION:
        raise ValueError(f"scene version {struct.unpack_from('<I', nu, 8)[0]}")
    out, unknown, off = [], 0, 16
    while off < len(nu):
        if len(nu) - off < 8:
            return out, unknown, True
        size = abs(struct.unpack_from("<i", nu, off + 4)[0])
        if size < 8:
            raise ValueError(f"block at {off:#x} of {size} bytes")
        if size > len(nu) - off:
            return out, unknown, True
        tag = nu[off:off + 4]
        out.append((tag, off + 8, size - 8))
        unknown += tag not in KNOWN
        off += size
    return out, unknown, False


def linear(blob: bytes) -> bytes:
    """A geom's DMA chain as the VIF receives it: the first tag whole (blob_geoms skips it), then per later tag its two
    VIF words, each followed by the tag's data."""
    out, off, first = bytearray(), 0, True
    while True:
        if off + 16 > len(blob):
            raise ValueError(f"DMA tag at {off:#x} outside the geom")
        lo, addr = struct.unpack_from("<II", blob, off)
        qwc, tag = lo & 0xFFFF, (lo >> 28) & 7
        end = off + 16 + qwc * 16
        if end > len(blob):
            raise ValueError(f"DMA tag at {off:#x} runs past the geom")
        out += blob[off:off + 16] if first else blob[off + 8:off + 16]
        out += blob[off + 16:end]
        first = False
        if tag in (6, 7):
            return bytes(out)
        if tag == 1:
            off = end
        elif tag == 2:
            off = addr
        else:
            raise ValueError(f"DMA tag id {tag} at {off:#x}")
        if len(out) > len(blob):
            raise ValueError("DMA chain sends more than the geom holds")


def obj_line(objid: int, geoms: list[dict], strip_tris) -> tuple[str, int, int, int]:
    """The OBJ line of an object's decoded batches (the tooling's blob_geoms dicts) and its counts."""
    vtx, idx = hashlib.sha256(), hashlib.sha256()
    nverts = ntris = 0
    for g in geoms:
        pos, uv, col = g["pos"], g["uv"], g["col"]
        if uv is None or col is None or len(uv) != len(pos) or len(col) != len(pos):
            raise ValueError(f"object {objid}: a batch without texcoords or colours for every vertex")
        for p, t, c in zip(pos, uv, col):
            vtx.update(struct.pack("<5f4B", p[0], p[1], p[2], t[0], t[1], *c))
        for tri in strip_tris([p[:3] for p in pos], adc=g["adc"], nloop=g["nloop"], coord_limit=math.inf):
            idx.update(struct.pack("<3I", *(nverts + i for i in tri)))
            ntris += 1
        nverts += len(pos)
    line = f"OBJ {objid} batches={len(geoms)} verts={nverts} tris={ntris} vtx={vtx.hexdigest()} idx={idx.hexdigest()}"
    return line, len(geoms), nverts, ntris


def scene_lines(path: str, nu: bytes, tooling) -> tuple[list[str], dict]:
    nus_scene = tooling["nus_scene"]
    found, unknown, truncated = blocks(nu)
    last = {tag: (off, size) for tag, off, size in found}
    mtls = last.get(b"MS00") or last.get(b"MS02")
    objects = []
    if b"OBJ0" in last:
        objects, _meta = nus_scene.read_objset(nu, last[b"OBJ0"])
    lines, batches, verts, tris = [], 0, 0, 0
    for objid, blobs in enumerate(objects):
        geoms = []
        for off, size, mtl in blobs:
            attrib1 = struct.unpack_from("<I", nu, mtls[0] + 8 + mtl * MTL_SIZE + 0x170)[0]
            if attrib1 & 8:  # attrib.ot: triangle blocks for the ordering table, no DMA chain
                continue
            stream = linear(nu[off:off + size])
            geoms += nus_scene.blob_geoms(stream, 0, len(stream))
        line, b, v, t = obj_line(objid, geoms, nus_scene.strip_tris)
        lines.append(line)
        batches, verts, tris = batches + b, verts + v, tris + t
    lines.append(f"GSC {path} chunks={len(found)} unknown={unknown} truncated={int(truncated)} "
                 f"objects={len(objects)} batches={batches} verts={verts} tris={tris}")
    return lines, {"unknown": unknown, "truncated": int(truncated), "objects": len(objects), "batches": batches,
                   "verts": verts, "tris": tris}


def load_tooling(path: str | None) -> dict | None:
    path = path or os.environ.get("CRASHWOC_TOOLING")
    if not path or not (Path(path) / "nus_scene.py").is_file():
        return None
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(Path(path).resolve()))
    import nus_scene  # noqa: E402
    import rnc  # noqa: E402
    return {"nus_scene": nus_scene, "rnc": rnc}


def all_lines(root: Path, tooling: dict) -> list[str]:
    lines = []
    total = {"files": 0, "failed": 0, "unknown": 0, "truncated": 0, "objects": 0, "batches": 0, "verts": 0,
             "tris": 0}
    for path in scene_paths():
        raw = (root / "disc" / path).read_bytes()
        nu = tooling["rnc"].unpack(raw) if raw[:4] == b"RNC\x02" else raw
        total["files"] += 1
        try:
            found, counts = scene_lines(path, nu, tooling)
        except ValueError as e:
            print(f"gsc_hash: {path}: {e}", file=sys.stderr)
            total["failed"] += 1
            continue
        lines += found
        for k, v in counts.items():
            total[k] += v
    lines.append("TOTAL " + " ".join(f"{k}={v}" for k, v in total.items()))
    return lines


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-dir", type=Path, required=True, help="the data root (with disc/)")
    parser.add_argument("--tooling", help="the crashwoc tooling folder (default: $CRASHWOC_TOOLING)")
    parser.add_argument("--compare", help="crashwoc-nuworld: diff its --check-scenes lines against these")
    args = parser.parse_args()

    tooling = load_tooling(args.tooling)
    if tooling is None:
        print("gsc_hash: no crashwoc tooling (--tooling or CRASHWOC_TOOLING): skipped")
        return SKIP
    lines = all_lines(args.data_dir, tooling)
    if not args.compare:
        print("\n".join(lines))
        return 0
    run = subprocess.run([args.compare, "--data-dir", str(args.data_dir), "--check-scenes"], capture_output=True,
                         text=True)
    theirs = [line for line in run.stdout.splitlines() if line.split(" ", 1)[0] in ("OBJ", "GSC", "TOTAL")]
    diff = list(difflib.unified_diff(lines, theirs, "tooling", "crashwoc-nuworld", lineterm="", n=1))
    if diff:
        print("\n".join(diff[:40]))
        print(f"gsc_hash: {sum(d[:1] in '+-' for d in diff[2:])} lines differ", file=sys.stderr)
        return 1
    print(f"gsc_hash: {len(lines)} lines match ({lines[-1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
