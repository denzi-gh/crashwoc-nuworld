#!/usr/bin/env python3
"""Disassemble the VU1 microcode of the boot ELF: packages, MPG chunks, rebuilt sources and listings."""
from __future__ import annotations

import argparse
import json
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BOOT_ELF = Path("disc") / "SLES_503.86"     # <data root>/disc/ (src/vfs/extract.h)

SHT_SYMTAB, SHT_NOBITS = 2, 8
STT_OBJECT_GLOBAL = 0x11
N_SLINE, N_SO, N_SOL = 0x44, 0x64, 0x84
VU1_MEM = 0x4000
DMA_IDS = ("refe", "cnt", "next", "ref", "refs", "call", "ret", "end")
XYZW = "xyzw"


class GenError(Exception):
    pass


def cstr(data: bytes, off: int) -> str:
    end = data.find(b"\0", off)
    return data[off:end if end >= 0 else len(data)].decode("latin-1")


def basename(path: str) -> str:
    return path.replace("\\", "/").rsplit("/", 1)[-1]


# --- ELF sections and symbols ---------------------------------------------------------------------------------

@dataclass
class Section:
    index: int
    name: str
    type: int
    addr: int
    data: bytes
    link: int


class Elf:
    def __init__(self, raw: bytes):
        if raw[:4] != b"\x7fELF" or raw[4:6] != b"\x01\x01":
            raise GenError("not a 32-bit little-endian ELF")
        shoff, = struct.unpack_from("<I", raw, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from("<HHH", raw, 0x2E)
        heads = [struct.unpack_from("<10I", raw, shoff + i * shentsize) for i in range(shnum)]

        def body(h):
            return b"" if h[1] == SHT_NOBITS else raw[h[4]:h[4] + h[5]]
        names = body(heads[shstrndx])
        self.sections = [Section(i, cstr(names, h[0]), h[1], h[3], body(h), h[6]) for i, h in enumerate(heads)]
        self.by_name = {s.name: s for s in self.sections}

    def section(self, name: str) -> Section:
        if name not in self.by_name:
            raise GenError(f"the ELF has no {name}")
        return self.by_name[name]

    def symbols(self):
        """(name, value, info, section index) of every .symtab entry."""
        tab = next((s for s in self.sections if s.type == SHT_SYMTAB), None)
        if tab is None:
            raise GenError("the ELF has no .symtab")
        strs = self.sections[tab.link].data
        for off in range(0, len(tab.data) - 15, 16):
            name, value, _size, info, _other, shndx = struct.unpack_from("<IIIBBH", tab.data, off)
            yield cstr(strs, name), value, info, shndx


# --- VU instructions ------------------------------------------------------------------------------------------

DEST, FT, FS, FD, FSF, FTF, IMM11 = 0x01E00000, 0x001F0000, 0x0000F800, 0x000007C0, 0x00600000, 0x01800000, 0x7FF

UPPER: dict[int, tuple[str, str]] = {}
for _i, _m in enumerate(("ADD", "SUB", "MADD", "MSUB", "MAX", "MINI", "MUL")):
    for _b, _c in enumerate(XYZW):
        UPPER[_i * 4 + _b] = (_m + _c, "bc")
UPPER.update({0x1C: ("MULq", "q"), 0x1D: ("MAXi", "i"), 0x1E: ("MULi", "i"), 0x1F: ("MINIi", "i"),
              0x20: ("ADDq", "q"), 0x21: ("MADDq", "q"), 0x22: ("ADDi", "i"), 0x23: ("MADDi", "i"),
              0x24: ("SUBq", "q"), 0x25: ("MSUBq", "q"), 0x26: ("SUBi", "i"), 0x27: ("MSUBi", "i"),
              0x28: ("ADD", "fd"), 0x29: ("MADD", "fd"), 0x2A: ("MUL", "fd"), 0x2B: ("MAX", "fd"),
              0x2C: ("SUB", "fd"), 0x2D: ("MSUB", "fd"), 0x2E: ("OPMSUB", "fd"), 0x2F: ("MINI", "fd")})

# Upper opcodes 0x3C-0x3F, keyed by bits 10..6 and 1..0.
UPPER_SPECIAL: dict[int, tuple[str, str]] = {}
for _i, _m in enumerate(("ADDA", "SUBA", "MADDA", "MSUBA")):
    for _b, _c in enumerate(XYZW):
        UPPER_SPECIAL[_i * 4 + _b] = (_m + _c, "abc")
for _i, _m in enumerate(("ITOF0", "ITOF4", "ITOF12", "ITOF15", "FTOI0", "FTOI4", "FTOI12", "FTOI15")):
    UPPER_SPECIAL[0x10 + _i] = (_m, "ftfs")
for _b, _c in enumerate(XYZW):
    UPPER_SPECIAL[0x18 + _b] = ("MULA" + _c, "abc")
UPPER_SPECIAL.update({0x1C: ("MULAq", "aq"), 0x1D: ("ABS", "ftfs"), 0x1E: ("MULAi", "ai"), 0x1F: ("CLIPw", "clip"),
                      0x20: ("ADDAq", "aq"), 0x21: ("MADDAq", "aq"), 0x22: ("ADDAi", "ai"), 0x23: ("MADDAi", "ai"),
                      0x24: ("SUBAq", "aq"), 0x25: ("MSUBAq", "aq"), 0x26: ("SUBAi", "ai"), 0x27: ("MSUBAi", "ai"),
                      0x28: ("ADDA", "a"), 0x29: ("MADDA", "a"), 0x2A: ("MULA", "a"), 0x2C: ("SUBA", "a"),
                      0x2D: ("MSUBA", "a"), 0x2E: ("OPMULA", "a"), 0x2F: ("NOP", "nop")})

# Lower: opcode bits 31..25; 0x40 continues in LOWER_OP (bits 5..0), whose 0x3C-0x3F continue in LOWER_SPECIAL
# (bits 10..6 and 1..0). The form names the operands and, through LOWER_FIELDS, the bits they use.
LOWER = {0x00: ("LQ", "lq"), 0x01: ("SQ", "sq"), 0x04: ("ILW", "ilw"), 0x05: ("ISW", "ilw"),
         0x08: ("IADDIU", "imm15"), 0x09: ("ISUBIU", "imm15"),
         0x10: ("FCEQ", "fc1"), 0x11: ("FCSET", "fc0"), 0x12: ("FCAND", "fc1"), 0x13: ("FCOR", "fc1"),
         0x14: ("FSEQ", "fs1"), 0x15: ("FSSET", "fs0"), 0x16: ("FSAND", "fs1"), 0x17: ("FSOR", "fs1"),
         0x18: ("FMEQ", "itis"), 0x1A: ("FMAND", "itis"), 0x1B: ("FMOR", "itis"), 0x1C: ("FCGET", "it"),
         0x20: ("B", "b"), 0x21: ("BAL", "bal"), 0x24: ("JR", "is"), 0x25: ("JALR", "itis"),
         0x28: ("IBEQ", "bitis"), 0x29: ("IBNE", "bitis"), 0x2C: ("IBLTZ", "bis"), 0x2D: ("IBGTZ", "bis"),
         0x2E: ("IBLEZ", "bis"), 0x2F: ("IBGEZ", "bis")}
LOWER_OP = {0x30: ("IADD", "3i"), 0x31: ("ISUB", "3i"), 0x32: ("IADDI", "imm5"), 0x34: ("IAND", "3i"),
            0x35: ("IOR", "3i")}
LOWER_SPECIAL = {
    0x0C << 2 | 0: ("MOVE", "ftfs"), 0x0C << 2 | 1: ("MR32", "ftfs"),
    0x0D << 2 | 0: ("LQI", "lqi"), 0x0D << 2 | 1: ("SQI", "sqi"), 0x0D << 2 | 2: ("LQD", "lqd"),
    0x0D << 2 | 3: ("SQD", "sqd"),
    0x0E << 2 | 0: ("DIV", "div"), 0x0E << 2 | 1: ("SQRT", "sqrt"), 0x0E << 2 | 2: ("RSQRT", "div"),
    0x0E << 2 | 3: ("WAITQ", "none"),
    0x0F << 2 | 0: ("MTIR", "mtir"), 0x0F << 2 | 1: ("MFIR", "mfir"), 0x0F << 2 | 2: ("ILWR", "ilwr"),
    0x0F << 2 | 3: ("ISWR", "ilwr"),
    0x10 << 2 | 0: ("RNEXT", "rft"), 0x10 << 2 | 1: ("RGET", "rft"), 0x10 << 2 | 2: ("RINIT", "rfs"),
    0x10 << 2 | 3: ("RXOR", "rfs"),
    0x19 << 2 | 0: ("MFP", "rft"),
    0x1A << 2 | 0: ("XTOP", "it"), 0x1A << 2 | 1: ("XITOP", "it"), 0x1B << 2 | 0: ("XGKICK", "is"),
    0x1C << 2 | 0: ("ESADD", "p"), 0x1C << 2 | 1: ("ERSADD", "p"), 0x1C << 2 | 2: ("ELENG", "p"),
    0x1C << 2 | 3: ("ERLENG", "p"),
    0x1D << 2 | 0: ("EATANxy", "p"), 0x1D << 2 | 1: ("EATANxz", "p"), 0x1D << 2 | 2: ("ESUM", "p"),
    0x1E << 2 | 0: ("ESQRT", "pf"), 0x1E << 2 | 1: ("ERSQRT", "pf"), 0x1E << 2 | 2: ("ERCPR", "pf"),
    0x1E << 2 | 3: ("WAITP", "none"),
    0x1F << 2 | 0: ("ESIN", "pf"), 0x1F << 2 | 1: ("EATAN", "pf"), 0x1F << 2 | 2: ("EEXP", "pf"),
}
LOWER_FIELDS = {
    "lq": DEST | FT | FS | IMM11, "sq": DEST | FT | FS | IMM11, "ilw": DEST | FT | FS | IMM11,
    "imm15": DEST | FT | FS | IMM11, "fc1": 0x00FFFFFF, "fc0": 0x00FFFFFF, "fs1": FT | 0x00200000 | IMM11,
    "fs0": 0x00200000 | IMM11, "itis": FT | FS, "it": FT, "is": FS, "b": IMM11, "bal": FT | IMM11,
    "bitis": FT | FS | IMM11, "bis": FS | IMM11, "3i": FT | FS | FD, "imm5": FT | FS | FD,
    "ftfs": DEST | FT | FS, "lqi": DEST | FT | FS, "lqd": DEST | FT | FS, "sqi": DEST | FT | FS,
    "sqd": DEST | FT | FS, "div": FTF | FSF | FT | FS, "sqrt": FTF | FT, "mtir": FSF | FT | FS,
    "mfir": DEST | FT | FS, "ilwr": DEST | FT | FS, "rft": DEST | FT, "rfs": FSF | FS, "p": DEST | FS, "pf": FSF | FS,
    "none": 0,
}
LOWER_NOP = 0x8000033C      # MOVE with no dest


def vf(n: int) -> str:
    return f"VF{n:02d}"


def vi(n: int) -> str:
    return f"VI{n:02d}"


def bits(w: int, hi: int, lo: int) -> int:
    return (w >> lo) & ((1 << (hi - lo + 1)) - 1)


def sext(v: int, width: int) -> int:
    return v - (1 << width) if v >> (width - 1) & 1 else v


def num(v: int) -> str:
    return str(v) if -10 < v < 10 else f"{'-' if v < 0 else ''}0x{abs(v):x}"


def dest(w: int) -> str:
    d = "".join(c for i, c in enumerate(XYZW) if w >> (24 - i) & 1)
    return "." + d if d else ""


def f32(w: int) -> str:
    """The shortest text that reads back as the float32 w."""
    v = struct.unpack("<f", struct.pack("<I", w))[0]
    if v != v or v in (float("inf"), float("-inf")):
        return f"0x{w:08x}"
    for digits in range(1, 10):
        text = f"{v:.{digits}g}"
        if struct.unpack("<f", struct.pack("<f", float(text)))[0] == v:
            break
    return text if any(c in text for c in ".e") else text + ".0"


def upper(w: int) -> str | None:
    """Text of an upper instruction; None for an encoding no VU executes or one with stray bits."""
    op = w & 0x3F
    entry = UPPER_SPECIAL.get((w >> 4 & 0x7C) | (w & 3)) if op >= 0x3C else UPPER.get(op)
    if entry is None or w & 0x06000000:
        return None
    mn, form = entry
    ft, fs, fd = bits(w, 20, 16), bits(w, 15, 11), bits(w, 10, 6)
    if form in ("q", "i", "aq", "ai") and ft or form == "nop" and w & (DEST | FT | FS):
        return None
    flags = "".join(f"[{c}]" for b, c in ((30, "E"), (29, "M"), (28, "D"), (27, "T")) if w >> b & 1)
    bc = XYZW[w & 3]
    ops = {"bc": (vf(fd), vf(fs), vf(ft) + bc), "q": (vf(fd), vf(fs), "Q"), "i": (vf(fd), vf(fs), "I"),
           "fd": (vf(fd), vf(fs), vf(ft)), "abc": ("ACC", vf(fs), vf(ft) + bc), "aq": ("ACC", vf(fs), "Q"),
           "ai": ("ACC", vf(fs), "I"), "a": ("ACC", vf(fs), vf(ft)), "ftfs": (vf(ft), vf(fs)),
           "clip": (vf(fs), vf(ft) + "w"), "nop": ()}[form]
    name = mn + ("" if form == "nop" else dest(w)) + flags
    return f"{name} {', '.join(ops)}" if ops else name


def branch_target(w: int, pc: int) -> int:
    return (pc + 8 + sext(w & IMM11, 11) * 8) & (VU1_MEM - 1)


def lower(w: int, pc: int, label=lambda vu: f"0x{vu:04x}") -> str | None:
    """Text of a lower instruction at VU address pc; label(vu) names a branch target. None as for upper()."""
    if w == LOWER_NOP:
        return "NOP"
    op = w >> 25
    if op == 0x40:
        sub = w & 0x3F
        if sub >= 0x3C:
            entry, opmask = LOWER_SPECIAL.get((w >> 4 & 0x7C) | (w & 3)), 0xFE0007FF
        else:
            entry, opmask = LOWER_OP.get(sub), 0xFE00003F
    else:
        entry, opmask = LOWER.get(op), 0xFE000000
    if entry is None or w & ~(opmask | LOWER_FIELDS[entry[1]]) & 0xFFFFFFFF:
        return None
    mn, form = entry
    it, is_, id_ = bits(w, 20, 16), bits(w, 15, 11), bits(w, 10, 6)
    ft, fs = it, is_
    fsf, ftf = XYZW[bits(w, 22, 21)], XYZW[bits(w, 24, 23)]
    imm11 = sext(w & IMM11, 11)
    d = dest(w)
    target = label(branch_target(w, pc)) if form in ("b", "bal", "bitis", "bis") else ""
    text = {
        "lq": f"{mn}{d} {vf(ft)}, {num(imm11)}({vi(is_)})",
        "sq": f"{mn}{d} {vf(fs)}, {num(imm11)}({vi(it)})",
        "ilw": f"{mn}{d} {vi(it)}, {num(imm11)}({vi(is_)})",
        "imm15": f"{mn} {vi(it)}, {vi(is_)}, {num((w >> 10 & 0x7800) | (w & IMM11))}",
        "fc1": f"{mn} {vi(1)}, 0x{w & 0xFFFFFF:06x}",
        "fc0": f"{mn} 0x{w & 0xFFFFFF:06x}",
        "fs1": f"{mn} {vi(it)}, 0x{(w >> 10 & 0x800) | (w & IMM11):03x}",
        "fs0": f"{mn} 0x{(w >> 10 & 0x800) | (w & IMM11):03x}",
        "itis": f"{mn} {vi(it)}, {vi(is_)}",
        "it": f"{mn} {vi(it)}",
        "is": f"{mn} {vi(is_)}",
        "b": f"{mn} {target}",
        "bal": f"{mn} {vi(it)}, {target}",
        "bitis": f"{mn} {vi(it)}, {vi(is_)}, {target}",
        "bis": f"{mn} {vi(is_)}, {target}",
        "3i": f"{mn} {vi(id_)}, {vi(is_)}, {vi(it)}",
        "imm5": f"{mn} {vi(it)}, {vi(is_)}, {num(sext(id_, 5))}",
        "ftfs": f"{mn}{d} {vf(ft)}, {vf(fs)}",
        "lqi": f"{mn}{d} {vf(ft)}, ({vi(is_)}++)",
        "lqd": f"{mn}{d} {vf(ft)}, (--{vi(is_)})",
        "sqi": f"{mn}{d} {vf(fs)}, ({vi(it)}++)",
        "sqd": f"{mn}{d} {vf(fs)}, (--{vi(it)})",
        "div": f"{mn} Q, {vf(fs)}{fsf}, {vf(ft)}{ftf}",
        "sqrt": f"{mn} Q, {vf(ft)}{ftf}",
        "mtir": f"{mn} {vi(it)}, {vf(fs)}{fsf}",
        "mfir": f"{mn}{d} {vf(ft)}, {vi(is_)}",
        "ilwr": f"{mn}{d} {vi(it)}, ({vi(is_)})",
        "rft": f"{mn}{d} {vf(ft)}, {'P' if mn == 'MFP' else 'R'}",
        "rfs": f"{mn} R, {vf(fs)}{fsf}",
        "p": f"{mn} P, {vf(fs)}{d[1:]}",       # the field the EFU op reads (ESUM xyzw, ELENG xyz, ...)
        "pf": f"{mn} P, {vf(fs)}{fsf}",
        "none": mn,
    }[form]
    return text


def is_branch(lo: int, up: int) -> bool:
    return not up >> 31 and lo >> 25 in (0x20, 0x21, 0x28, 0x29, 0x2C, 0x2D, 0x2E, 0x2F)


def instruction(lo: int, up: int, pc: int, label=lambda vu: f"0x{vu:04x}") -> str | None:
    u = upper(up)
    lw = f"LOI {f32(lo)}" if up >> 31 else lower(lo, pc, label)
    return None if u is None or lw is None else f"{u:<31} {lw}"


# --- DMA tags and VIF codes -----------------------------------------------------------------------------------

VIF_NAMES = {0x00: "NOP", 0x01: "STCYCL", 0x02: "OFFSET", 0x03: "BASE", 0x04: "ITOP", 0x05: "STMOD",
             0x06: "MSKPATH3", 0x07: "MARK", 0x10: "FLUSHE", 0x11: "FLUSH", 0x13: "FLUSHA", 0x14: "MSCAL",
             0x15: "MSCALF", 0x17: "MSCNT", 0x20: "STMASK", 0x30: "STROW", 0x31: "STCOL", 0x4A: "MPG",
             0x50: "DIRECT", 0x51: "DIRECTHL"}


@dataclass
class Item:
    ee: int
    kind: str               # "dma", "vif" or "vu"
    words: tuple
    text: str | None = None
    vu: int | None = None   # instructions: VU1 address


@dataclass
class Chunk:
    name: str
    ee: int
    vu: int
    count: int


@dataclass
class Package:
    name: str
    ee: int
    end: int
    dsm: str = ""
    chunks: list[Chunk] = field(default_factory=list)
    items: list[Item] = field(default_factory=list)
    labels: dict[int, list[str]] = field(default_factory=dict)      # VU address -> names
    lines: dict[int, tuple[str, int]] = field(default_factory=dict)  # EE address -> (source path, line)

    def ee_to_vu(self, ee: int) -> int | None:
        for c in self.chunks:
            if c.ee <= ee <= c.ee + c.count * 8:
                return c.vu + ee - c.ee
        return None

    def vu_to_ee(self, vu: int) -> int | None:
        for c in self.chunks:
            if c.vu <= vu <= c.vu + c.count * 8:
                return c.ee + vu - c.vu
        return None

    def label(self, vu: int) -> str:
        """The last name at vu: a .dsm defines its entry labels before the code it includes defines its own."""
        names = self.labels.get(vu)
        return names[-1] if names else f"0x{vu:04x}"


def walk(pkg: Package, blob: bytes, base: int) -> list[tuple[int, int, int]]:
    """Items of pkg (DMA tags, VIF codes, instructions undecoded); returns its MPGs as (EE, VU, count)."""
    def u32(ee):
        return struct.unpack_from("<I", blob, ee - base)[0]
    mpgs = []
    pos = pkg.ee
    while pos < pkg.end:
        tag, addr = u32(pos), u32(pos + 4)
        tid, qwc = tag >> 28 & 7, tag & 0xFFFF
        stop = pos + 16 + qwc * 16
        if DMA_IDS[tid] not in ("cnt", "ret", "end") or stop > pkg.end:
            raise GenError(f"{pkg.name}: DMA tag 0x{tag:08x} at 0x{pos:x} does not fit the package")
        text = f"DMA{DMA_IDS[tid]} {num(qwc)}" + (f", 0x{addr:x}" if addr else "")
        pkg.items.append(Item(pos, "dma", (tag, addr), text))
        pos += 8
        while pos < stop:
            code = u32(pos)
            cmd, n, imm = code >> 24 & 0x7F, code >> 16 & 0xFF, code & 0xFFFF
            irq = "[i]" if code >> 31 else ""
            name = VIF_NAMES.get(cmd, "UNPACK" if cmd >= 0x60 else None)
            if name is None:
                raise GenError(f"{pkg.name}: unknown VIF code 0x{code:08x} at 0x{pos:x}")
            payload = {"STMASK": 1, "STROW": 4, "STCOL": 4}.get(name, 0)
            if name == "MPG":
                count = n or 256
                if (pos + 4) % 8 or pos + 4 + count * 8 > stop:
                    raise GenError(f"{pkg.name}: MPG at 0x{pos:x} is misaligned or overruns its DMA packet")
                pkg.items.append(Item(pos, "vif", (code,), f"MPG{irq} 0x{imm * 8:04x}, {count}"))
                mpgs.append((pos + 4, imm * 8, count))
                for k in range(count):
                    ee = pos + 4 + k * 8
                    pkg.items.append(Item(ee, "vu", (u32(ee + 4), u32(ee)), vu=(imm * 8 + k * 8) & (VU1_MEM - 1)))
                pos += 4 + count * 8
                continue
            if name in ("DIRECT", "DIRECTHL"):
                payload = (imm or 0x10000) * 4
            elif name == "UNPACK":
                payload = -(-(n or 256) * (32 >> (cmd & 3)) * ((cmd >> 2 & 3) + 1) // 32)
            if name in ("NOP", "FLUSHE", "FLUSH", "FLUSHA", "MSCNT"):
                text = name + irq
            elif name == "STCYCL":
                text = f"STCYCL{irq} {imm & 0xFF}, {imm >> 8}"
            elif name in ("MSCAL", "MSCALF"):
                text = f"{name}{irq} 0x{imm * 8:04x}"
            elif name == "UNPACK":
                mask = "m" if cmd & 0x10 else ""
                text = f"UNPACK{irq} V{(cmd >> 2 & 3) + 1}-{32 >> (cmd & 3)}{mask}, 0x{imm:x}, {n or 256}"
            elif name in ("DIRECT", "DIRECTHL"):
                text = f"{name}{irq} {imm or 0x10000}"
            else:
                text = f"{name}{irq} 0x{imm:x}"
            words = (code,) + tuple(u32(pos + 4 + 4 * k) for k in range(min(payload, 4)))
            pkg.items.append(Item(pos, "vif", words, text + "".join(f", 0x{w:08x}" for w in words[1:])))
            pos += 4 + payload * 4
        if pos != stop:
            raise GenError(f"{pkg.name}: VIF codes overrun the DMA packet ending at 0x{stop:x}")
    return mpgs


# --- Mapping: packages, chunks, labels, lines -----------------------------------------------------------------

@dataclass
class Microcode:
    packages: list[Package]
    errors: list[str]
    sources: dict[str, str] = field(default_factory=dict)            # basename -> path in .stab
    copies: dict[str, list[str]] = field(default_factory=dict)       # basename -> packages including it
    rebuilt: dict[str, dict[int, str]] = field(default_factory=dict)  # basename -> line -> text

    @property
    def chunk_count(self) -> int:
        return sum(len(p.chunks) for p in self.packages)

    def counts(self) -> tuple[int, int, int]:
        vsm = sum(1 for s in self.sources if s.lower().endswith(".vsm"))
        return len(self.packages), vsm, self.chunk_count


def read_stab(elf: Elf) -> list[tuple[int, int, int, str]]:
    """(type, desc, value, string) of each .stab entry; the header entries (type 0) only advance the strings."""
    stab = elf.section(".stab")
    strs = elf.sections[stab.link].data
    out, base, size = [], 0, 0
    for off in range(0, len(stab.data) - 11, 12):
        strx, typ, _other, desc, value = struct.unpack_from("<IBBHI", stab.data, off)
        if typ == 0:
            base, size = base + size, value
            continue
        out.append((typ, desc, value, cstr(strs, base + strx)))
    return out


def analyse(elf: Elf) -> Microcode:
    vutext = elf.section(".vutext")
    lo, hi = vutext.addr, vutext.addr + len(vutext.data)
    errors: list[str] = []
    syms = list(elf.symbols())
    starts = sorted((v, n) for n, v, info, sec in syms if sec == vutext.index and info == STT_OBJECT_GLOBAL)
    if not starts:
        raise GenError(".vutext has no package symbols")
    ends = [v for v, _n in starts[1:]] + [hi]
    packages = [Package(n, v, e) for (v, n), e in zip(starts, ends)]

    table = elf.section(".DVP.ovlytab")
    names = elf.sections[table.link]
    overlays = {}
    for off in range(0, len(table.data) - 11, 12):
        name_addr, lma, vma = struct.unpack_from("<3I", table.data, off)
        name = cstr(names.data, name_addr - names.addr)
        sec = elf.section(name)
        overlays[sec.index] = Chunk(name, lma, vma, len(sec.data) // 8)

    labels: dict[int, list[tuple[int, str]]] = {}
    ee_labels: list[tuple[int, str]] = []
    for name, value, _info, sec in syms:
        if sec in overlays and name and not name.startswith("__start_"):
            labels.setdefault(sec, []).append((value, name))
        elif sec == vutext.index and name.startswith("_$"):
            ee_labels.append((value, name[2:]))

    # Lines belong to the package of the N_SO before them; a .dsm's last line may sit at the package's end.
    by_ee = {p.ee: p for p in packages}
    pkg, current = None, ""
    for typ, desc, value, s in read_stab(elf):
        if typ == N_SO:
            pkg, current = by_ee.get(value), s
            if pkg is None or pkg.dsm:
                errors.append(f".stab: {basename(s)} at 0x{value:x} starts no package or a second time")
                pkg = None
            else:
                pkg.dsm = s
        elif typ == N_SOL:
            current = s
        elif typ == N_SLINE:
            where = f"{basename(current)}:{desc} at 0x{value:x}"
            if pkg is None or not pkg.ee <= value <= pkg.end:
                errors.append(f".stab: line {where} lies outside its package")
            elif value in pkg.lines:
                errors.append(f"{pkg.name}: line {where} shares its address")
            else:
                pkg.lines[value] = (current, desc)

    for pkg in packages:
        if not pkg.dsm:
            errors.append(f"{pkg.name}: no .stab N_SO")
        pkg.chunks = sorted((c for c in overlays.values() if pkg.ee <= c.ee < pkg.end), key=lambda c: c.ee)
        for sec, c in overlays.items():
            if pkg.ee <= c.ee < pkg.end:
                for value, name in labels.get(sec, ()):
                    pkg.labels.setdefault(value, []).append(name)
        mpgs = walk(pkg, vutext.data, lo)
        if mpgs != [(c.ee, c.vu, c.count) for c in pkg.chunks]:
            errors.append(f"{pkg.name}: the MPG codes do not match .DVP.ovlytab")
        starts_at = {i.ee for i in pkg.items} | {pkg.end}
        for ee, (path, line) in pkg.lines.items():
            if ee not in starts_at:
                errors.append(f"{pkg.name}: {basename(path)}:{line} at 0x{ee:x} starts no DMA tag, VIF code or "
                              "instruction")
        for item in pkg.items:
            if item.kind != "vu":
                continue
            up, lw = item.words
            item.text = instruction(lw, up, item.vu, pkg.label)
            if item.text is None:
                errors.append(f"{pkg.name}: unknown instruction 0x{up:08x} 0x{lw:08x} at 0x{item.ee:x}")
                item.text = f".word 0x{lw:08x}, 0x{up:08x}"
            elif is_branch(lw, up) and branch_target(lw, item.vu) not in pkg.labels:
                errors.append(f"{pkg.name}: branch at 0x{item.ee:x} to VU 0x{branch_target(lw, item.vu):04x} "
                              "has no label")
            if item.ee not in pkg.lines:
                errors.append(f"{pkg.name}: instruction at 0x{item.ee:x} has no .stab line")
        for vu, names_at in pkg.labels.items():
            if pkg.vu_to_ee(vu) is None:
                errors.append(f"{pkg.name}: label {names_at[0]} at VU 0x{vu:04x} is outside its chunks")
    for ee, name in ee_labels:
        if not any(name in p.labels.get(p.ee_to_vu(ee), ()) for p in packages if p.ee <= ee <= p.end):
            errors.append(f"_${name} at 0x{ee:x} has no VU label at the same place")
    return Microcode(packages, errors)


# --- Rebuilt sources ------------------------------------------------------------------------------------------

def rebuild(mc: Microcode) -> None:
    """Fills mc.sources/copies/rebuilt: each source file's lines from every package that includes it. Copies of a
    shared .vsm must agree line by line; a label only some copies have is the including .dsm's (listings only)."""
    labelled: dict[str, dict[int, list[str]]] = {}
    for pkg in mc.packages:
        bounds = sorted(pkg.lines) + [pkg.end]
        by_ee = {i.ee: k for k, i in enumerate(pkg.items)}
        copies: dict[str, dict[int, str]] = {}
        marks: dict[str, dict[int, list[str]]] = {}
        for ee, stop in zip(bounds, bounds[1:]):
            path, line = pkg.lines[ee]
            name = basename(path)
            if mc.sources.setdefault(name, path) != path:
                mc.errors.append(f"{name}: two sources share the name ({mc.sources[name]}, {path})")
            k = by_ee.get(ee)
            if k is None:
                continue
            span = []
            for item in pkg.items[k:]:
                if item.ee >= stop:
                    break
                if span and (item.kind == "vu") != (span[0].kind == "vu"):
                    if item.kind == "vu":
                        mc.errors.append(f"{pkg.name}: {name}:{line} mixes directives and instructions")
                    break       # an instruction line's span may run into the MPG of the next chunk
                span.append(item)
            if span[0].kind == "vu" and len(span) > 1:
                mc.errors.append(f"{pkg.name}: {name}:{line} holds {len(span)} instructions")
            copies.setdefault(name, {})[line] = " ; ".join(i.text for i in span)
        for vu, names in sorted(pkg.labels.items()):
            ee = pkg.vu_to_ee(vu)
            if ee in pkg.lines and ee < pkg.end:      # else after a chunk's last instruction: listings only
                path, line = pkg.lines[ee]
                marks.setdefault(basename(path), {})[line] = names
        for name, text in copies.items():
            mc.copies.setdefault(name, []).append(pkg.name)
            first = mc.rebuilt.setdefault(name, text)
            mark = marks.get(name, {})
            if first is text:
                labelled[name] = mark
                continue
            if first != text:
                diff = min(n for n in set(first) | set(text) if first.get(n) != text.get(n))
                mc.errors.append(f"{name}: {pkg.name} differs from {mc.copies[name][0]} at line {diff}: "
                                 f"{text.get(diff)!r} vs {first.get(diff)!r}")
            labelled[name] = {line: [n for n in names if n in mark.get(line, ())]
                              for line, names in labelled[name].items()}
    for name, mark in labelled.items():
        text = mc.rebuilt[name]
        for line, names in sorted(mark.items()):
            if not names:
                continue
            if line > 1 and line - 1 not in text:
                text[line - 1] = " ".join(n + ":" for n in names)
            else:
                text[line] = " ".join(n + ":" for n in names) + " " + text[line]


# --- Writers --------------------------------------------------------------------------------------------------

def source_text(path: str, text: dict[int, str]) -> str:
    out = [text.get(n, "") for n in range(1, max(text) + 1)]
    if not out[0]:
        out[0] = f"; {path} (rebuilt by tools/vu1dis.py at its .stab line numbers)"
    return "\n".join(out) + "\n"


def listing(pkg: Package) -> str:
    out = [f"; {pkg.name}  EE 0x{pkg.ee:x}-0x{pkg.end:x}  {pkg.dsm}"]
    for k, c in enumerate(pkg.chunks):
        out.append(f"; chunk {k}  EE 0x{c.ee:x}  VU 0x{c.vu:04x}  {c.count} instructions  {c.name}")
    out.append("")
    for item in pkg.items:
        if item.kind == "vu":
            for name in pkg.labels.get(item.vu, ()):
                out.append(f"{name}:")
        words = " ".join(f"{w:08x}" for w in item.words)
        vu = f"{item.vu:04x}" if item.vu is not None else "----"
        where = pkg.lines.get(item.ee)
        src = f"  ; {basename(where[0])}:{where[1]}" if where else ""
        out.append(f"{item.ee:08x} {vu}  {words:<26} {item.text}{src}")
    return "\n".join(out) + "\n"


def mapping(mc: Microcode) -> dict:
    return {
        "packages": [{
            "name": p.name, "ee": f"0x{p.ee:x}", "size": p.end - p.ee, "dsm": p.dsm,
            "sources": list(dict.fromkeys(basename(path) for path, _line in p.lines.values())),
            "chunks": [{"name": c.name, "ee": f"0x{c.ee:x}", "vu": f"0x{c.vu:04x}", "count": c.count}
                       for c in p.chunks],
            "labels": {n: f"0x{vu:04x}" for vu, names in sorted(p.labels.items()) for n in names},
        } for p in mc.packages],
        "sources": {n: {"path": mc.sources[n], "packages": mc.copies.get(n, [])} for n in sorted(mc.sources)},
    }


def write(mc: Microcode, out: Path) -> int:
    (out / "src").mkdir(parents=True, exist_ok=True)
    (out / "lst").mkdir(parents=True, exist_ok=True)
    files = 0
    for name, text in mc.rebuilt.items():
        (out / "src" / name).write_text(source_text(mc.sources[name], text), newline="\n")
        files += 1
    for pkg in mc.packages:
        (out / "lst" / f"{pkg.name}.lst").write_text(listing(pkg), newline="\n")
        files += 1
    (out / "vu1map.json").write_text(json.dumps(mapping(mc), indent=1) + "\n", newline="\n")
    return files + 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    src = parser.add_mutually_exclusive_group(required=True)
    src.add_argument("--elf", type=Path, help="the boot ELF")
    src.add_argument("--data-dir", type=Path, help=f"a data root with an imported disc (<root>/{BOOT_ELF.as_posix()})")
    parser.add_argument("--out", type=Path, default=REPO / "build" / "vu1")
    parser.add_argument("--check", action="store_true", help="decode and check only, write nothing")
    parser.add_argument("--expect", metavar="P,V,C",
                        help="also fail unless there are P packages, V .vsm sources and C MPG chunks")
    args = parser.parse_args()
    path = args.elf or args.data_dir / BOOT_ELF
    try:
        mc = analyse(Elf(path.read_bytes()))
        rebuild(mc)
        if args.expect:
            want = tuple(int(x) for x in args.expect.split(","))
            if want != mc.counts():
                mc.errors.append(f"expected {want} packages/.vsm/chunks, found {mc.counts()}")
    except (OSError, ValueError, struct.error, GenError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    for e in mc.errors:
        print(f"error: {e}", file=sys.stderr)
    pkgs, vsm, chunks = mc.counts()
    instrs = sum(1 for p in mc.packages for i in p.items if i.kind == "vu")
    print(f"{pkgs} packages, {len(mc.sources) - vsm} .dsm + {vsm} .vsm, {chunks} chunks, {instrs} instructions")
    if mc.errors:
        return 1
    if not args.check:
        print(f"{write(mc, args.out)} files in {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
