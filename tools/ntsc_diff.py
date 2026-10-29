#!/usr/bin/env python3
"""Diff the PAL boot ELF (SLES_503.86, 50 Hz) against the US one (SLUS_202.38, 60 Hz) per function and per data
symbol, telling the rate differences (a tick's amounts x6/5 or x36/25 at 50 Hz, tick counts x5/6) from the rest.

  python tools/ntsc_diff.py functions [--disasm NAME]  literals, immediates and structure per function
  python tools/ntsc_diff.py data                        .data/.sdata/.rodata values per symbol
  python tools/ntsc_diff.py check [--decomp DIR]        the decomp built with NW_HZ's NTSC values vs the US ELF, and
                                                        with its PAL values vs the PAL ELF (must be equal)
  python tools/ntsc_diff.py table [--check]             writes the rate fields as src/port/hz_table.inc

--pal / NUWORLD_PAL_ELF and --us / NUWORLD_US_ELF name the ELFs (the user's, never committed). Differences that are
not a rate (US 1.00 vs PAL 1.03, languages, sound ids) are listed in tools/ntsc_diff_known.txt: "regex  # reason"
over "func:<unit>/<name>" (its structure), "func:<unit>/<name>+0x<off>" (one site), "data:<symbol>+0x<off>" and
"data:<symbol>+size"; "+regex" marks a rate the ratios miss, "-regex" no rate although the ratio fits.
"""
from __future__ import annotations

import argparse
import bisect
import difflib
import os
import re
import struct
import subprocess
import sys
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DECOMP = ROOT / "decomp"
KNOWN = ROOT / "tools" / "ntsc_diff_known.txt"
OUT = ROOT / "build" / "ntsc_diff"
IMAGE = os.environ.get("CRASHWOC_IMAGE", "crashwoc-decomp")
VERSION = "pal103"

sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(DECOMP / "tools"))  # declib: the decomp's ELF reader and lui/%lo pairing
from declib.elf import LOADS, STORES, code_refs, read_elf  # noqa: E402
import nwunits  # noqa: E402
import ps2_diff  # noqa: E402

FUNC, OBJECT = 2, 1
R_MIPS_26, R_MIPS_HI16, R_MIPS_LO16, R_MIPS_GPREL16, R_MIPS_LITERAL = 4, 5, 6, 7, 8
LUI, ORI, COP1, LWC1, LDC1, LW = 0x0F, 0x0D, 0x11, 0x31, 0x35, 0x23
SP = 29
IMM_OPS = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x18, 0x19}
ZERO_EXT = {0x0C, 0x0D, 0x0E}  # andi ori xori
LITERAL_SECTIONS = (".lit4", ".lit8")
DATA_SECTIONS = (".data", ".sdata", ".rodata")
# PAL over NTSC: an amount per tick (6/5, a few 5/4) and per tick squared; tick counts the inverse
PER_TICK = (6 / 5, 5 / 4)
SCALES = PER_TICK + tuple(1 / k for k in PER_TICK)
RATIOS = SCALES + (36 / 25, 25 / 36)


def sext16(v: int) -> int:
    return v - 0x10000 if v & 0x8000 else v


def f32(bits: int) -> float:
    return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]


def f64(raw: bytes) -> float:
    return struct.unpack("<d", raw)[0]


def plausible_f32(bits: int) -> bool:
    """A float a programmer wrote: zero or an exponent of about 1e-7..1e9 (not a small int or packed bytes)."""
    e = bits >> 23 & 0xFF
    return bits & 0x7FFFFFFF == 0 or 0x68 <= e <= 0x9F


def plausible_f64(raw: bytes) -> bool:
    bits = int.from_bytes(raw, "little")
    e = bits >> 52 & 0x7FF
    return bits & ((1 << 63) - 1) == 0 or 0x3C0 <= e <= 0x43F


def rate_float(pal: float, ntsc: float) -> bool:
    if pal == ntsc or pal == 0 or ntsc == 0 or (pal < 0) != (ntsc < 0):
        return False
    r = pal / ntsc
    return any(abs(r - k) <= 1e-3 * k for k in RATIOS)


def rate_int(pal: int, ntsc: int) -> bool:
    """Below 4 ticks the rounding says nothing."""
    if pal == ntsc or (pal < 0) != (ntsc < 0) or max(abs(pal), abs(ntsc)) < 4:
        return False
    return any(abs(ntsc - pal / k) < 1.0 for k in SCALES)


class Known(ps2_diff.Known):
    """ntsc_diff_known.txt: "regex  # reason" is a difference on purpose, no rate; "+regex" a rate the ratios miss;
    "-regex" no rate although the ratio fits."""

    def __init__(self, path: Path | None):
        self.rules, self.rate, self.not_rate = [], [], []
        for line in (path.read_text(encoding="utf-8").splitlines() if path and path.is_file() else []):
            pattern, _, reason = line.partition("#")
            pattern, reason = pattern.strip(), reason.strip()
            if pattern:
                kind = {"+": self.rate, "-": self.not_rate}.get(pattern[0], self.rules)
                pattern = pattern[1:].strip() if kind is not self.rules else pattern
                kind.append((re.compile(pattern), reason or pattern))

    def is_rate(self, name: str) -> bool:
        return any(p.search(name) for p, _ in self.rate)

    def classify(self, name: str, cls: str) -> str:
        """cls after the + and - rules."""
        if cls == "review" and self.is_rate(name):
            return "rate"
        if cls == "rate" and any(p.search(name) for p, _ in self.not_rate):
            return "review"
        return cls


# -- code ----------------------------------------------------------------------------------------------------------

@dataclass(frozen=True)
class Val:
    """A value an instruction carries: a literal it loads (f32, f64, word with its 8-byte context) or an
    immediate (imm, the lui form hi)."""
    kind: str
    bits: int
    ctx: bytes = field(default=b"", compare=False)

    def show(self) -> str:
        if self.kind == "f32":
            return f"{f32(self.bits):.9g}"
        if self.kind == "f64":
            return f"{f64(self.ctx):.17g}"
        if self.kind == "word" and plausible_f64(self.ctx):
            return f"0x{self.bits:08x} ({f64(self.ctx):.17g} as double)"
        return str(self.bits) if self.kind in ("imm", "off", "frame") else f"0x{self.bits:08x}"


def classify(a: Val, b: Val) -> str:
    """rate or review, PAL value a against NTSC value b. Displacements and frame sizes are never a rate."""
    if a.kind != b.kind or a.kind in ("off", "frame"):
        return "review"
    if a.kind == "f32" or (a.kind == "hi" and plausible_f32(a.bits) and plausible_f32(b.bits)):
        return "rate" if rate_float(f32(a.bits), f32(b.bits)) else "review"
    if a.kind in ("f64", "word") and plausible_f64(a.ctx) and plausible_f64(b.ctx) and a.ctx != b.ctx:
        if rate_float(f64(a.ctx), f64(b.ctx)):
            return "rate"
    if a.kind == "word" and plausible_f32(a.bits) and plausible_f32(b.bits) and \
            rate_float(f32(a.bits), f32(b.bits)):
        return "rate"
    return "rate" if rate_int(a.bits, b.bits) else "review"


@dataclass
class Code:
    """One function as tokens, nops left out (the assemblers place them differently): what must agree (shape)
    and the value each instruction carries."""
    name: str
    unit: str
    addr: int
    words: list[int]
    offs: list[int] = field(default_factory=list)
    shape: list[tuple] = field(default_factory=list)
    vals: list[Val | None] = field(default_factory=list)


def branch(w: int) -> bool:
    op = w >> 26
    return op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17) or op == 0x01 and (w >> 16 & 31) < 4 or         op in (0x10, 0x11, 0x12) and (w >> 21 & 31) == 0x08


def tokenize(code: Code, mark: dict[int, Val | None]) -> Code:
    """mark: instruction index -> the literal it loads, or None for an address operand (masked)."""
    words, code.words = code.words, []
    for i, w in enumerate(words):
        if w == 0:
            continue
        code.words.append(w)
        code.offs.append(4 * i)
        op, rs, rt = w >> 26, w >> 21 & 31, w >> 16 & 31
        if op in (2, 3):
            code.shape.append((op,))
            code.vals.append(None)
        elif op == LUI:
            code.shape.append((op, rt))
            code.vals.append(mark[i] if i in mark else Val("hi", (w & 0xFFFF) << 16))
        elif i in mark:
            code.shape.append((op, rs, rt))
            code.vals.append(mark[i])
        elif branch(w):
            code.shape.append((op, rs, rt))
            code.vals.append(None)
        elif op in LOADS or op in STORES:
            code.shape.append((op, rs, rt))
            code.vals.append(Val("off", sext16(w & 0xFFFF)))
        elif op in IMM_OPS and rs == rt == SP:
            code.shape.append((op, rs, rt))
            code.vals.append(Val("frame", sext16(w & 0xFFFF)))
        elif op in IMM_OPS:
            imm = w & 0xFFFF
            code.shape.append((op, rs, rt))
            code.vals.append(Val("imm", imm if op in ZERO_EXT else sext16(imm)))
        else:
            code.shape.append((w,))
            code.vals.append(None)
    floats(code)
    return code


def writes(w: int) -> int | None:
    """The GPR an instruction writes, roughly (enough to end a lui's constant)."""
    op = w >> 26
    if op == 0:
        return w >> 11 & 31
    if op in IMM_OPS or op == LUI or op in LOADS and LOADS[op][2]:
        return w >> 16 & 31
    return None


def floats(code: Code) -> None:
    """lui [ori] mtc1 builds a float: the lui carries all of it (f32), the ori nothing."""
    words = code.words
    for i, v in enumerate(code.vals):
        if not v or v.kind != "hi":
            continue
        reg, bits, ori = words[i] >> 16 & 31, v.bits, None
        for j in range(i + 1, min(i + 10, len(words))):
            w = words[j]
            if w >> 26 == ORI and w >> 21 & 31 == reg and w >> 16 & 31 == reg and ori is None:
                bits, ori = bits | (w & 0xFFFF), j
                continue
            if w >> 26 == COP1 and w >> 21 & 31 == 4 and w >> 16 & 31 == reg:
                code.vals[i] = Val("f32", bits)
                if ori is not None:
                    code.vals[ori] = None
                break
            if writes(w) == reg:
                break


@dataclass
class Site:
    off: int
    pal: Val | None
    ntsc: Val | None
    cls: str
    index: int = 0  # the token in a


@dataclass
class Diff:
    name: str
    unit: str
    sites: list[Site] = field(default_factory=list)
    unaligned: int = 0  # instructions outside the aligned blocks, either side


def compare(a: Code, b: Code) -> Diff:
    """a's values against b's, over the instructions whose shapes line up (difflib) when the code differs."""
    d = Diff(a.name, a.unit)
    if a.shape == b.shape:
        pairs = [(i, i) for i in range(len(a.shape))]
    else:
        sm = difflib.SequenceMatcher(None, a.shape, b.shape, autojunk=False)
        pairs = [(m.a + k, m.b + k) for m in sm.get_matching_blocks() for k in range(m.size)]
        d.unaligned = len(a.shape) + len(b.shape) - 2 * len(pairs)
    for i, j in pairs:
        va, vb = a.vals[i], b.vals[j]
        if (va is None) != (vb is None) and (va or vb).kind in ("off", "hi", "imm", "frame"):
            continue  # an address part on one side only: the ELF's pairing and the relocations disagree
        if va != vb:
            d.sites.append(Site(a.offs[i], va, vb, classify(va, vb) if va and vb else "review", i))
    return d


class Elf:
    """A linked ELF's functions as tokens: lui/%lo and $gp pairs from declib.code_refs."""

    def __init__(self, path: Path):
        self.sections, self.symbols, _ = read_elf(path.read_bytes())
        self.by_name = {s["name"]: s for s in self.sections}
        text = self.by_name[".text"]
        self.text = text
        reginfo = self.by_name[".reginfo"]["data"]
        self.gp = struct.unpack_from("<I", reginfo, 20)[0]
        self.funcs = sorted((s for s in self.symbols if s["type"] == FUNC and s["size"]), key=lambda s: s["value"])
        self.objects = sorted((s["value"], s["value"] + s["size"], s["name"]) for s in self.symbols
                              if s["type"] == OBJECT and s["size"])
        refs = code_refs(text["data"], text["addr"], [f["value"] for f in self.funcs], self.gp)
        self.lo = {r.vram: r for r in refs}
        self.hi = {r.hi_vram for r in refs if r.hi_vram is not None}
        self.end = max(s["addr"] + s["size"] for s in self.sections if s["flags"] & 2)

    def section_at(self, addr: int) -> dict | None:
        return next((s for s in self.sections if s["flags"] & 2 and s["addr"] <= addr < s["addr"] + s["size"]
                     and s["data"]), None)

    def named(self, addr: int) -> bool:
        i = bisect.bisect_right(self.objects, (addr, 1 << 32, "")) - 1
        return i >= 0 and self.objects[i][0] <= addr < self.objects[i][1]

    def read(self, addr: int, n: int) -> bytes | None:
        s = self.section_at(addr)
        if not s or addr + n > s["addr"] + s["size"]:
            return None
        return s["data"][addr - s["addr"]:addr - s["addr"] + n]

    def literal(self, addr: int, width: int, op: int) -> Val | None:
        s = self.section_at(addr)
        if not s or width == 0:
            return None
        if s["name"] not in LITERAL_SECTIONS and (s["name"] != ".rodata" or self.named(addr)):
            return None
        raw, ctx = self.read(addr, 4), self.read(addr & ~7, 8) or b""
        if raw is None:
            return None
        if op == LDC1:
            raw = self.read(addr, 8) or b""
            return Val("f64", int.from_bytes(raw, "little"), raw)
        return Val("f32" if op == LWC1 else "word", int.from_bytes(raw, "little"), ctx)

    def code(self, f: dict, unit: str = "") -> Code:
        base = f["value"] - self.text["addr"]
        raw = self.text["data"][base:base + f["size"]]
        words = list(struct.unpack(f"<{len(raw) // 4}I", raw))
        mark: dict[int, Val | None] = {}
        for i, w in enumerate(words):
            vram = f["value"] + 4 * i
            r = self.lo.get(vram)
            if r is not None:
                mark[i] = self.literal(r.address, r.width, w >> 26)
            elif vram in self.hi:
                mark[i] = None
        code = tokenize(Code(f["name"], unit, f["value"], words), mark)
        for i, v in enumerate(code.vals):
            if v and v.kind == "hi" and 0x100000 <= v.bits < self.end:
                code.vals[i] = None  # a lui of an address declib did not pair
        return code


class Obj:
    """A relocatable object's functions as tokens: relocations say what is an address and what a literal. A
    literal the unit borrows from retail (an undefined D_<address>) is read from elf."""

    def __init__(self, path: Path, unit: str, elf: Elf):
        self.unit, self.elf = unit, elf
        self.sections, self.symbols, relocs = read_elf(path.read_bytes())
        self.text = next(s for s in self.sections if s["name"] == ".text")
        reginfo = next((s["data"] for s in self.sections if s["name"] == ".reginfo"), b"")
        self.gp0 = struct.unpack_from("<I", reginfo, 20)[0] if len(reginfo) >= 24 else 0
        self.relocs = {off: (t, sym) for off, t, sym, _ in relocs.get(self.text["index"], [])}
        self.funcs = [s for s in self.symbols if s["type"] == FUNC and s["shndx"] == self.text["index"]
                      and s["size"]]
        self.objects = {}
        for s in self.symbols:
            if s["type"] == OBJECT and s["size"]:
                self.objects.setdefault(s["shndx"], []).append((s["value"], s["value"] + s["size"]))

    def literal(self, shndx: int, off: int, width: int, op: int) -> Val | None:
        s = self.sections[shndx]
        if width == 0 or (s["name"] not in LITERAL_SECTIONS and (
                s["name"] != ".rodata" or any(lo <= off < hi for lo, hi in self.objects.get(shndx, [])))):
            return None
        data = s["data"]
        ctx = data[off & ~7:(off & ~7) + 8]
        if op == LDC1:
            return Val("f64", int.from_bytes(data[off:off + 8], "little"), data[off:off + 8])
        return Val("f32" if op == LWC1 else "word", int.from_bytes(data[off:off + 4], "little"), ctx)

    def code(self, f: dict) -> Code:
        base = f["value"]
        raw = self.text["data"][base:base + f["size"]]
        words = list(struct.unpack(f"<{len(raw) // 4}I", raw))
        mark: dict[int, Val | None] = {}
        hi: dict[int, int] = {}
        for i, w in enumerate(words):
            t, sym = self.relocs.get(base + 4 * i, (None, None))
            if t is None:
                continue
            op = w >> 26
            width = LOADS[op][1] if op in LOADS else 0
            s = self.symbols[sym]
            borrowed = s["shndx"] == 0 and re.fullmatch(r"D_[0-9A-Fa-f]{8}", s["name"])
            if borrowed and t in (R_MIPS_LO16, R_MIPS_GPREL16, R_MIPS_LITERAL):
                addr = int(s["name"][2:], 16) + (sext16(w & 0xFFFF) if t != R_MIPS_LO16 else 0)
                mark[i] = self.elf.literal(addr, width, op)
            elif t == R_MIPS_HI16:
                hi[sym] = (w & 0xFFFF) << 16
                mark[i] = None
            elif t == R_MIPS_LO16:
                off = s["value"] + hi.get(sym, 0) + sext16(w & 0xFFFF)
                mark[i] = self.literal(s["shndx"], off, width, op) if s["shndx"] < len(self.sections) else None
            elif t in (R_MIPS_GPREL16, R_MIPS_LITERAL):
                off = s["value"] + sext16(w & 0xFFFF) + self.gp0
                mark[i] = self.literal(s["shndx"], off, width, op) if s["shndx"] < len(self.sections) else None
            else:
                mark[i] = None
        name = re.sub(r"__[0-9a-f]{8}$", "", f["name"])
        return tokenize(Code(name, self.unit, base, words), mark)


def unit_of(decomp: Path) -> dict[int, str]:
    """PAL function address -> its decomp unit."""
    return {f.address: u.name for u in nwunits.load_registries(decomp, VERSION) for f in u.functions}


def pairs(pal: Elf, us: Elf) -> tuple[list[tuple[dict, dict]], list[str], list[str]]:
    """Functions of both ELFs by name (a repeated name by its order), and those only one has."""
    def by_name(elf: Elf) -> dict[str, list[dict]]:
        out: dict[str, list[dict]] = {}
        for f in elf.funcs:
            out.setdefault(f["name"], []).append(f)
        return out
    a, b = by_name(pal), by_name(us)
    both = [(x, y) for name in a if name in b for x, y in zip(a[name], b[name])]
    return both, sorted(set(a) - set(b)), sorted(set(b) - set(a))


def disasm(words: list[int], vram: int) -> list[str]:
    try:
        import rabbitizer
    except ImportError:
        return [f"{vram + 4 * i:08x}: {w:08x}" for i, w in enumerate(words)]
    return [f"{vram + 4 * i:08x}: {rabbitizer.Instruction(w, vram + 4 * i).disassemble()}"
            for i, w in enumerate(words)]


def unvram(lines: list[str]) -> list[str]:
    """Listing lines without their address, call targets and gp offsets: what differs then is the code."""
    out = []
    for line in lines:
        text = line.split(": ", 1)[1]
        text = re.sub(r"func_[0-9A-F]+", "func", text)
        out.append(re.sub(r"-?0x[0-9A-F]+\(\$gp\)", "gp", text))
    return out


def report(d: Diff, known: Known, out: list[str], counts: Counter) -> bool:
    """d's lines on out, known parts only counted; True when something unknown is left."""
    left = False
    head = f"{d.unit or '?'} {d.name}:"
    lines = []
    if d.unaligned:
        why = known.reason(f"func:{d.unit}/{d.name}")
        counts["structure known" if why else "structure"] += 1
        if not why:
            lines.append(f"  structure: {d.unaligned} instructions differ")
            left = True
    for s in d.sites:
        site = f"func:{d.unit}/{d.name}+0x{s.off:x}"
        s.cls = known.classify(site, s.cls)
        why = known.reason(site)
        counts[f"{s.cls} known" if why else s.cls] += 1
        if why:
            continue
        left = True
        pal, ntsc = s.pal.show() if s.pal else "-", s.ntsc.show() if s.ntsc else "-"
        lines.append(f"  +0x{s.off:04x} {s.pal.kind if s.pal else '?':5} {pal} -> {ntsc}  {s.cls}")
    if lines:
        out.append(head)
        out.extend(lines)
    return left


def elf_arg(value: str | None, env: str, what: str) -> Path:
    path = value or os.environ.get(env)
    if not path:
        sys.exit(f"error: the {what} ELF: --{what.lower()} or {env}")
    return Path(path)


def cmd_functions(args) -> int:
    pal, us = Elf(elf_arg(args.pal, "NUWORLD_PAL_ELF", "PAL")), Elf(elf_arg(args.us, "NUWORLD_US_ELF", "US"))
    units = unit_of(args.decomp)
    both, pal_only, us_only = pairs(pal, us)
    if args.disasm:
        for fa, fb in both:
            if fa["name"] == args.disasm:
                a, b = (unvram(disasm(elf.code(f).words, f["value"])) for elf, f in ((pal, fa), (us, fb)))
                for line in difflib.unified_diff(a, b, "PAL", "US", lineterm="", n=3):
                    print(line)
        return 0
    known, counts, out = Known(args.known), Counter(), []
    for fa, fb in both:
        d = compare(pal.code(fa, units.get(fa["value"], "")), us.code(fb))
        if d.sites or d.unaligned:
            report(d, known, out, counts)
    print("\n".join(out))
    print(f"\nfunctions: {len(both)} in both, PAL only {', '.join(pal_only) or '-'}, US only {', '.join(us_only) or '-'}")
    print("sites: " + ", ".join(f"{n} {k}" for k, n in sorted(counts.items())))
    return 0


# -- data ----------------------------------------------------------------------------------------------------------

@dataclass
class Field:
    """One differing value of a data symbol (a float, or an aligned int of 1, 2 or 4 bytes) at off; addr is the
    symbol's PAL address."""
    symbol: str
    off: int
    size: int
    kind: str  # f32 or int
    pal: int
    ntsc: int
    cls: str
    addr: int = 0

    def show(self, v: int) -> str:
        return f"{f32(v):.9g}" if self.kind == "f32" else str(v)


def data_fields(name: str, a: bytes, b: bytes, pal_end: int, us_end: int) -> list[Field]:
    """Per differing word: a float, else the widest aligned unit holding the changed bytes whose values are a rate
    (word, halfword, then each byte), else the narrowest one, for review."""
    out = []
    for off in range(0, len(a) - len(a) % 4, 4):
        x, y = struct.unpack_from("<I", a, off)[0], struct.unpack_from("<I", b, off)[0]
        if x == y or (0x100000 <= x <= pal_end and 0x100000 <= y <= us_end):
            continue  # equal, or a pointer in each
        if plausible_f32(x) and plausible_f32(y):
            out.append(Field(name, off, 4, "f32", x, y, "rate" if rate_float(f32(x), f32(y)) else "review"))
            continue
        changed = [k for k in range(4) if (x >> 8 * k) & 0xFF != (y >> 8 * k) & 0xFF]

        def unit(start: int, size: int) -> tuple[int, int]:
            """Signed when either value has its top bit set: a negative count or step."""
            mask, top = (1 << 8 * size) - 1, 1 << (8 * size - 1)
            u, v = x >> 8 * start & mask, y >> 8 * start & mask
            if size > 1 and (u | v) & top:
                u, v = u - (u & top) * 2, v - (v & top) * 2
            return u, v

        halves = sorted({k & ~1 for k in changed})
        if rate_int(*unit(0, 4)):
            out.append(Field(name, off, 4, "int", *unit(0, 4), "rate"))
        elif all(rate_int(*unit(h, 2)) for h in halves):
            out.extend(Field(name, off + h, 2, "int", *unit(h, 2), "rate") for h in halves)
        elif all(rate_int(*unit(k, 1)) for k in changed):
            out.extend(Field(name, off + k, 1, "int", *unit(k, 1), "rate") for k in changed)
        else:
            lo, hi = changed[0], changed[-1]
            size = 1 if lo == hi else 2 if lo // 2 == hi // 2 else 4
            start = lo & ~(size - 1)
            out.append(Field(name, off + start, size, "int", *unit(start, size), "review"))
    return out


def data_diff(pal: Elf, us: Elf) -> tuple[list[Field], list[str], int]:
    """Fields of same-named, same-sized data symbols that differ (a repeated name paired in address order, the
    second as name#1); the symbols whose size differs; the names left out because the ELFs repeat them unequally."""
    def objs(elf: Elf) -> dict[str, list[dict]]:
        out: dict[str, list[dict]] = {}
        for s in elf.symbols:
            sec = elf.sections[s["shndx"]]["name"] if s["shndx"] < len(elf.sections) else ""
            if s["type"] == OBJECT and s["size"] and sec in DATA_SECTIONS:
                out.setdefault(s["name"], []).append(s)
        return out
    a, b = objs(pal), objs(us)
    fields, sized, repeated = [], [], 0
    for name in sorted(a, key=lambda n: a[n][0]["value"]):
        if name not in b:
            continue
        if len(a[name]) != len(b[name]):
            repeated += 1
            continue
        xs, ys = (sorted(v[name], key=lambda s: s["value"]) for v in (a, b))
        for i, (x, y) in enumerate(zip(xs, ys)):
            what = f"{name}#{i}" if i else name
            ra, rb = pal.read(x["value"], x["size"]), us.read(y["value"], y["size"])
            if ra is None or rb is None or ra == rb:
                continue
            if x["size"] == y["size"]:
                pairs = [(0, ra, rb)]
            elif name.startswith("SfxTab") and x["size"] % SOUND == y["size"] % SOUND == 0:
                pairs = sound_pairs(ra, rb)
            else:
                sized.append(f"{what} ({x['size']} -> {y['size']} bytes)")
                continue
            for base, ea, eb in pairs:
                for f in data_fields(what, ea, eb, pal.end, us.end):
                    f.off += base
                    f.addr = x["value"]
                    fields.append(f)
    return fields, sized, repeated


SOUND = 0x30  # a GameSound: its name first


def sound_pairs(a: bytes, b: bytes) -> list[tuple[int, bytes, bytes]]:
    """A sound table PAL extended, entry by entry under the same name: (PAL offset, PAL entry, US entry)."""
    names = {}
    for off in range(0, len(b), SOUND):
        names.setdefault(b[off:off + 16].split(b"\0")[0], b[off:off + SOUND])
    return [(off, a[off:off + SOUND], names[a[off:off + 16].split(b"\0")[0]]) for off in range(0, len(a), SOUND)
            if a[off:off + 16].split(b"\0")[0] in names]


def cmd_data(args) -> int:
    pal, us = Elf(elf_arg(args.pal, "NUWORLD_PAL_ELF", "PAL")), Elf(elf_arg(args.us, "NUWORLD_US_ELF", "US"))
    fields, sized, repeated = data_diff(pal, us)
    known, counts = Known(args.known), Counter()
    for f in fields:
        name = f"data:{f.symbol}+0x{f.off:x}"
        f.cls = known.classify(name, f.cls)
        why = known.reason(name) if f.cls == "review" else None
        counts[f"{f.cls} known" if why else f.cls] += 1
        if not why:
            print(f"{f.symbol}+0x{f.off:x} {f.kind}{f.size * 8 if f.kind == 'int' else ''} "
                  f"{f.show(f.pal)} -> {f.show(f.ntsc)}  {f.cls}")
    for s in sized:
        why = known.reason(f"data:{s.split()[0]}+size")
        counts["size known" if why else "size"] += 1
        if not why:
            print(f"size: {s}")
    print(f"\nfields: " + ", ".join(f"{n} {k}" for k, n in sorted(counts.items())) +
          f"; {repeated} repeated names left out")
    return 0


TABLE = ROOT / "src" / "port" / "hz_table.inc"


def c_float(bits: int) -> str:
    """The shortest C float literal of exactly these bits."""
    v = f32(bits)
    for prec in range(1, 10):
        text = f"{v:.{prec}g}"
        if struct.unpack("<I", struct.pack("<f", float(text)))[0] == bits & 0xFFFFFFFF:
            break
    return (text if any(c in text for c in ".e") else text + ".0") + "f"


def table_lines(fields: list[Field]) -> list[str]:
    """One NW_HZ_F32 or NW_HZ_INT per rate field; an int narrowed to its changed bytes (1, 2 or 4)."""
    out = []
    for f in fields:
        if f.kind == "f32":
            out.append(f'NW_HZ_F32("{f.symbol}+0x{f.off:x}", 0x{f.addr + f.off:08x}, {c_float(f.pal)}, '
                       f'{c_float(f.ntsc)})')
            continue
        mask = (1 << 8 * f.size) - 1
        pb, nb = (f.pal & mask).to_bytes(f.size, "little"), (f.ntsc & mask).to_bytes(f.size, "little")
        changed = [k for k, (x, y) in enumerate(zip(pb, nb)) if x != y]
        lo, hi = changed[0], changed[-1]
        size = 1 if lo == hi else 2 if lo // 2 == hi // 2 else 4
        start = lo & ~(size - 1)
        pv, nv = (int.from_bytes(raw[start:start + size], "little") for raw in (pb, nb))
        out.append(f'NW_HZ_INT("{f.symbol}+0x{f.off + start:x}", 0x{f.addr + f.off + start:08x}, {size}, {pv}, {nv})')
    return out


def cmd_table(args) -> int:
    pal, us = Elf(elf_arg(args.pal, "NUWORLD_PAL_ELF", "PAL")), Elf(elf_arg(args.us, "NUWORLD_US_ELF", "US"))
    fields, _, _ = data_diff(pal, us)
    known = Known(args.known)
    rate = [f for f in fields if known.classify(f"data:{f.symbol}+0x{f.off:x}", f.cls) == "rate"]
    text = "\n".join(["/* Generated by tools/ntsc_diff.py table: the data NTSC's 60 Hz changes, as a name, its PAL address, "
                      "the PAL",
                      "   value and the NTSC one (an int narrowed to the bytes that change). src/port/hz.c writes each "
                      "where the PAL",
                      "   value is. */",
                      *table_lines(rate), ""])
    if args.check:
        same = args.out.is_file() and args.out.read_text(encoding="utf-8") == text
        print(f"{args.out}: {'current' if same else 'differs from a fresh table'} ({len(rate)} entries)")
        return 0 if same else 1
    args.out.write_text(text, encoding="utf-8")
    print(f"{args.out}: {len(rate)} entries")
    return 0


# -- check: the decomp built for EE with either value of NW_HZ -------------------------------------------------------

def in_container() -> bool:
    return Path("/.dockerenv").exists()


def cmd_compile(args) -> int:
    """In the decomp image: every unit, PAL (no define) and NTSC (-DNW_HZ_NTSC), into --out."""
    sys.path.insert(0, str(args.decomp / "tools"))
    import cc
    for unit in args.units:
        src = args.decomp / "src" / f"{unit}.c"
        for mode, extra in (("pal", []), ("ntsc", ["-DNW_HZ_NTSC"])):
            dest = args.out / mode / f"{unit}.o"
            try:
                cc._run_driver(src, "out.o", ["-c", *extra], dest, "default", VERSION)
            except subprocess.CalledProcessError as e:
                sys.exit(f"{unit} ({mode}): {e.stderr}")
    return 0


def compile_units(decomp: Path, units: list[str]) -> None:
    out = OUT
    out.mkdir(parents=True, exist_ok=True)
    if in_container():
        args = argparse.Namespace(decomp=decomp, out=out, units=units)
        cmd_compile(args)
        return
    cmd = ["docker", "run", "--rm", "-v", f"{ROOT}:/nw", "-v", f"{decomp.resolve()}:/decomp", "-w", "/nw", IMAGE,
           "python3", "tools/ntsc_diff.py", "compile", "--decomp", "/decomp", "--out", "build/ntsc_diff", *units]
    if subprocess.run(cmd, stdin=subprocess.DEVNULL).returncode:
        sys.exit("error: the EE compile failed")


def cmd_check(args) -> int:
    pal, us = Elf(elf_arg(args.pal, "NUWORLD_PAL_ELF", "PAL")), Elf(elf_arg(args.us, "NUWORLD_US_ELF", "US"))
    units = unit_of(args.decomp)
    model = nwunits.load_registries(args.decomp, VERSION)
    nwunits.load_states(args.decomp, VERSION, model, [])
    matching = {(u.name, f.name) for u in model for f in u.functions if f.state == "matching"}
    both, _, _ = pairs(pal, us)
    differ = set()
    for fa, fb in both:
        d = compare(pal.code(fa), us.code(fb))
        if d.sites or d.unaligned:
            differ.add(units.get(fa["value"], ""))
    todo = args.units or [u for u in sorted(differ) if u and (args.decomp / "src" / f"{u}.c").is_file()]
    if not args.no_compile:
        compile_units(args.decomp, todo)
    pal_by, us_by = {}, {}  # by unit and name: a few statics share a name
    for fa, fb in both:
        key = (units.get(fa["value"], ""), fa["name"])
        pal_by[key], us_by[key] = fa, fb
    known, counts, out, self_test = Known(args.known), Counter(), [], []
    left = False
    for unit in todo:
        objs = {mode: Obj(OUT / mode / f"{unit}.o", unit, pal) for mode in ("pal", "ntsc")}
        for f in objs["pal"].funcs:
            code = objs["pal"].code(f)
            if (unit, code.name) not in pal_by:
                continue
            d = compare(code, pal.code(pal_by[unit, code.name]))
            if (d.sites or d.unaligned) and (unit, code.name) in matching:
                self_test.append(f"{unit} {code.name}: {len(d.sites)} values, {d.unaligned} instructions")
        built = {f["name"]: objs["pal"].code(f) for f in objs["pal"].funcs}
        for f in objs["ntsc"].funcs:
            code = objs["ntsc"].code(f)
            if (unit, code.name) not in us_by:
                continue
            d = compare(code, us.code(us_by[unit, code.name]))
            base = built.get(f["name"])
            if base is not None and base.shape == code.shape:
                for s in d.sites:
                    was = base.vals[s.index]
                    if was != s.pal:  # NW_HZ changed it
                        s.cls = "over" if was == s.ntsc else "wrong"
            if d.sites or d.unaligned:
                left |= report(d, known, out, counts)
    print("\n".join(out))
    if self_test:
        print("\nPAL build vs PAL ELF (must be equal):\n  " + "\n  ".join(self_test))
    print(f"\n{len(todo)} units; NTSC build vs US ELF: " +
          (", ".join(f"{n} {k}" for k, n in sorted(counts.items())) or "equal"))
    return 1 if left or self_test else 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="cmd", required=True)
    for name in ("functions", "data", "check", "table"):
        p = sub.add_parser(name)
        p.add_argument("--pal", help="the PAL ELF (default $NUWORLD_PAL_ELF)")
        p.add_argument("--us", help="the US ELF (default $NUWORLD_US_ELF)")
        p.add_argument("--known", type=Path, default=KNOWN, help=f"differences that are no rate (default {KNOWN})")
        p.add_argument("--decomp", type=Path, default=DECOMP, help="the decomp checkout (default the submodule)")
    sub.choices["functions"].add_argument("--disasm", metavar="NAME", help="a function's PAL and US code side by side")
    sub.choices["check"].add_argument("--units", nargs="*", help="these decomp units (default: every differing one)")
    sub.choices["check"].add_argument("--no-compile", action="store_true", help="reuse build/ntsc_diff's objects")
    sub.choices["table"].add_argument("--out", type=Path, default=TABLE, help=f"the table (default {TABLE})")
    sub.choices["table"].add_argument("--check", action="store_true", help="only compare it with a fresh one")
    p = sub.add_parser("compile", help=argparse.SUPPRESS)
    p.add_argument("--decomp", type=Path, required=True)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("units", nargs="+")
    args = parser.parse_args()
    return {"functions": cmd_functions, "data": cmd_data, "check": cmd_check, "table": cmd_table,
            "compile": cmd_compile}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main())
