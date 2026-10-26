#!/usr/bin/env python3
"""Compare crashwoc-nuworld --dump's PS2 view of the game (<vblank>.nwd) with PCSX2's EE RAM at that vblank.

The data image's blocks sit at their addresses and heap records where the loader says retail keeps them; the rest
(the superbuffer, records with no known place) go where most of PCSX2's pointers to them point. Every placed
block is then compared byte for byte, pointer slots as the address their target was placed at; differences are
named by symbol and field (typedb.json). Slots holding host pointers the dump could not place only have to agree
with each other.

  python tools/ps2_diff.py <port>.nwd <pcsx2>.ee [--typedb build/release/gen/typedb.json]
"""
from __future__ import annotations

import argparse
import bisect
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TYPEDB = ROOT / "build" / "release" / "gen" / "typedb.json"
KNOWN = ROOT / "tools" / "ps2_diff_known.txt"

MAGIC = b"NWPS2V1\0"
ABS, IN, FREE = 0, 1, 2
OPAQUE = 0x80000000
NONE = 0xFFFFFFFF
RAM_SIZE = 0x2000000
CHUNK = 4096


@dataclass
class Block:
    id: int
    kind: int
    parent: int
    base: int
    size: int
    count: int
    name: str
    type: str
    path: str
    data: bytes
    relocs: list[tuple[int, int, int]]

    def label(self) -> str:
        if self.name:
            return self.name
        return f"{self.type or 'bytes'} <- {self.path}" if self.path else f"block {self.id}"


@dataclass
class Dump:
    vblank: int
    vbcnt: int
    blocks: list[Block]
    opaque: list[int]
    counters: dict[str, int]


def read_dump(data: bytes) -> Dump:
    if data[:8] != MAGIC:
        raise ValueError("not a crashwoc-nuworld --dump file")
    vblank, vbcnt = struct.unpack_from("<iI", data, 8)
    at, blocks = 16, []

    def text() -> str:
        nonlocal at
        (n,) = struct.unpack_from("<H", data, at)
        s = data[at + 2:at + 2 + n].decode("latin-1")
        at += 2 + n
        return s

    while True:
        (bid,) = struct.unpack_from("<I", data, at)
        at += 4
        if bid == NONE:
            break
        kind, parent, base, size, count = struct.unpack_from("<5I", data, at)
        at += 20
        name, type_, path = text(), text(), text()
        body = data[at:at + size]
        at += size
        (n,) = struct.unpack_from("<I", data, at)
        at += 4
        relocs = [struct.unpack_from("<3I", data, at + 12 * i) for i in range(n)]
        at += 12 * n
        blocks.append(Block(bid, kind, parent, base, size, count, name, type_, path, body, relocs))
    (n,) = struct.unpack_from("<I", data, at)
    opaque = list(struct.unpack_from(f"<{n}Q", data, at + 4))
    at += 4 + 8 * n
    names = ("failed", "unplaced", "conflicts", "unmapped")
    return Dump(vblank, vbcnt, blocks, opaque, dict(zip(names, struct.unpack_from("<4I", data, at))))


def u32(ram: bytes, addr: int) -> int:
    a = addr & (RAM_SIZE - 1)
    return int.from_bytes(ram[a:a + 4], "little")


def place(dump: Dump, ram: bytes) -> tuple[dict[int, int], int]:
    """Block id -> PS2 address, and how many pointer votes disagreed with the winner."""
    blocks = dump.blocks
    base = {b.id: b.base for b in blocks if b.kind == ABS}
    children = defaultdict(list)
    for b in blocks:
        if b.kind == IN:
            children[b.parent].append(b)
    votes: dict[int, Counter] = defaultdict(Counter)
    fresh = list(base)
    while fresh:
        for bid in fresh:
            for c in children[bid]:
                if c.id not in base:
                    base[c.id] = base[bid] + c.base
                    fresh.append(c.id)
        for bid in fresh:
            for off, target, toff in blocks[bid].relocs:
                if target & OPAQUE or target in base or blocks[target].kind != FREE:
                    continue
                v = u32(ram, base[bid] + off)
                if toff <= v < RAM_SIZE and v:
                    votes[target][v - toff] += 1
        fresh = []
        for target, c in votes.items():
            if target not in base:
                base[target] = c.most_common(1)[0][0]
                fresh.append(target)
    split = sum(sum(c.values()) - c[base[t]] for t, c in votes.items())
    return base, split


class Fields:
    """typedb.json: a record's field at a PS2 offset, recursively."""

    def __init__(self, typedb: dict | None):
        self.records = (typedb or {}).get("records", {})
        self.cache: dict[str, tuple[list[int], list[dict]]] = {}

    def _fields(self, ref: str) -> tuple[list[int], list[dict]]:
        if ref not in self.cache:
            fs = sorted(self.records.get(ref, {}).get("fields", []), key=lambda f: f["ps2"]["offset"])
            self.cache[ref] = ([f["ps2"]["offset"] for f in fs], fs)
        return self.cache[ref]

    def size(self, ref: str) -> int:
        return self.records.get(ref, {}).get("ps2", {}).get("size", 0)

    def at(self, ref: str, off: int) -> tuple[str, int, int, dict]:
        """(path, start, size, leaf) of the field holding off; ("+0x..", off, 1, {}) past the known fields."""
        offs, fs = self._fields(ref)
        i = bisect.bisect_right(offs, off) - 1
        while i >= 0 and offs[i] + fs[i]["ps2"]["size"] <= off:
            i -= 1
        if i < 0:
            return f"+0x{off:x}", off, 1, {}
        f = fs[i]
        start = f["ps2"]["offset"]
        bits = [g for g in fs if g["ps2"]["offset"] == start and "bit" in g["ps2"]]
        name = "|".join(g["name"] for g in bits) if bits else (f["name"] or "")
        kind = f["kind"]
        if kind == "record" and not bits:
            path, s, n, leaf = self.at(f["ref"], off - start)
            return (f"{name}.{path}" if name else path).replace(".+", "+"), start + s, n, leaf
        if kind == "array":
            elem = f["element"]
            esize = f["ps2"]["size"] // max(f["count"], 1)
            ix = (off - start) // esize if esize else 0
            if elem.get("kind") == "record":
                path, s, n, leaf = self.at(elem["ref"], off - start - ix * esize)
                return f"{name}[{ix}].{path}".replace(".+", "+"), start + ix * esize + s, n, leaf
            return f"{name}[{ix}]", start + ix * esize, esize, elem
        return name, start, f["ps2"]["size"], f


def show(raw: bytes, leaf: dict, symbolize) -> str:
    kind = leaf.get("kind")
    if len(raw) == 4 and kind == "float":
        return f"{struct.unpack('<f', raw)[0]:.9g}"
    if len(raw) == 4 and kind in ("pointer", "fnptr"):
        return symbolize(int.from_bytes(raw, "little"))
    if len(raw) in (1, 2, 4, 8) and kind in ("int", "enum"):
        v = int.from_bytes(raw, "little", signed=bool(leaf.get("signed")))
        return f"{v} (0x{int.from_bytes(raw, 'little'):x})" if abs(v) > 9 else str(v)
    return raw.hex(" ")


def rounding(a: bytes, b: bytes, leaf: dict, ulps: int) -> bool:
    """Floats a few units in the last place apart (EE rounding, #91), or zeros of either sign. Untyped words count
    when both are normal floats of the same sign."""
    if len(a) != 4 or leaf.get("kind") not in (None, "float"):
        return False
    x, y = struct.unpack("<II", a + b)
    if (x | y) & 0x7FFFFFFF == 0:
        return True
    exp = lambda v: v >> 23 & 0xFF  # noqa: E731
    if leaf.get("kind") is None and not (0 < exp(x) < 0xFF and 0 < exp(y) < 0xFF):
        return False
    return x >> 31 == y >> 31 and abs(x - y) <= ulps


class Known:
    """tools/ps2_diff_known.txt: "regex  # reason" per line, over "<symbol or type>.<field>"; differences the port
    has on purpose."""

    def __init__(self, path: Path | None):
        self.rules: list[tuple[re.Pattern, str]] = []
        for line in (path.read_text(encoding="utf-8").splitlines() if path and path.is_file() else []):
            pattern, _, reason = line.partition("#")
            if pattern.strip():
                self.rules.append((re.compile(pattern.strip()), reason.strip() or pattern.strip()))

    def reason(self, name: str) -> str | None:
        return next((r for p, r in self.rules if p.search(name)), None)


@dataclass
class Result:
    vblank: int
    blocks: int = 0
    placed: int = 0
    compared: int = 0
    bytes: int = 0
    differ_blocks: int = 0
    differ_fields: int = 0
    pointers: int = 0
    pointer_diffs: int = 0
    floats: int = 0
    known: Counter = field(default_factory=Counter)
    opaque_slots: int = 0
    opaque_split: int = 0
    split_votes: int = 0
    outside: int = 0
    lines: list[str] = field(default_factory=list)
    differing: list[str] = field(default_factory=list)

    def summary(self) -> str:
        known = sum(self.known.values())
        return (f"vblank {self.vblank}: {self.placed}/{self.blocks} blocks placed, {self.compared} compared "
                f"({self.bytes} bytes); {self.differ_blocks} differ in {self.differ_fields} fields "
                f"({self.pointer_diffs} of {self.pointers} pointers); {self.floats} float rounding, {known} known; "
                f"{self.opaque_slots} unplaced host pointers ({self.opaque_split} with more than one PCSX2 value); "
                f"{self.split_votes} split placement votes")


def diff_runs(a: bytes, b: bytes) -> list[tuple[int, int]]:
    """[start, end) runs where a and b differ."""
    runs, n = [], min(len(a), len(b))
    for c in range(0, n, CHUNK):
        x, y = a[c:c + CHUNK], b[c:c + CHUNK]
        if x == y:
            continue
        i = 0
        while i < len(x):
            if x[i] != y[i]:
                j = i
                while j < len(x) and x[j] != y[j]:
                    j += 1
                if runs and runs[-1][1] == c + i:
                    runs[-1] = (runs[-1][0], c + j)
                else:
                    runs.append((c + i, c + j))
                i = j
            else:
                i += 1
    return runs


def compare(dump: Dump, ram: bytes, fields: Fields, known: Known | None = None, ulps: int = 16, limit: int = 40,
            per_block: int = 8, show_all: bool = False) -> Result:
    """Differences per field: float rounding and known ones are only counted (listed with show_all)."""
    known = known or Known(None)
    res = Result(dump.vblank, blocks=len(dump.blocks))
    base, res.split_votes = place(dump, ram)
    res.placed = len(base)
    blocks = dump.blocks
    spans = sorted((base[b.id], base[b.id] + max(b.size, 1), b.label()) for b in blocks if b.id in base)
    starts = [s[0] for s in spans]

    def symbolize(v: int) -> str:
        i = bisect.bisect_right(starts, v) - 1
        if v and i >= 0 and v < spans[i][1]:
            return f"0x{v:08x} ({spans[i][2]}+0x{v - spans[i][0]:x})"
        return f"0x{v:08x}"

    covered = defaultdict(list)  # parent -> [start, end) its placed children compare themselves, or holes
    for b in blocks:
        if b.kind == IN and b.id in base and b.parent in base:
            if b.type == "hole":
                covered[b.parent].append((b.base, b.base + b.count))
                res.known[b.name or "hole"] += 1
            elif b.size:
                covered[b.parent].append((b.base, b.base + b.size))
    opaque_values: dict[int, set[int]] = defaultdict(set)
    for b in sorted((b for b in blocks if b.id in base and b.size), key=lambda b: base[b.id]):
        label = b.label()
        addr = base[b.id]
        if addr + b.size > RAM_SIZE:
            res.outside += 1
            continue
        res.compared += 1
        res.bytes += b.size
        port = bytearray(b.data)
        want = ram[addr:addr + b.size]
        slots = set()
        for off, target, toff in b.relocs:
            slots.add(off)
            got = u32(ram, addr + off)
            if target & OPAQUE:
                res.opaque_slots += 1
                opaque_values[target].add(got)
                port[off:off + 4] = want[off:off + 4]
                continue
            res.pointers += 1
            if target in base:
                port[off:off + 4] = struct.pack("<I", base[target] + toff)
            else:
                port[off:off + 4] = want[off:off + 4]
        for lo, hi in covered.get(b.id, []):
            port[lo:hi] = want[lo:hi]
        runs = diff_runs(bytes(port), want)
        if not runs:
            continue
        elem = fields.size(b.type) if b.type else 0
        notes: list[str] = []
        seen: set[str] = set()
        real = 0
        for lo, hi in runs:
            off = lo
            while off < hi:
                if b.type and elem:
                    ix = off // elem
                    path, start, size, leaf = fields.at(b.type, off - ix * elem)
                    start += ix * elem
                    name = f"[{ix}].{path}" if b.count > 1 else path
                elif b.type:
                    name, start, size, leaf = f"+0x{off:x}", off, 4 - off % 4, {}
                else:
                    start = off - off % 4
                    name, size, leaf = f"+0x{start:x}", 4, {}
                size = max(size, 1)
                off = start + size
                if name in seen:
                    continue
                seen.add(name)
                p, w = bytes(port[start:start + size]), want[start:start + size]
                pointer = start in slots or leaf.get("kind") in ("pointer", "fnptr")
                why = known.reason(f"{b.name or b.type or 'bytes'}.{name}")
                if why:
                    res.known[why] += 1
                    tag = f" [known: {why}]"
                elif not pointer and rounding(p, w, leaf, ulps):
                    res.floats += 1
                    tag = " [float rounding]"
                else:
                    real += 1
                    res.pointer_diffs += start in slots
                    tag = ""
                if (show_all or not tag) and len(notes) < per_block:
                    leaf = {"kind": "pointer"} if pointer else leaf
                    notes.append(f"    {name}: port {show(p, leaf, symbolize)}, PCSX2 {show(w, leaf, symbolize)}{tag}")
        if not real and not show_all:
            continue
        res.differ_blocks += real > 0
        res.differ_fields += real
        res.differing.append(label)
        if len(res.differing) <= limit:
            kind = f"{b.type} x{b.count}" if b.type else "bytes"
            res.lines.append(f"  {label} at 0x{addr:08x} ({kind}, {b.size} bytes): {real} fields differ")
            res.lines += notes
            shown = len(seen) if show_all else real
            if shown > len(notes):
                res.lines.append(f"    ... and {shown - len(notes)} more")
    res.opaque_split = sum(len(v) > 1 for v in opaque_values.values())
    return res


def load_typedb(path: Path | None) -> Fields:
    if path and path.is_file():
        return Fields(json.loads(path.read_text(encoding="utf-8")))
    return Fields(None)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("dump", type=Path, help="crashwoc-nuworld --dump's <vblank>.nwd")
    parser.add_argument("ram", type=Path, help="PCSX2's EE RAM at that vblank (tools/pcsx2_dump.py, <vblank>.ee)")
    parser.add_argument("--typedb", type=Path, default=TYPEDB, help=f"field names (default {TYPEDB})")
    parser.add_argument("--known", type=Path, default=KNOWN, help=f"differences on purpose (default {KNOWN})")
    parser.add_argument("--ulps", type=int, default=16, help="float rounding tolerance in last places (default 16)")
    parser.add_argument("--all", action="store_true", help="also list float rounding and known differences")
    parser.add_argument("--limit", type=int, default=40, help="differing blocks listed (default 40)")
    args = parser.parse_args()
    dump = read_dump(args.dump.read_bytes())
    res = compare(dump, args.ram.read_bytes(), load_typedb(args.typedb), Known(args.known), args.ulps, args.limit,
                  show_all=args.all)
    print(res.summary())
    c = dump.counters
    print(f"port: {c['failed']} blocks failed to convert, {c['unplaced']} pointer marks not found, "
          f"{c['conflicts']} type conflicts, {c['unmapped']} pointers into no field")
    for reason, n in res.known.most_common():
        print(f"known: {n} x {reason}")
    for line in res.lines:
        print(line)
    if len(res.differing) > args.limit:
        print(f"  ... and {len(res.differing) - args.limit} more blocks")
    return 1 if res.differ_blocks else 0


if __name__ == "__main__":
    sys.exit(main())
