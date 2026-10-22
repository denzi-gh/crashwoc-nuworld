#!/usr/bin/env python3
"""Generate the data image: host definitions for the decomp data that compiled code references but no
compiled unit defines (the roots), and the tables src/data/image.c fills them from at startup.

Writes dataimage.c, dataimage_types.c and <unit>.data.inc (every wrapper ends with it) under --out.
Reads only committed files: the decomp sources and registries, port/units.toml, port/data.toml and
typedb.json.

Root kinds (src/data/image_tables.h):
  mirror   plain data (no pointer slots, the same layout on both sides): an alias at its PS2 offset
           into nw_ps2_mirror, the retail image from .vutext to the end of .bss
  copy     plain data that a host declaration needs aligned better than its PS2 address allows
  convert  data with pointer, function pointer or nuintptr slots, converted element by element
  alias    inside a copied or converted root, or inside data a compiled unit defines in C
"""
from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tomllib
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_converters  # noqa: E402
import nwclang  # noqa: E402
import nwunits  # noqa: E402
import typedb  # noqa: E402
from gen_ps2types import nested, type_name  # noqa: E402

cindex = nwclang.cindex

SOURCE = "dataimage.c"
TYPES = "dataimage_types.c"
DEFAULT_DATA = nwunits.REPO / "port" / "data.toml"
MIRROR_ALIGN = 64
ROOT_ALIGN = 16
LARGE_ARRAY = 16                    # x86-64 psABI: a global array of 16 bytes or more is 16-aligned
ADDR_TYPEDEFS = ("nuintptr", "nusintptr")
WIDE_TYPEDEFS = frozenset({"int64_t", "uint64_t"})
PRIMITIVE_TYPES = {("int", n, s): f"nw_type_{'s' if s else 'u'}{n * 8}"
                   for n in (1, 2, 4, 8) for s in (False, True)}


class GenError(Exception):
    pass


def lowbit(n: int) -> int:
    return n & -n if n else 1 << 30


def c_ident(text: str) -> str:
    return re.sub(r"[^A-Za-z0-9_]", "_", text)


def unit_symbol(unit: nwunits.Unit) -> str:
    return "nw_data_unit_" + c_ident(unit.name)


def output_path(out: Path, unit: nwunits.Unit) -> Path:
    return out / (unit.name + ".data.inc")


def local_slots(t) -> set[str]:
    """Pointer-like members of a libclang type: ptr, addr (nuintptr), long (not s64/u64)."""
    T = cindex.TypeKind
    out: set[str] = set()

    def walk(t) -> None:
        names, d = set(), t
        while True:
            if d.kind == T.ELABORATED:
                d = d.get_named_type()
            elif d.kind == T.TYPEDEF:
                decl = d.get_declaration()
                names.add(decl.spelling)
                d = decl.underlying_typedef_type
            else:
                break
        c = d.get_canonical()
        if names & set(ADDR_TYPEDEFS):
            out.add("addr")
        elif c.kind == T.POINTER:
            out.add("ptr")
        elif c.kind in (T.LONG, T.ULONG) and not names & WIDE_TYPEDEFS:
            out.add("long")
        elif c.kind in (T.CONSTANTARRAY, T.INCOMPLETEARRAY):
            walk((d if d.kind == c.kind else c).element_type)
        elif c.kind == T.RECORD:
            for f in c.get_fields():
                walk(f.type)

    walk(t)
    return out


# -- registries ----------------------------------------------------------------------------------

@dataclass
class Sym:
    name: str
    unit: int
    scope: str
    addr: int
    section: str
    size: int | None


class Registry:
    """data_symbols.toml, sections.json, symbol_addrs.txt, data_map.toml and the status [[data]]."""

    def __init__(self, decomp: Path, version: str = nwunits.VERSION):
        cfg = decomp / "config" / version
        self.symbols = [Sym(s["name"], int(s["unit"]), s["scope"], int(s["address"]), s["section"],
                            s.get("size"))
                        for s in tomllib.loads((cfg / "data_symbols.toml").read_text())["symbol"]]
        self.globals = {s.name: s for s in self.symbols if s.scope == "global"}
        self.statics = {(s.unit, s.name): s for s in self.symbols if s.scope == "static"}
        self.at: dict[int, list[Sym]] = defaultdict(list)
        for s in self.symbols:
            self.at[s.addr].append(s)
        raw = json.loads((cfg / "sections.json").read_text())
        self.sections = []                  # (name, addr, size, nobits) of the loaded sections
        for s in raw["sections"]:
            addr, size, flags = int(s["addr"], 16), int(s["size"], 16), int(s["flags"], 16)
            if flags & 2 and size:          # SHF_ALLOC
                self.sections.append((s["name"], addr, size, int(s["type"], 16) == 8))
        text = next((s for s in self.sections if s[0] == ".text"), None)
        vutext = next((s for s in self.sections if s[0] == ".vutext"), None)
        if text is None or vutext is None:
            raise GenError("sections.json has no .text or .vutext")
        self.text = (text[1], text[1] + text[2])
        end = max(a + n for name, a, n, _nobits in self.sections if name != ".text")
        self.mirror_base = vutext[1] & ~(MIRROR_ALIGN - 1)
        self.mirror_end = (end + MIRROR_ALIGN - 1) & ~(MIRROR_ALIGN - 1)
        self.addrs: dict[str, int] = {}     # data names of symbol_addrs.txt (linker symbols too)
        for line in (cfg / "symbol_addrs.txt").read_text().splitlines():
            m = re.match(r"\s*([A-Za-z_][\w.$]*)\s*=\s*0x([0-9A-Fa-f]+)\s*;(.*)$", line)
            if m and "type:func" not in m.group(3):
                self.addrs[m.group(1)] = int(m.group(2), 16)
        self.ranges = [(r["section"], int(r["start"]), int(r["end"]), r["owner"])
                       for r in tomllib.loads((cfg / "data_map.toml").read_text())["range"]]
        self.matching: set[tuple[int, int]] = set()     # (unit, address) the decomp defines in C
        for path in sorted((cfg / "status").rglob("*.toml")):
            for d in tomllib.loads(path.read_text()).get("data", []):
                _v, unit, addr, _name = d["id"].split(":", 3)
                if d["state"] == "matching":
                    self.matching.add((int(unit.rsplit("-", 1)[1]), int(addr, 16)))
        bounds = {a for _n, a, n, _b in self.sections} | {a + n for _n, a, n, _b in self.sections}
        for s in self.symbols:
            bounds.add(s.addr)
            if s.size:
                bounds.add(s.addr + s.size)
        for _s, start, end, _o in self.ranges:
            bounds |= {start, end}
        self.bounds = bounds

    def section(self, addr: int) -> str | None:
        return next((n for n, a, size, _b in self.sections if a <= addr < a + size), None)

    def owner(self, addr: int) -> int | None:
        for _s, start, end, owner in self.ranges:
            if start <= addr < end:
                return int(owner.rsplit("-", 1)[1]) if owner.startswith("unit-") else None
        return None

    def start(self, addr: int) -> Sym | None:
        """The largest sized symbol that starts at addr."""
        sized = [s for s in self.at.get(addr, ()) if s.size]
        return max(sized, key=lambda s: s.size) if sized else None

    def address(self, name: str) -> int | None:
        if name in self.globals:
            return self.globals[name].addr
        if name in self.addrs:
            return self.addrs[name]
        m = re.fullmatch(r"D_([0-9A-Fa-f]{8})", name)
        return int(m.group(1), 16) if m else None


# -- port/data.toml ------------------------------------------------------------------------------

def load_data_toml(path: Path) -> tuple[dict, list[str]]:
    data = tomllib.loads(path.read_text())
    errors = []
    if data.get("schema") != 1:
        errors.append(f"{path.name}: schema must be 1")
    unknown = set(data) - {"schema", "owned", "conflicts"}
    if unknown:
        errors.append(f"{path.name}: unknown tables {sorted(unknown)}")
    out = {"owned": data.get("owned", {}), "conflicts": data.get("conflicts", {})}
    for table, entries in out.items():
        for key, value in entries.items():
            if not isinstance(value, str) or not value.strip():
                errors.append(f"{path.name}: {table}.{key}: the value is the reason, a string")
    return out, errors


# -- types ---------------------------------------------------------------------------------------

class Types:
    """typedb-style descriptors of host declarations (records by typedb id) and their layouts."""

    def __init__(self, db: dict, gen: gen_converters.Gen):
        self.db = db
        self.records = db["records"]
        self.gen = gen
        self.converted = set(gen.named)
        self.derived: dict[str, tuple] = {}     # nw_rt_* -> (spelling, kind, data)
        # Records a .c file defines (not in typedb): plain ones work like POD records, the PS2 layout
        # from a parse of their file in the ps2 view (resolve_locals)
        self.local: dict[str, dict] = {}

    def local_host(self, rid: str, canon) -> None:
        if rid in self.local:
            return
        f = canon.get_declaration().location.file
        self.local[rid] = {"file": f.name if f else None, "host": (canon.get_size(), canon.get_align()),
                           "host_slots": local_slots(canon), "ps2": None, "plain": False}

    def resolve_locals(self, decomp: Path) -> None:
        by_file: dict[str, list[str]] = defaultdict(list)
        for rid, info in self.local.items():
            if info["file"]:
                by_file[info["file"]].append(rid)
        clang = nwclang.Clang(decomp, "ps2")
        try:
            for path, rids in sorted(by_file.items()):
                rel = Path(path).resolve()
                sdk = "sdk" in rel.parts
                tu = clang.index.parse(path, args=clang.args + ([] if sdk else ["-include", "nu_libc.h"]))
                found = {}
                for c in tu.cursor.walk_preorder():
                    if c.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL) \
                            and c.is_definition() and typedb.View.tagged(c):
                        kind = "struct" if c.kind == cindex.CursorKind.STRUCT_DECL else "union"
                        found.setdefault(f"{kind} {c.spelling}", c.type.get_canonical())
                    elif c.kind == cindex.CursorKind.TYPEDEF_DECL and c.spelling in rids:
                        found.setdefault(c.spelling, c.underlying_typedef_type.get_canonical())
                for rid in rids:
                    t = found.get(rid)
                    if t is None or t.kind != cindex.TypeKind.RECORD:
                        continue
                    info = self.local[rid]
                    info["ps2"] = (t.get_size(), t.get_align())
                    info["plain"] = not info["host_slots"] and not local_slots(t) \
                        and info["ps2"] == info["host"]
        finally:
            clang.close()

    # descriptors

    def record_id(self, t) -> str:
        sugar = t
        while True:
            if sugar.kind == cindex.TypeKind.ELABORATED:
                sugar = sugar.get_named_type()
            elif sugar.kind == cindex.TypeKind.TYPEDEF:
                decl = sugar.get_declaration()
                under = decl.underlying_typedef_type
                while under.kind == cindex.TypeKind.ELABORATED:
                    under = under.get_named_type()
                if under.kind == cindex.TypeKind.RECORD and \
                        not typedb.View.tagged(under.get_declaration()):
                    return decl.spelling
                sugar = under
            else:
                break
        decl = t.get_canonical().get_declaration()
        kind = "struct" if decl.kind == cindex.CursorKind.STRUCT_DECL else "union"
        return f"{kind} {decl.spelling}" if typedb.View.tagged(decl) else f"{kind} $"

    def describe(self, t) -> dict:
        T = cindex.TypeKind
        rid = self.record_id(t) if t.get_canonical().kind == T.RECORD else None
        if rid is not None and (rid not in self.records or "host" not in self.records[rid]):
            self.local_host(rid, t.get_canonical())
        tag, wide = None, False
        d = t
        while True:
            if d.kind == T.ELABORATED:
                d = d.get_named_type()
            elif d.kind == T.TYPEDEF:
                decl = d.get_declaration()
                if tag is None and decl.spelling in typedb.TAGGED_TYPEDEFS:
                    tag = decl.spelling
                wide = wide or decl.spelling in WIDE_TYPEDEFS
                d = decl.underlying_typedef_type
            else:
                break
        canon = d.get_canonical()
        k = canon.kind
        if k == T.POINTER:
            pointee = (d if d.kind == T.POINTER else canon).get_pointee()
            if pointee.get_canonical().kind in (T.FUNCTIONPROTO, T.FUNCTIONNOPROTO):
                out = {"kind": "fnptr"}
            else:
                out = {"kind": "pointer", "pointee": self.describe(pointee)}
        elif k in (T.CONSTANTARRAY, T.INCOMPLETEARRAY):
            elem = (d if d.kind == k else canon).element_type
            out = {"kind": "array", "count": canon.element_count if k == T.CONSTANTARRAY else 0,
                   "element": self.describe(elem)}
        elif k == T.RECORD:
            out = {"kind": "record", "ref": rid}
        elif k == T.ENUM:
            decl = canon.get_declaration()
            out = {"kind": "enum", **typedb.View.enum_int(decl)}
        elif k == T.VOID:
            out = {"kind": "void"}
        elif k in (T.FLOAT, T.DOUBLE, T.LONGDOUBLE):
            out = {"kind": "float", "size": canon.get_size()}
        elif k in (T.INT128, T.UINT128):
            out = {"kind": "u128", "signed": k in typedb.SIGNED, "size": 16}
        elif k in (T.LONG, T.ULONG):
            # nu_types.h's s64/u64 are int64_t/uint64_t: long in this LP64 view, 8 bytes on every host
            out = {"kind": "int", "signed": k == T.LONG, "size": 8} if wide else \
                {"kind": "long", "size": canon.get_size()}
        elif k in typedb.INTS:
            out = {"kind": "int", "signed": k in typedb.SIGNED, "size": canon.get_size()}
        else:
            out = {"kind": "other"}
        if tag:
            out["typedef"] = tag
        return out

    @staticmethod
    def element(d: dict) -> tuple[dict, int, bool]:
        """Innermost element, element count (incomplete outer dimension: 1) and completeness."""
        count, complete = 1, True
        while d["kind"] == "array":
            if d["count"]:
                count *= d["count"]
            else:
                complete = False
            d = d["element"]
        return d, count, complete

    def missing(self, d: dict) -> list[str]:
        """Element records with pointer slots that typedb (decomp/include) does not define."""
        e, _n, _c = self.element(d)
        if e["kind"] != "record":
            return []
        rec = self.records.get(e["ref"])
        if rec and "ps2" in rec and "host" in rec and not nested(e["ref"]):
            return []
        return [] if self.local.get(e["ref"], {}).get("plain") else [e["ref"]]

    def sizes(self, d: dict) -> tuple[int, int, int, int]:
        """(ps2 size, ps2 align, host size, host align) of one element; arrays count."""
        tag = d.get("typedef")
        kind = d["kind"]
        if tag in ADDR_TYPEDEFS:
            return 4, 4, 8, 8
        if tag:
            raise GenError(f"{tag} has no data image rule")
        if kind in ("pointer", "fnptr"):
            return 4, 4, 8, 8
        if kind in ("int", "enum", "float"):
            n = d["size"]
            return n, n, n, n
        if kind == "u128":
            return 16, 16, 16, 16
        if kind == "record":
            local = self.local.get(d["ref"])
            if local and local["plain"]:
                return (*local["ps2"], *local["host"])
            if d["ref"] not in self.records or "host" not in self.records[d["ref"]]:
                raise GenError(f"{d['ref']} has no layout in typedb")
            rec = self.records[d["ref"]]
            return rec["ps2"]["size"], rec["ps2"]["align"], rec["host"]["size"], rec["host"]["align"]
        if kind == "array":
            p, pa, h, ha = self.sizes(d["element"])
            return p * d["count"], pa, h * d["count"], ha
        raise GenError(f"{kind} has no data image rule")

    def plain(self, d: dict) -> bool:
        e, _n, _c = self.element(d)
        if e.get("typedef") in ADDR_TYPEDEFS or e["kind"] in ("pointer", "fnptr"):
            return False
        if e["kind"] == "record":
            local = self.local.get(e["ref"])
            if local is not None:
                return local["plain"]
            return self.gen.pod(e["ref"])
        return e["kind"] in ("int", "enum", "float", "u128")

    def slots(self, d: dict) -> tuple:
        """Comparable layout of one element: sizes and the pointer slots."""
        e, _n, _c = self.element(d)
        p, _pa, h, _ha = self.sizes(e)
        if e["kind"] == "record" and e["ref"] in self.local:
            slots = ()
        elif e["kind"] == "record":
            items = self.gen.items(e["ref"])
            slots = tuple((k, po, ho) for k, po, ho, _s in self.gen.flatten(items, 0, 0)
                          if k in ("ptr", "fn", "addr", "hook"))
        elif e.get("typedef") in ADDR_TYPEDEFS:
            slots = (("addr", 0, 0),)
        elif e["kind"] == "pointer":
            slots = (("ptr", 0, 0),)
        elif e["kind"] == "fnptr":
            slots = (("fn", 0, 0),)
        else:
            slots = ()
        return p, h, slots

    def xoff(self, d: dict, off: int) -> int | None:
        """Host offset of a PS2 offset into one element (nw_xoff); None when it does not map."""
        p, _pa, h, _ha = self.sizes(d)
        if off == p:
            return h
        if off > p:
            return None
        if self.plain(d):
            return off
        if d["kind"] != "record":
            return 0 if off == 0 else None
        for po, ho, ps, hs, count, kind, ref in self.gen.spans(d["ref"]):
            if po <= off < po + ps * count:
                i, inner = divmod(off - po, ps)
                if kind == "BYTES":
                    return ho + i * hs + inner
                if kind == "SLOT":
                    return ho + i * hs if inner == 0 else None
                rid = self.gen.record_of(ref[1:])
                sub = self.xoff({"kind": "record", "ref": rid}, inner)
                return None if sub is None else ho + i * hs + sub
        return None

    # NwType symbols of root elements: nw_convert.c (records), convert.c (primitives) or derived
    # here (nw_rt_*, dataimage_types.c)

    def nwtype(self, d: dict) -> str:
        tag = d.get("typedef")
        if tag in ADDR_TYPEDEFS:
            sym = f"nw_rt_addr_{'s' if tag == 'nusintptr' else 'u'}"
            self.derived[sym] = (tag, "addr", tag == "nusintptr")
            return sym
        kind = d["kind"]
        if kind == "fnptr":
            return "nw_type_fnptr"
        if kind == "pointer":
            target = self.pointee(d["pointee"])
            stem = target[len("nw_type_"):] if target.startswith("nw_type_") else target[len("nw_rt_"):]
            sym = "nw_rt_p_" + stem
            self.derived[sym] = (self.spelling(target) + " *", "pointer", target)
            return sym
        if kind == "record":
            return f"nw_type_{type_name(d['ref'])}"
        return self.primitive(d)

    def primitive(self, d: dict) -> str:
        kind = d["kind"]
        if kind in ("int", "enum"):
            return PRIMITIVE_TYPES[("int", d["size"], d["signed"])]
        if kind == "float":
            return f"nw_type_f{d['size'] * 8}"
        if kind == "u128":
            return "nw_type_u128"
        raise GenError(f"{kind} has no NwType")

    def pointee(self, d: dict) -> str:
        if d.get("typedef"):
            return "nw_type_void"
        kind = d["kind"]
        if kind in ("void", "other", "long"):
            return "nw_type_void"
        if kind == "fnptr":
            return "nw_type_fnptr"
        if kind == "record":
            ref = d["ref"]
            if ref in self.converted:
                return f"nw_type_{type_name(ref)}"
            local = self.local.get(ref)
            if local and local["plain"]:            # plain record of a .c file: POD with its sizes
                sym = "nw_rt_r_" + c_ident(type_name(ref))
                self.derived[sym] = (ref, "array", (*local["ps2"], *local["host"]))
                return sym
            sym = "nw_rt_o_" + c_ident(type_name(ref))
            self.derived[sym] = (ref, "opaque", None)
            return sym
        if kind == "pointer":
            return self.nwtype(d)
        if kind == "array":
            elem = self.pointee(d["element"])
            if not self.plain(d["element"]) or not d["count"]:
                return "nw_type_void"
            p, pa, h, ha = self.sizes(d)
            sym = f"nw_rt_a{d['count']}_" + elem[len("nw_type_"):] if elem.startswith("nw_type_") \
                else f"nw_rt_a{d['count']}_" + elem[len("nw_rt_"):]
            self.derived[sym] = (f"{self.spelling(elem)}[{d['count']}]", "array", (p, pa, h, ha))
            return sym
        return self.primitive(d)

    def spelling(self, sym: str) -> str:
        if sym in self.derived:
            return self.derived[sym][0]
        rid = self.gen.record_of(sym)
        return rid or sym.split("_", 2)[-1]


# -- scanning ------------------------------------------------------------------------------------

@dataclass
class Decl:
    unit: str
    where: str
    desc: dict
    host_size: int | None       # libclang, None when incomplete
    host_align: int
    need: int                   # alignment clang assumes for the declaration
    main: bool                  # referenced from the unit's own source


@dataclass
class Def:
    name: str
    static: bool
    where: str
    desc: dict
    host_size: int
    need: int


@dataclass
class Scan:
    refs: dict[str, Decl] = field(default_factory=dict)
    defs: list[Def] = field(default_factory=list)


class Scanner:
    def __init__(self, decomp: Path, types: Types):
        self.clang = nwclang.Clang(decomp, "host")
        self.types = types
        self.files: dict[str, str] = {}

    def close(self) -> None:
        self.clang.close()

    def norm(self, name: str) -> str:
        if name not in self.files:
            self.files[name] = os.path.normcase(os.path.abspath(name))
        return self.files[name]

    def need(self, t) -> tuple[int | None, int, int]:
        size, align = t.get_size(), t.get_align()
        size = size if size >= 0 else None
        if align < 0:                       # incomplete array: its element's alignment
            align = max(t.get_canonical().element_type.get_align(), 1)
        need = align
        if t.get_canonical().kind == cindex.TypeKind.CONSTANTARRAY and size and size >= LARGE_ARRAY:
            need = max(need, LARGE_ARRAY)
        return size, align, need

    def scan(self, unit: nwunits.Unit, path: Path, rel: str) -> Scan:
        # What the wrapper compiles: engine and game units start with the prelude (nu_libc.h).
        args = self.clang.args + (["-include", "nu_libc.h"] if not unit.name.startswith("sdk/") else [])
        tu = self.clang.index.parse(str(path), args=args)
        K, S = cindex.CursorKind, cindex.StorageClass
        main = self.norm(str(path))
        dropped = [(c.extent.start.line, c.extent.end.line) for c in tu.cursor.get_children()
                   if c.kind == K.FUNCTION_DECL and c.is_definition() and c.location.file
                   and self.norm(c.location.file.name) == main and unit.is_overridden(c.spelling)]
        # Errors inside definitions tu_filter drops do not matter, and the stand-in libc headers declare
        # no functions: calls to them cannot hide a data reference.
        errors = []
        for d in tu.diagnostics:
            if d.severity < cindex.Diagnostic.Error:
                continue
            loc = d.location
            if loc.file and self.norm(loc.file.name) == main and \
                    any(a <= loc.line <= b for a, b in dropped):
                continue
            text = str(d)
            if "implicit" in text or "undeclared library" in text or "undeclared function" in text:
                continue
            errors.append(text)
        if errors:
            raise GenError(f"{unit.name}: " + "\n".join(errors[:10]))
        out = Scan()
        externs: dict[str, list] = defaultdict(list)
        refs: dict[str, bool] = {}

        def walk(cursor, in_main: bool) -> None:
            for c in cursor.walk_preorder():
                if c.kind != K.DECL_REF_EXPR:
                    continue
                v = c.referenced
                if v is None or v.kind != K.VAR_DECL or v.linkage != cindex.LinkageKind.EXTERNAL \
                        or v.semantic_parent is None \
                        or v.semantic_parent.kind != K.TRANSLATION_UNIT:
                    continue
                refs[v.spelling] = refs.get(v.spelling, False) or in_main

        for c in tu.cursor.get_children():
            f = c.location.file
            in_main = f is not None and self.norm(f.name) == main
            if c.kind == K.FUNCTION_DECL and c.is_definition():
                if in_main and unit.is_overridden(c.spelling):
                    continue                # tu_filter drops it
                walk(c, in_main)
            elif c.kind == K.VAR_DECL:
                if c.storage_class == S.EXTERN:
                    externs[c.spelling].append(c)
                    continue
                walk(c, in_main)
                if in_main:
                    size, _align, need = self.need(c.type)
                    if size is None:
                        raise GenError(f"{rel}:{c.location.line}: {c.spelling} is defined "
                                       "with an incomplete type")
                    out.defs.append(Def(c.spelling, c.storage_class == S.STATIC,
                                        f"{rel}:{c.location.line}", self.types.describe(c.type),
                                        size, need))
        for name, main_ref in refs.items():
            decls = externs.get(name)
            if not decls:
                continue                    # defined in this unit
            best = max(decls, key=lambda d: d.type.get_size())
            size, align, need = self.need(best.type)
            f = best.location.file
            where = f"{os.path.relpath(f.name, self.clang.include.parent).replace(os.sep, '/')}" \
                    f":{best.location.line}" if f else rel
            out.refs[name] = Decl(unit.name, where, self.types.describe(best.type), size, align,
                                  max(need, *(self.need(d.type)[2] for d in decls)), main_ref)
        return out


# -- the image -----------------------------------------------------------------------------------

@dataclass
class Root:
    name: str
    addr: int
    decls: list[Decl]
    kind: str = ""              # mirror | copy | convert | alias
    desc: dict | None = None    # the chosen declaration's type
    elem: dict | None = None
    count: int = 1
    ps2_size: int = 0           # 0: unsized
    host_size: int = 0
    extent: str = ""            # registry | declared | bound
    need: int = 1
    container: object = None    # Obj for an alias
    host_off: int = 0
    nwtype: str = ""


@dataclass
class Obj:
    """Something with host storage at a retail address: a copied/converted root or a C definition."""
    name: str
    addr: int
    ps2_size: int
    elem: dict | None           # None: unknown type (only offset 0 maps)
    elem_ps2: int
    elem_host: int
    count: int
    align: int
    unit: nwunits.Unit | None   # C definition: its unit
    root: Root | None = None
    static: bool = False
    nwtype: str | None = None
    plain: bool = False
    host_size: int = 0

    @property
    def end(self) -> int:
        return self.addr + self.ps2_size


class Image:
    def __init__(self, model: nwunits.Model, reg: Registry, types: Types, data: dict,
                 scans: dict[str, Scan]):
        self.model, self.reg, self.types, self.data = model, reg, types, data
        self.scans = scans
        self.errors: list[str] = []
        self.notes: list[str] = []
        self.units = {u.name: u for u in model.units if u.kind == "wrapper"}
        self.by_index = {u.index: u for u in model.units}
        self.roots: dict[str, Root] = {}
        self.objs: list[Obj] = []
        self.unit_objs: dict[str, list[Obj]] = defaultdict(list)
        self.unit_aliases: dict[str, list[Root]] = defaultdict(list)
        self.build()

    def error(self, message: str) -> None:
        self.errors.append(message)

    def build(self) -> None:
        defined: dict[str, str] = {}
        for uname, scan in self.scans.items():
            for d in scan.defs:
                if not d.static:
                    defined.setdefault(d.name, uname)
        refs: dict[str, list[Decl]] = defaultdict(list)
        for uname, scan in self.scans.items():
            for name, decl in scan.refs.items():
                if name not in defined:
                    refs[name].append(decl)
        owned = self.data["owned"]
        for name in sorted(set(owned) - set(refs)):
            self.error(f"data.toml: owned.{name} is no root (nothing references it, or the decomp "
                       "defines it)")
        for name in sorted(set(self.data["conflicts"]) - set(refs)):
            self.error(f"data.toml: conflicts.{name} is no root")
        for name in sorted(refs):
            if name in owned:
                continue
            addr = self.reg.address(name)
            if addr is None:
                self.error(f"{refs[name][0].where}: {name} has no retail address (data_symbols.toml, "
                           "symbol_addrs.txt or a D_ name); list it under [owned] if the port "
                           "defines it")
                continue
            self.roots[name] = Root(name, addr, refs[name])
        for root in self.roots.values():
            self.classify(root)
        self.host_objects()
        self.place()

    # classification

    def classify(self, root: Root) -> None:
        where = f"{root.decls[0].where}: {root.name}"
        for decl in root.decls:
            missing = self.types.missing(decl.desc)
            if missing:
                self.error(f"{decl.where}: {root.name} has type {missing[0]}, which decomp/include does "
                           "not define (move the record into a header)")
                return
            if decl.desc["kind"] in ("void", "other") or \
                    self.types.element(decl.desc)[0]["kind"] in ("long", "other", "void"):
                self.error(f"{decl.where}: {root.name}: the declared type has no data image rule")
                return
        try:
            plain = [self.types.plain(d.desc) for d in root.decls]
            layouts = {self.types.slots(d.desc) for d, p in zip(root.decls, plain) if not p}
        except GenError as e:
            self.error(f"{where}: {e}")
            return
        root.need = max(d.need for d in root.decls)
        sect = self.reg.section(root.addr)
        if sect is None or not self.reg.mirror_base <= root.addr < self.reg.mirror_end:
            self.error(f"{where}: 0x{root.addr:08x} lies outside the retail data")
            return
        if all(plain):
            root.kind = "mirror"
            root.desc = max(root.decls, key=lambda d: d.host_size or 0).desc
        else:
            if any(plain) and root.name not in self.data["conflicts"]:
                spell = ", ".join(f"{d.where}" for d, p in zip(root.decls, plain) if p)
                self.error(f"{where}: declared as plain data at {spell} but with pointer slots "
                           "elsewhere (fix the declaration, or list it under [conflicts])")
                return
            if len(layouts) > 1:
                self.error(f"{where}: the declarations disagree on the pointer slots: "
                           + ", ".join(d.where for d in root.decls))
                return
            slotted = [d for d, p in zip(root.decls, plain) if not p]
            root.kind = "convert"
            root.desc = max(slotted, key=lambda d: self.specific(d.desc)).desc
        root.elem, count, complete = self.types.element(root.desc)
        p, _pa, h, _ha = self.types.sizes(root.elem)
        reg = self.reg.globals.get(root.name) or self.reg.start(root.addr)
        if reg and reg.size and (reg.name == root.name or reg.addr == root.addr):
            root.ps2_size, root.extent = reg.size, "registry"
        elif complete:
            root.ps2_size, root.extent = p * count, "declared"
        elif root.kind == "convert" or root.addr % root.need:
            above = [b for b in self.reg.bounds if b > root.addr]
            above += [r.addr for r in self.roots.values() if r.addr > root.addr]
            nxt = min(above, default=None)
            if nxt is None:
                self.error(f"{where}: no extent")
                return
            root.ps2_size, root.extent = nxt - root.addr, "bound"
        if root.kind == "mirror" and (root.addr - self.reg.mirror_base) % root.need:
            if not root.ps2_size or root.extent == "bound":
                self.error(f"{where}: declared as a complete array (clang assumes {root.need}-byte "
                           f"alignment) at 0x{root.addr:08x} without a retail size: declare it [] or "
                           "give it a size")
                return
            root.kind = "copy"
        if root.ps2_size:
            if complete and root.ps2_size < p * count:
                self.error(f"{where}: declared {p * count} bytes, retail has {root.ps2_size}")
                return
            root.count = max(root.ps2_size // p, 1) if p else 1
            if root.ps2_size % p and root.kind == "convert":
                self.notes.append(f"{root.name}: {root.ps2_size} bytes is not a whole number of "
                                  f"{p}-byte elements")
            if complete:
                root.count = max(root.count, count)
            root.host_size = root.count * h if root.kind == "convert" else root.ps2_size
        if root.kind == "convert":
            root.nwtype = self.types.nwtype(root.elem)

    def specific(self, d: dict) -> int:
        """Prefer declarations whose pointees say more than void."""
        e, _n, _c = self.types.element(d)
        if e["kind"] == "record":
            return 2
        if e["kind"] == "pointer":
            return 0 if e["pointee"]["kind"] in ("void", "other") else 1
        return 0

    # C definitions at retail addresses

    def host_objects(self) -> None:
        for uname, scan in self.scans.items():
            unit = self.units[uname]
            for d in scan.defs:
                sym = self.reg.statics.get((unit.index, d.name)) if d.static \
                    else self.reg.globals.get(d.name)
                if sym is None and not d.static:
                    addr = self.reg.address(d.name)
                    sym = self.reg.start(addr) if addr is not None else None
                if sym is None or (unit.index, sym.addr) not in self.reg.matching:
                    continue
                if self.types.missing(d.desc):
                    elem, elem_ps2, elem_host, count, plain = None, 0, 0, 1, False
                    nwtype = None
                else:
                    try:
                        elem, n, _complete = self.types.element(d.desc)
                        elem_ps2, _pa, elem_host, _ha = self.types.sizes(elem)
                        plain = self.types.plain(d.desc)
                        nwtype = None if plain else self.types.nwtype(elem)
                        count = n
                    except GenError:
                        elem, elem_ps2, elem_host, count, plain, nwtype = None, 0, 0, 1, False, None
                obj = Obj(d.name, sym.addr, sym.size or (elem_ps2 * count), elem, elem_ps2, elem_host,
                          count, d.need, unit, static=d.static, nwtype=nwtype, plain=plain,
                          host_size=d.host_size)
                self.objs.append(obj)
                self.unit_objs[uname].append(obj)

    # storage and aliases

    def place(self) -> None:
        stored = [Obj(r.name, r.addr, r.ps2_size, r.elem, *self.elem_sizes(r), r.count,
                      max(ROOT_ALIGN, self.types.sizes(r.elem)[3]), None, root=r,
                      nwtype=r.nwtype or None, plain=r.kind == "copy", host_size=r.host_size)
                  for r in self.roots.values() if r.kind in ("copy", "convert")]
        everything = sorted(self.objs + stored, key=lambda o: (o.addr, -o.ps2_size))
        outer: list[Obj] = []
        for o in everything:
            host = next((c for c in reversed(outer) if c.addr <= o.addr < c.end), None)
            if host is None:
                outer.append(o)
                continue
            if o.root is None or o.end > host.end:
                self.error(f"{o.name} (0x{o.addr:08x}, {o.ps2_size} bytes) overlaps {host.name} "
                           f"(0x{host.addr:08x}, {host.ps2_size} bytes)")
                continue
            self.alias(o.root, host)
        self.storage = [o for o in outer if o.root is not None]
        for r in self.roots.values():
            if r.kind == "mirror":
                host = next((c for c in outer if c.addr <= r.addr < c.end), None)
                if host is not None:
                    self.alias(r, host)
        for r in self.roots.values():
            if r.kind == "mirror":
                align = min(MIRROR_ALIGN, lowbit(r.addr - self.reg.mirror_base))
                if r.need > align:
                    self.error(f"{r.name}: 0x{r.addr:08x} is only {align}-byte aligned in the mirror")

    def elem_sizes(self, r: Root) -> tuple[int, int]:
        p, _pa, h, _ha = self.types.sizes(r.elem)
        return p, h

    def alias(self, root: Root, host: Obj) -> None:
        off = root.addr - host.addr
        if host.elem is None:
            hoff = 0 if off == 0 else None
        else:
            i, inner = divmod(off, host.elem_ps2) if host.elem_ps2 else (0, off)
            sub = self.types.xoff(host.elem, inner) if inner else 0
            hoff = None if sub is None else i * host.elem_host + sub
        if hoff is None:
            self.error(f"{root.name} (0x{root.addr:08x}) points into {host.name} at +0x{off:x}, which "
                       "does not map to the host layout")
            return
        align = min(host.align, lowbit(hoff))
        if root.need > align:
            self.error(f"{root.name}: {host.name}+{hoff} is only {align}-byte aligned, the "
                       f"declaration needs {root.need}")
            return
        root.kind, root.container, root.host_off = "alias", host, hoff
        if host.unit is not None:
            self.unit_aliases[host.unit.name].append(root)

    # reports

    def replaced(self) -> list[tuple[str, str]]:
        """Roots in the data of units the port replaces (their replacement must write them)."""
        out = []
        for r in self.roots.values():
            owner = self.reg.owner(r.addr)
            unit = self.by_index.get(owner) if owner is not None else None
            if unit is not None and unit.treatment in ("replace", "hle", "dead"):
                out.append((r.name, unit.name))
        return sorted(out)

    def counts(self) -> dict[str, int]:
        out = defaultdict(int)
        for r in self.roots.values():
            out[r.kind] += 1
        return out


# -- rendering -----------------------------------------------------------------------------------

def asm_alias(name: str, base: str, off: int, size: int) -> list[str]:
    elf = (f'    ".type " NW_SYM({name}) ", @object\\n"\n'
           f'    ".size " NW_SYM({name}) ", {size}\\n"\n')
    return [f'__asm__(".globl " NW_SYM({name}) "\\n"',
            "#ifdef __ELF__",
            elf.rstrip("\n"),
            "#endif",
            f'        ".set " NW_SYM({name}) ", " NW_SYM({base}) "+{off}\\n");']


SYM_MACROS = ["#define NW_SYM_STR2(x) #x",
              "#define NW_SYM_STR(x) NW_SYM_STR2(x)",
              "#define NW_SYM(x) NW_SYM_STR(__USER_LABEL_PREFIX__) #x"]


def render(image: Image) -> dict[str, str]:
    reg, types = image.reg, image.types
    gen = ("/* Generated by tools/gen_dataimage.py from the decomp registries and port/data.toml; do "
           "not edit. */")
    roots = sorted(image.roots.values(), key=lambda r: (r.addr, r.name))
    src = [gen,
           "/* The roots are raw bytes here, so this unit includes no decomp, libc or converter header",
           " * (their declarations would clash), and it must never be built with LTO. */",
           '#include "data/image_tables.h"', "", *SYM_MACROS, "",
           f"#define NW_MIRROR_BASE 0x{reg.mirror_base:08x}u",
           f"#define NW_MIRROR_SIZE 0x{reg.mirror_end - reg.mirror_base:x}u",
           f"_Alignas({MIRROR_ALIGN}) unsigned char nw_ps2_mirror[NW_MIRROR_SIZE];", ""]
    used_types = sorted({r.nwtype for r in roots if r.kind == "convert"})
    src += [f"extern const struct NwType {t};" for t in used_types]
    src += ["", "/* Copied and converted roots. */"]
    for o in image.storage:
        r = o.root
        align = max(ROOT_ALIGN, types.sizes(r.elem)[3], r.need)
        src.append(f"_Alignas({align}) unsigned char {r.name}[{max(r.host_size, 1)}];")
    src += ["", "/* Plain roots in the mirror and roots inside other roots. */"]
    for r in roots:
        if r.kind == "mirror":
            src += asm_alias(r.name, "nw_ps2_mirror", r.addr - reg.mirror_base,
                             r.ps2_size or (r.decls[0].host_size or 1))
        elif r.kind == "alias" and r.container.unit is None:
            src += asm_alias(r.name, r.container.name, r.host_off, max(r.decls[0].host_size or 1, 1))
    aliases = [r for r in roots if r.kind == "alias"]
    if aliases:
        src += ["", *(f"extern unsigned char {r.name}[];" for r in aliases)]
    src += ["", "static const NwImageRoot nw_image_roots[] = {"]
    kinds = {"mirror": "MIRROR", "copy": "COPY", "convert": "CONVERT", "alias": "ALIAS"}
    for r in roots:
        host = f"nw_ps2_mirror + 0x{r.addr - reg.mirror_base:x}u" if r.kind == "mirror" else r.name
        host_size = r.host_size if r.kind in ("copy", "convert") else (r.decls[0].host_size or 0)
        elem = f"&{r.nwtype}" if r.kind == "convert" else "0"
        src.append(f'    {{ "{r.name}", 0x{r.addr:08x}u, {r.ps2_size}u, {host}, {host_size}u, {elem}, '
                   f"{r.count if r.kind == 'convert' else 0}u, NW_ROOT_{kinds[r.kind]} }},")
    src += ["};", ""]
    registry = sorted({(s.addr, s.size) for s in reg.symbols if s.size})
    src.append("static const unsigned int nw_image_registry[][2] = {")
    src += [f"    {{ 0x{a:08x}u, {n}u }}," for a, n in registry]
    src += ["};", ""]
    bounds = sorted(reg.bounds | {r.addr for r in roots})
    src.append("static const unsigned int nw_image_bounds[] = {")
    for i in range(0, len(bounds), 8):
        src.append("    " + " ".join(f"0x{b:08x}u," for b in bounds[i:i + 8]))
    src += ["};", ""]
    units = list(image.units.values())
    src += [f"extern const NwDataUnit {unit_symbol(u)};" for u in units]
    src += ["", "static const NwDataUnit *const nw_data_units[] = {",
            *(f"    &{unit_symbol(u)}," for u in units), "};", "",
            "const NwImageTables nw_image_tables = {",
            "    NW_MIRROR_BASE, NW_MIRROR_SIZE, nw_ps2_mirror,",
            f"    0x{reg.text[0]:08x}u, 0x{reg.text[1]:08x}u,",
            f"    nw_image_roots, {len(roots)}u,",
            f"    nw_data_units, {len(units)}u,",
            f"    nw_image_registry, {len(registry)}u,",
            f"    nw_image_bounds, {len(bounds)}u,",
            "};", ""]

    typ = [gen, '#include "data/convert.h"', ""]
    typ += [f"extern const NwType {sym};" for sym in sorted(types.derived)]
    externs = sorted({t for _n, k, t in types.derived.values()
                      if k == "pointer" and t.startswith("nw_type_")})
    typ += [f"extern const NwType {t};" for t in externs] + [""]
    for sym, (name, kind, target) in sorted(types.derived.items()):
        stem = sym[len("nw_rt_"):]
        typ += [f"/* {name} */"]
        if kind == "opaque":
            typ.append(f'const NwType {sym} = {{ "{name}", 0, 1, 0, 1, NW_TYPE_OPAQUE, NULL, NULL, NULL, 0u }};')
        elif kind == "array":
            p, pa, h, ha = target
            typ.append(f'const NwType {sym} = {{ "{name}", {p}, {pa}, {h}, {ha}, NW_TYPE_POD, NULL, NULL, '
                       "NULL, 0u };")
        elif kind == "addr":
            typ += [f"static void nw_rtx_{stem}(void *dst, const void *src, NwXCtx *ctx)", "{",
                    "    uint32_t v;", "    memcpy(&v, src, sizeof v);",
                    f"    *(uintptr_t *)dst = ctx->addr_in(ctx, v, {int(target)}, NULL);", "}", "",
                    f"static void nw_rtxr_{stem}(void *dst, const void *src, NwXCtx *ctx)", "{",
                    "    uint32_t v = ctx->addr_out(ctx, *(const uintptr_t *)src, NULL);",
                    "    memcpy(dst, &v, sizeof v);", "}", "",
                    f"static const NwSpan nw_rts_{stem}[] = "
                    "{ { 0u, 0u, 4u, sizeof(uintptr_t), 1u, NW_SPAN_SLOT, NULL } };",
                    f'const NwType {sym} = {{ "{name}", 4, 4, sizeof(uintptr_t), _Alignof(uintptr_t), 0, '
                    f"nw_rtx_{stem}, nw_rtxr_{stem}, nw_rts_{stem}, 1u }};"]
        else:
            typ += [f"static void nw_rtx_{stem}(void *dst, const void *src, NwXCtx *ctx)", "{",
                    "    uint32_t addr;", "    memcpy(&addr, src, sizeof addr);",
                    f"    *(void **)dst = nw_ptr_in(ctx, addr, &{target}, NULL);", "}", "",
                    f"static void nw_rtxr_{stem}(void *dst, const void *src, NwXCtx *ctx)", "{",
                    f"    uint32_t addr = nw_ptr_out(ctx, *(void *const *)src, &{target}, NULL);",
                    "    memcpy(dst, &addr, sizeof addr);", "}", "",
                    f"static const NwSpan nw_rts_{stem}[] = "
                    "{ { 0u, 0u, 4u, sizeof(void *), 1u, NW_SPAN_SLOT, NULL } };",
                    f'const NwType {sym} = {{ "{name}", 4, 4, sizeof(void *), _Alignof(void *), 0, '
                    f"nw_rtx_{stem}, nw_rtxr_{stem}, nw_rts_{stem}, 1u }};"]
        typ.append("")
    if not types.derived:
        typ.append("typedef int nw_dataimage_types_empty;")

    out = {SOURCE: "\n".join(src), TYPES: "\n".join(typ) + "\n"}
    for uname, unit in image.units.items():
        out[str(Path(unit.name + ".data.inc"))] = render_unit(image, unit)
    return out


def render_unit(image: Image, unit: nwunits.Unit) -> str:
    lines = [f"/* Generated by tools/gen_dataimage.py for decomp unit {unit.name}; do not edit. */",
             '#include "data/image_tables.h"', ""]
    objs = image.unit_objs.get(unit.name, [])
    types = sorted({o.nwtype for o in objs if o.nwtype})
    lines += [f"extern const struct NwType {t};" for t in types]
    if objs:
        lines.append("static const NwDataObj nw_data_objs_[] = {")
        for o in objs:
            t = f"&{o.nwtype}" if o.nwtype else "0"
            lines.append(f'    {{ "{o.name}", 0x{o.addr:08x}u, {o.ps2_size}u, (void *)&{o.name}, '
                         f"(unsigned int)sizeof {o.name}, {t}, {o.count}u, "
                         f"{'NW_DATA_PLAIN' if o.plain else '0u'} }},")
        lines += ["};", f'const NwDataUnit {unit_symbol(unit)} = {{ "{unit.name}", nw_data_objs_, '
                        f"{len(objs)}u }};"]
    else:
        lines.append(f'const NwDataUnit {unit_symbol(unit)} = {{ "{unit.name}", 0, 0u }};')
    aliases = image.unit_aliases.get(unit.name, [])
    if aliases:
        lines += ["", *SYM_MACROS]
        for r in sorted(aliases, key=lambda r: r.addr):
            lines += asm_alias(r.name, r.container.name, r.host_off, max(r.decls[0].host_size or 1, 1))
        lines += ["#undef NW_SYM", "#undef NW_SYM_STR", "#undef NW_SYM_STR2"]
    checks = []
    for r in sorted(image.roots.values(), key=lambda r: r.name):
        decl = next((d for d in r.decls if d.unit == unit.name), None)
        if decl is None:
            continue
        expr = r.name if decl.host_size is not None else f"{r.name}[0]"
        size = decl.host_size if decl.host_size is not None else types_size(image, decl)
        if size is None:
            continue
        checks.append(f'_Static_assert(sizeof({expr}) == {size}, "{r.name}");')
    if checks:
        lines += ["", "/* The host layouts the generator assumed (libclang, x86_64-linux-gnu). */", *checks]
    return "\n".join(lines) + "\n"


def types_size(image: Image, decl: Decl) -> int | None:
    d = decl.desc
    if d["kind"] != "array":
        return None
    try:
        return image.types.sizes(d["element"])[2]
    except GenError:
        return None


# -- driver --------------------------------------------------------------------------------------

def generate(model: nwunits.Model, decomp: Path, db: dict, annotations: dict,
             data: dict) -> tuple[Image, dict[str, str]]:
    gen = gen_converters.Gen(db, annotations)
    if gen.errors:
        raise GenError("typedb/convert.toml: " + gen.errors[0])
    types = Types(db, gen)
    reg = Registry(decomp)
    scanner = Scanner(decomp, types)
    scans: dict[str, Scan] = {}
    try:
        for unit in model.units:
            if unit.kind != "wrapper":
                continue
            scans[unit.name] = scanner.scan(unit, decomp / unit.source, unit.source)
    finally:
        scanner.close()
    types.resolve_locals(decomp)
    image = Image(model, reg, types, data, scans)
    return image, {} if image.errors else render(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    nwunits.add_common_args(parser)
    parser.add_argument("--typedb", type=Path, default=nwunits.REPO / "build" / "gen" / "typedb.json")
    parser.add_argument("--annotations", type=Path, default=gen_converters.DEFAULT_ANNOTATIONS)
    parser.add_argument("--data", type=Path, default=DEFAULT_DATA, help="port/data.toml")
    parser.add_argument("--out", type=Path, default=nwunits.REPO / "build" / "gen")
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    parser.add_argument("--list", action="store_true", help="print every root")
    args = parser.parse_args()

    model = nwunits.load(args.decomp, args.units, args.port_src, args.editor)
    if not nwunits.report(model, sys.stderr):
        return 1
    wrappers = [u for u in model.units if u.kind == "wrapper"]
    if args.list_outputs:
        nwunits.print_outputs(args.out, [args.out / SOURCE, args.out / TYPES,
                                         *(output_path(args.out, u) for u in wrappers)])
        return 0
    try:
        data, errors = load_data_toml(args.data)
        annotations, ann_errors = gen_converters.load_annotations(args.annotations)
        errors += ann_errors
        if errors:
            raise GenError(errors[0])
        db = json.loads(args.typedb.read_text())
        image, outputs = generate(model, args.decomp, db, annotations, data)
    except (OSError, ValueError, GenError, nwclang.ClangError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    for e in image.errors:
        print(f"error: {e}", file=sys.stderr)
    if args.list or image.errors:
        for r in sorted(image.roots.values(), key=lambda r: r.addr):
            where = r.container.name + f"+{r.host_off}" if r.kind == "alias" else ""
            print(f"0x{r.addr:08x} {r.kind:7} {r.name:32} {r.ps2_size:7} {r.extent:8} "
                  f"{r.nwtype or ''} {where}")
        for name, unit in image.replaced():
            print(f"replaced: {name} ({unit})")
        for note in image.notes:
            print(f"note: {note}")
    if image.errors:
        return 1
    written = 0
    if not args.check:
        for name, text in outputs.items():
            written += nwunits.write_if_changed(args.out / name, text)
    counts = image.counts()
    bound = sum(1 for r in image.roots.values() if r.extent == "bound")
    print(f"{len(image.roots)} roots ({', '.join(f'{counts[k]} {k}' for k in ('mirror', 'copy', 'convert', 'alias'))}), "
          f"{len(image.objs)} C definitions, {bound} bounded extents, {len(image.replaced())} in "
          "replaced units" + ("" if args.check else f"; {written} files written to {args.out}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
