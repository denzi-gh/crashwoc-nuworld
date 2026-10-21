#!/usr/bin/env python3
"""Generate the converters nw_x_<T> / nw_xr_<T> between the PS2 mirror structs and the host structs."""
from __future__ import annotations

import argparse
import json
import sys
import tomllib
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_ps2types  # noqa: E402
import nwunits  # noqa: E402
from gen_ps2types import leaf, mirror_name, nested, type_name  # noqa: E402

DEFAULT_ANNOTATIONS = nwunits.REPO / "port" / "convert.toml"
HEADER = "nw_convert.h"
SOURCE = "nw_convert.c"
TEST_TABLE = "nw_convert_test.inc"
ADDRESS_TYPEDEFS = ("nuintptr", "nusintptr")
SLOTS = ("ptr", "fn", "addr")
LOOPS = ("i", "j", "k")
PRIMITIVES = {f"nw_type_{k}{n * 8}": n for k in "us" for n in (1, 2, 4, 8)} | {
    "nw_type_u128": 16, "nw_type_f32": 4, "nw_type_f64": 8}


@dataclass
class Item:
    kind: str                       # bytes | int | bit | pod | ptr | fn | addr | rec | hook
    path: str | None                # C member path; None for an anonymous union copied as bytes
    ps2_off: int                    # from the named record; bits for `bit`
    host_off: int
    ps2_size: int                   # of one element; bits for `bit`
    host_size: int
    dims: list[int] = field(default_factory=list)
    type: str = ""                  # pointee (ptr) or element (rec, pod) NwType symbol
    ref: str = ""                   # element record (rec, pod)
    signed: bool = False            # addr
    hook: str = ""                  # hook name stem

    @property
    def count(self) -> int:
        n = 1
        for d in self.dims:
            n *= d
        return n


def hook_stem(key: str) -> str:
    return type_name(key).replace("$", "__").replace(".", "__")


def load_annotations(path: Path) -> tuple[dict[str, dict], list[str]]:
    data = tomllib.loads(path.read_text())
    errors, out = [], {}
    if data.get("schema") != 1:
        errors.append(f"{path.name}: schema must be 1")
    for table in ("unions", "arrays"):
        for key, entry in data.get(table, {}).items():
            keys = set(entry) if isinstance(entry, dict) else set()
            ok = keys == {"hook"} and entry["hook"] is True
            ok = ok or (table == "unions" and keys == {"member"} and isinstance(entry["member"], str))
            if not ok:
                allowed = "member = \"<name>\" or hook = true" if table == "unions" else "hook = true"
                errors.append(f"{path.name}: {table}.{key}: takes {allowed}")
                continue
            out[key] = dict(entry, table=table)
    unknown = set(data) - {"schema", "unions", "arrays"}
    if unknown:
        errors.append(f"{path.name}: unknown tables {sorted(unknown)}")
    return out, errors


class Gen:
    def __init__(self, db: dict, annotations: dict[str, dict]):
        self.mirror = gen_ps2types.Mirror(db)
        self.errors = list(self.mirror.errors)
        self.db = db
        self.records = db["records"]
        self.named = [rid for rid in self.mirror.named if "host" in self.records[rid]]
        self.converted = set(self.named)
        self.annotations = annotations
        self.used: set[str] = set()
        self.derived: dict[str, tuple] = {}     # symbol -> (spelling, kind, target or sizes)
        self.hooks: dict[str, str] = {}         # stem -> named record
        self.memo: dict[str, list[Item]] = {}
        if self.errors:
            return
        for rid in self.named:
            self.items(rid)
        for key in sorted(set(annotations) - self.used):
            self.errors.append(f"convert.toml: {annotations[key]['table']}.{key} matches nothing to "
                               "annotate")
        symbols = Counter([*(f"nw_type_{type_name(r)}" for r in self.named), *self.derived])
        self.errors += [f"{sym} is generated twice" for sym, n in symbols.items() if n > 1]

    def error(self, where: str, message: str) -> None:
        if f"{where}: {message}" not in self.errors:
            self.errors.append(f"{where}: {message}")

    def items(self, rid: str) -> list[Item]:
        if rid not in self.memo:
            rec = self.records[rid]
            if rec["kind"] == "union" and not nested(rid):
                self.memo[rid] = self.union(rid, rid, "", "", 0, 0)
            else:
                self.memo[rid] = self.walk(rid, rid, "", 0, 0)
        return self.memo[rid]

    def pod(self, rid: str) -> bool:
        rec = self.records[rid]
        return rec["ps2"] == rec["host"] and all(
            i.kind in ("bytes", "int", "bit", "pod") and i.ps2_off == i.host_off
            for i in self.items(rid))

    def walk(self, rid: str, top: str, prefix: str, ps2_base: int, host_base: int) -> list[Item]:
        out = []
        for f in self.records[rid]["fields"]:
            out += self.field(rid, top, f, prefix, ps2_base, host_base)
        return out

    def field(self, rid: str, top: str, f: dict, prefix: str, ps2_base: int,
              host_base: int) -> list[Item]:
        name = f["name"]
        path = prefix + name
        where = f"{f.get('where') or self.records[rid]['where']}: {rid}.{name or '<unnamed>'}"
        p, h = f["ps2"], f["host"]
        d, dims = leaf(f)
        po, ho = ps2_base + p["offset"], host_base + h["offset"]
        if 0 in dims:
            return self.hook(f"{rid}.{name}", top, where, po, ho, 0, 0)
        if "bit" in p:
            if not name:
                return []
            if (p["bits"], p["size"], p["bit"] - p["unit"] * 8) != \
                    (h["bits"], h["size"], h["bit"] - h["unit"] * 8):
                self.error(where, "the bitfield is packed differently on the host")
            return [Item("bit", path, ps2_base * 8 + p["bit"], host_base * 8 + h["bit"],
                         p["bits"], h["bits"])]
        kind, tag = d["kind"], d.get("typedef")
        count = 1
        for n in dims:
            count *= n
        if len(dims) > len(LOOPS):
            self.error(where, f"arrays of more than {len(LOOPS)} dimensions are not supported")
            return []
        ps2_size, host_size = p["size"] // count, h["size"] // count
        if kind in ("long", "other", "void"):
            self.error(where, f"{f['type']} has no conversion rule")
            return []
        if tag in ADDRESS_TYPEDEFS:
            return [Item("addr", path, po, ho, ps2_size, host_size, dims, signed=d["signed"])]
        if tag:
            self.error(where, f"{tag} has no conversion rule")
            return []
        if kind == "pointer":
            return [Item("ptr", path, po, ho, ps2_size, host_size, dims,
                         type=self.pointee(d["pointee"], where))]
        if kind == "fnptr":
            return [Item("fn", path, po, ho, ps2_size, host_size, dims)]
        if kind == "record":
            ref = d["ref"]
            if ref not in self.records or "host" not in self.records[ref]:
                self.error(where, f"{ref} has no host layout")
                return []
            if nested(ref):
                if dims:
                    self.error(where, "arrays of unnamed records are not supported")
                    return []
                inner = path + "." if name else prefix
                if self.records[ref]["kind"] == "union":
                    return self.union(ref, top, path if name else None, inner, po, ho)
                return self.walk(ref, top, inner, po, ho)
            kind = "pod" if self.pod(ref) else "rec"
            return [Item(kind, path, po, ho, ps2_size, host_size, dims,
                         type=f"nw_type_{type_name(ref)}", ref=ref)]
        if ps2_size != host_size:
            self.error(where, "the host size differs")
            return []
        if dims or kind in ("float", "u128"):
            return [Item("bytes", path, po, ho, p["size"], h["size"])]
        return [Item("int", path, po, ho, ps2_size, host_size)]

    def hook(self, key: str, top: str, where: str, po: int, ho: int, ps2_size: int,
             host_size: int) -> list[Item]:
        entry = self.annotation(key)
        if not entry or not entry.get("hook"):
            self.error(where, "a [0]/[] array needs a hook in port/convert.toml" if "." in key else
                       "a union that mixes pointers with other members needs a member or a hook in "
                       "port/convert.toml")
            return []
        stem = hook_stem(key)
        self.hooks[stem] = top
        return [Item("hook", None, po, ho, ps2_size, host_size, hook=stem)]

    def annotation(self, key: str) -> dict | None:
        entry = self.annotations.get(key)
        if entry is not None:
            self.used.add(key)
        return entry

    def union(self, rid: str, top: str, here: str | None, prefix: str, po: int,
              ho: int) -> list[Item]:
        rec = self.records[rid]
        where = f"{rec['where']}: {rid}"
        members = rec["fields"]
        sub = [self.field(rid, top, m, prefix, po, ho) for m in members]
        plain = not any(i.kind in SLOTS + ("rec", "hook") for items in sub for i in items)
        single = all(len(items) == 1 and items[0].kind in ("ptr", "addr") and not items[0].dims
                     for items in sub)
        same = all(self.signature(items) == self.signature(sub[0]) for items in sub[1:])
        entry = self.annotation(rid)
        if entry and (plain or single or same):
            self.error(where, "needs no annotation in port/convert.toml")
        elif entry and "member" in entry:
            names = [m["name"] for m in members]
            if entry["member"] not in names:
                self.error(where, f"port/convert.toml names no member {entry['member']!r}")
                return []
            return sub[names.index(entry["member"])]
        if plain:
            if rec["ps2"]["size"] != rec["host"]["size"]:
                self.error(where, "the host size differs")
                return []
            return [Item("bytes", here or None, po, ho, rec["ps2"]["size"], rec["host"]["size"])]
        if single:
            first = next((items[0] for items in sub if items[0].kind == "ptr"), sub[0][0])
            if first.kind == "ptr":
                first.type = "nw_type_void"
            return [first]
        if same:
            return sub[0]
        return self.hook(rid, top, where, po, ho, rec["ps2"]["size"], rec["host"]["size"])

    # Leaf kinds and offsets with adjacent bytes merged; pointee types do not count.
    def signature(self, items: list[Item]) -> list[tuple]:
        out: list[list] = []
        for i in self.flatten(items, 0, 0):
            last = out[-1] if out else None
            if i[0] == "b" and last and last[0] == "b" and last[1] + last[3] == i[1] \
                    and last[2] + last[3] == i[2]:
                last[3] += i[3]
            else:
                out.append(list(i))
        return [tuple(x) for x in out]

    def flatten(self, items: list[Item], ps2_base: int, host_base: int):
        for i in sorted(items, key=lambda i: (i.ps2_off * (1 if i.kind == "bit" else 8))):
            if i.kind in ("bytes", "int"):
                yield ("b", ps2_base + i.ps2_off, host_base + i.host_off, i.ps2_size)
            elif i.kind == "bit":
                yield ("bit", ps2_base * 8 + i.ps2_off, host_base * 8 + i.host_off, i.ps2_size)
            elif i.kind in ("pod", "rec"):
                for n in range(i.count):
                    yield from self.flatten(self.items(i.ref), ps2_base + i.ps2_off + n * i.ps2_size,
                                            host_base + i.host_off + n * i.host_size)
            else:
                for n in range(i.count):
                    yield (i.kind, ps2_base + i.ps2_off + n * i.ps2_size,
                           host_base + i.host_off + n * i.host_size, i.ps2_size)

    def pointee(self, d: dict, where: str) -> str:
        kind = d["kind"]
        if d.get("typedef"):
            self.error(where, f"pointers to {d['typedef']} have no conversion rule")
            return "nw_type_void"
        if kind == "void":
            return "nw_type_void"
        if kind == "fnptr":
            return "nw_type_fnptr"
        if kind in ("int", "enum"):
            return f"nw_type_{'s' if d['signed'] else 'u'}{d['size'] * 8}"
        if kind == "float":
            return f"nw_type_f{d['size'] * 8}"
        if kind == "u128":
            return "nw_type_u128"
        if kind == "record":
            ref = d["ref"]
            if nested(ref) or (ref in self.records and ref not in self.converted):
                self.error(where, f"pointers to {ref} have no conversion rule")
                return "nw_type_void"
            sym = f"nw_type_{type_name(ref)}"
            if ref not in self.records:
                self.derived[sym] = (ref, "opaque", None)
            return sym
        if kind == "pointer":
            target = self.pointee(d["pointee"], where)
            sym = "nw_type_p_" + target[len("nw_type_"):]
            name = self.spelling(target)
            self.derived[sym] = (name + ("*" if name.endswith("*") else " *"), "pointer", target)
            return sym
        if kind == "array":
            elem = self.pointee(d["element"], where)
            if not self.is_pod(elem):
                self.error(where, "pointers to arrays of non-POD elements have no conversion rule")
                return "nw_type_void"
            sym = f"nw_type_a{d['count']}_{elem[len('nw_type_'):]}"
            ps2, host = self.sizes(elem)
            self.derived[sym] = (f"{self.spelling(elem)}[{d['count']}]", "array",
                                 (d["count"] * ps2[0], ps2[1], d["count"] * host[0], host[1]))
            return sym
        self.error(where, f"pointers to {kind} have no conversion rule")
        return "nw_type_void"

    def record_of(self, sym: str) -> str | None:
        return next((r for r in self.named if f"nw_type_{type_name(r)}" == sym), None)

    def spelling(self, sym: str) -> str:
        if sym in self.derived:
            return self.derived[sym][0]
        return self.record_of(sym) or sym[len("nw_type_"):]

    def is_pod(self, sym: str) -> bool:
        if sym in self.derived:
            return self.derived[sym][1] == "array"
        rid = self.record_of(sym)
        return self.pod(rid) if rid else sym in PRIMITIVES

    def sizes(self, sym: str) -> tuple[tuple[int, int], tuple[int, int]]:
        if sym in PRIMITIVES:
            return (PRIMITIVES[sym],) * 2, (PRIMITIVES[sym],) * 2
        if sym in self.derived:
            n = self.derived[sym][2]
            return (n[0], n[1]), (n[2], n[3])
        rec = self.records[self.record_of(sym)]
        return (rec["ps2"]["size"], rec["ps2"]["align"]), (rec["host"]["size"], rec["host"]["align"])

    # -- rendering -----------------------------------------------------------------------------

    def stmt(self, i: Item, reverse: bool) -> list[str]:
        loops = LOOPS[:len(i.dims)] if i.kind in SLOTS + ("rec",) else ()
        sub = "".join(f"[{v}]" for v in loops)
        d, s = f"dst->{i.path}{sub}", f"src->{i.path}{sub}"
        site = f'"{i.path}"'
        if i.kind == "bytes" and i.path is None:
            line = (f"NW_XBYTES(dst, {i.ps2_off}, src, {i.host_off}, {i.ps2_size});" if reverse
                    else f"NW_XBYTES(dst, {i.host_off}, src, {i.ps2_off}, {i.ps2_size});")
        elif i.kind in ("bytes", "pod"):
            line = f"NW_XCOPY({d}, {s});"
        elif i.kind in ("int", "bit"):
            line = f"{d} = {s};"
        elif i.kind == "ptr":
            line = (f"{d} = nw_ptr_out(ctx, {s}, &{i.type}, {site});" if reverse
                    else f"{d} = nw_ptr_in(ctx, {s}, &{i.type}, {site});")
        elif i.kind == "fn":
            line = (f"{d} = nw_fn_out(ctx, (NwFuncPtr){s}, {site});" if reverse
                    else f"{d} = (__typeof__({d}))nw_fn_in(ctx, {s}, {site});")
        elif i.kind == "addr":
            line = (f"{d} = ctx->addr_out(ctx, (uintptr_t){s}, {site});" if reverse
                    else f"{d} = ctx->addr_in(ctx, {s}, {int(i.signed)}, {site});")
        elif i.kind == "rec":
            index = "-1"
            if loops:
                flat = loops[0]
                for v, n in zip(loops[1:], i.dims[1:]):
                    flat = f"{flat} * {n} + {v}"
                index = f"(int)({flat})" if len(loops) > 1 else f"(int){flat}"
            fn = ("nw_xr_" if reverse else "nw_x_") + i.type[len("nw_type_"):]
            line = f"NW_XNEST(ctx, {site}, {index}, {fn}(&{d}, &{s}, ctx));"
        else:
            line = f"nw_xh{'r' if reverse else ''}_{i.hook}(dst, src, ctx);"
        out, indent = [], "    "
        for v, n in zip(loops, i.dims):
            out.append(f"{indent}for (unsigned {v} = 0; {v} < {n}; {v}++) {{")
            indent += "    "
        out.append(indent + line)
        for _ in loops:
            indent = indent[:-4]
            out.append(indent + "}")
        return out

    def function(self, rid: str, reverse: bool) -> list[str]:
        t, m = type_name(rid), mirror_name(rid)
        head = (f"void nw_xr_{t}({m} *dst, const {rid} *src, NwXCtx *ctx)" if reverse
                else f"void nw_x_{t}({rid} *dst, const {m} *src, NwXCtx *ctx)")
        if self.pod(rid):
            return [head, "{", "    (void)ctx;", "    memcpy(dst, src, sizeof *dst);", "}"]
        body = ["    memset(dst, 0, sizeof *dst);"]
        items = self.items(rid)
        if not any(i.kind in SLOTS + ("rec", "hook") for i in items):
            body.insert(0, "    (void)ctx;")
        for i in items:
            body += self.stmt(i, reverse)
        return [head, "{", *body, "}"]

    def spans(self, rid: str) -> list[tuple]:
        out = []
        for i in self.items(rid):
            if i.kind in ("bytes", "int", "pod"):
                out.append([i.ps2_off, i.host_off, i.ps2_size * (i.count if i.kind == "pod" else 1),
                            i.host_size * (i.count if i.kind == "pod" else 1), 1, "BYTES", "NULL"])
            elif i.kind == "bit":
                first, last = i.ps2_off // 8, (i.ps2_off + i.ps2_size - 1) // 8
                out.append([first, i.host_off // 8, last - first + 1, last - first + 1, 1, "BYTES",
                            "NULL"])
            elif i.kind in SLOTS or (i.kind == "hook" and i.ps2_size):
                out.append([i.ps2_off, i.host_off, i.ps2_size, i.host_size, i.count, "SLOT", "NULL"])
            elif i.kind == "rec":
                out.append([i.ps2_off, i.host_off, i.ps2_size, i.host_size, i.count, "RECORD",
                            f"&{i.type}"])
        out.sort()
        merged: list[list] = []
        for s in out:
            last = merged[-1] if merged else None
            if last and s[5] == last[5] == "BYTES" and s[0] - last[0] == s[1] - last[1] \
                    and s[0] <= last[0] + last[2]:
                last[2] = last[3] = max(last[2], s[0] + s[2] - last[0])
            else:
                merged.append(s)
        return [tuple(s) for s in merged]

    def leaves(self, rid: str) -> list[str]:
        out = []
        for i in self.items(rid):
            kind = {"bytes": "BYTES", "int": "BYTES", "bit": "BIT", "pod": "REC", "ptr": "PTR",
                    "fn": "FN", "addr": "ADDR", "rec": "REC", "hook": "HOOK"}[i.kind]
            count = i.count if i.kind in SLOTS + ("rec", "pod") else 1
            out.append(f'    {{ "{i.path or ""}", NW_LEAF_{kind}, {i.ps2_off}u, {i.host_off}u, '
                       f"{i.ps2_size}u, {i.host_size}u, {count}u, {int(bool(i.dims))}, "
                       f"{int(i.signed)}, {'&' + i.type if i.type else 'NULL'} }},")
        return out

    def render(self) -> dict[str, str]:
        gen = ("/* Generated by tools/gen_converters.py from typedb.json and port/convert.toml; "
               "do not edit. */")
        hdr = [gen, "#ifndef NW_CONVERT_H", "#define NW_CONVERT_H", "",
               '#include "data/convert.h"', '#include "nw_ps2types.h"', ""]
        hdr += [f'#include "{h}"' for h in self.db["headers"]]
        hdr.append("")
        for sym in [*(f"nw_type_{type_name(r)}" for r in self.named), *sorted(self.derived)]:
            hdr.append(f"extern const NwType {sym};")
        for rid in self.named:
            t, m = type_name(rid), mirror_name(rid)
            hdr += ["", f"void nw_x_{t}({rid} *dst, const {m} *src, NwXCtx *ctx);",
                    f"void nw_xr_{t}({m} *dst, const {rid} *src, NwXCtx *ctx);"]
        if self.hooks:
            hdr += ["", "/* Hand-written in src/data/xhooks.c (port/convert.toml); nested ones get the named "
                        "record. */"]
            for stem, top in sorted(self.hooks.items()):
                m = mirror_name(top)
                hdr += [f"void nw_xh_{stem}({top} *dst, const {m} *src, NwXCtx *ctx);",
                        f"void nw_xhr_{stem}({m} *dst, const {top} *src, NwXCtx *ctx);"]
        hdr += ["", "#endif", ""]

        src = [gen, '#include "nw_convert.h"', ""]
        for rid in self.named:
            rec, t, m = self.records[rid], type_name(rid), mirror_name(rid)
            src += ["", f"/* {rid} ({rec['where']}) */",
                    f"_Static_assert(sizeof({rid}) == {rec['host']['size']} && "
                    f"_Alignof({rid}) == {rec['host']['align']}, \"{rid}\");"]
            src += self.mirror.asserts(rid, rid, 0, "", "host")
            src += [""] + self.function(rid, False) + [""] + self.function(rid, True) + [""]
            src += [f"static void nw_xv_{t}(void *dst, const void *src, NwXCtx *ctx) "
                    f"{{ nw_x_{t}(dst, src, ctx); }}",
                    f"static void nw_xrv_{t}(void *dst, const void *src, NwXCtx *ctx) "
                    f"{{ nw_xr_{t}(dst, src, ctx); }}"]
            pod = self.pod(rid)
            spans = [] if pod else self.spans(rid)
            if spans:
                src.append(f"static const NwSpan nw_spans_{t}[] = {{")
                src += [f"    {{ {s[0]}u, {s[1]}u, {s[2]}u, {s[3]}u, {s[4]}u, NW_SPAN_{s[5]}, {s[6]} }},"
                        for s in spans]
                src.append("};")
            flags = " | ".join(f for f, on in (("NW_TYPE_POD", pod), ("NW_TYPE_HOOK", any(
                i.kind == "hook" for i in self.items(rid)))) if on) or "0"
            src.append(f"const NwType nw_type_{t} = {{ \"{rid}\", sizeof({m}), _Alignof({m}), sizeof({rid}), "
                       f"_Alignof({rid}), {flags}, nw_xv_{t}, nw_xrv_{t}, "
                       f"{f'nw_spans_{t}' if spans else 'NULL'}, {len(spans)}u }};")
        for sym, (name, kind, target) in sorted(self.derived.items()):
            stem = sym[len("nw_type_"):]
            src += ["", f"/* {name} */"]
            if kind == "opaque":
                src.append(f'const NwType {sym} = {{ "{name}", 0, 1, 0, 1, NW_TYPE_OPAQUE, NULL, NULL, '
                           "NULL, 0u };")
            elif kind == "array":
                src.append(f'const NwType {sym} = {{ "{name}", {target[0]}, {target[1]}, {target[2]}, '
                           f"{target[3]}, NW_TYPE_POD, NULL, NULL, NULL, 0u }};")
            else:
                src += [f"static void nw_xv_{stem}(void *dst, const void *src, NwXCtx *ctx)", "{",
                        "    uint32_t addr;", "    memcpy(&addr, src, sizeof addr);",
                        f"    *(void **)dst = nw_ptr_in(ctx, addr, &{target}, NULL);", "}", "",
                        f"static void nw_xrv_{stem}(void *dst, const void *src, NwXCtx *ctx)", "{",
                        f"    uint32_t addr = nw_ptr_out(ctx, *(void *const *)src, &{target}, NULL);",
                        "    memcpy(dst, &addr, sizeof addr);", "}", "",
                        f"static const NwSpan nw_spans_{stem}[] = "
                        "{ { 0u, 0u, 4u, sizeof(void *), 1u, NW_SPAN_SLOT, NULL } };",
                        f'const NwType {sym} = {{ "{name}", 4, 4, sizeof(void *), _Alignof(void *), 0, '
                        f"nw_xv_{stem}, nw_xrv_{stem}, nw_spans_{stem}, 1u }};"]
        src.append("")

        test = [gen, "/* The leaves of every converter, for tests/c/test_convert.c. */"]
        rows = []
        for rid in self.named:
            t = type_name(rid)
            leaves = self.leaves(rid)
            if leaves:
                test += [f"static const NwTestLeaf nw_test_leaves_{t}[] = {{", *leaves, "};"]
            rows.append(f"    {{ &nw_type_{t}, {f'nw_test_leaves_{t}' if leaves else 'NULL'}, "
                        f"{len(leaves)}u }},")
        test += ["", "static const NwTestType nw_test_types[] = {", *rows, "    { NULL, NULL, 0u }",
                 "};", ""]
        return {HEADER: "\n".join(hdr), SOURCE: "\n".join(src), TEST_TABLE: "\n".join(test)}


def generate(db: dict, annotations: dict[str, dict]) -> tuple[Gen, dict[str, str]]:
    gen = Gen(db, annotations)
    return gen, {} if gen.errors else gen.render()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--typedb", type=Path, default=nwunits.REPO / "build" / "gen" / "typedb.json")
    parser.add_argument("--annotations", type=Path, default=DEFAULT_ANNOTATIONS)
    parser.add_argument("--out", type=Path, default=nwunits.REPO / "build" / "gen")
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    parser.add_argument("--list-outputs", action="store_true",
                        help="print the files that would be written, relative to --out")
    args = parser.parse_args()
    if args.list_outputs:
        nwunits.print_outputs(args.out, [args.out / n for n in (HEADER, SOURCE, TEST_TABLE)])
        return 0

    try:
        annotations, errors = load_annotations(args.annotations)
        gen, outputs = (None, {}) if errors else generate(json.loads(args.typedb.read_text()),
                                                          annotations)
    except (OSError, ValueError, gen_ps2types.GenError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    errors = errors or gen.errors
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    if errors:
        return 1
    written = 0
    if not args.check:
        for name, text in outputs.items():
            written += nwunits.write_if_changed(args.out / name, text)
    pods = sum(gen.pod(r) for r in gen.named)
    print(f"{len(gen.named)} converters ({pods} POD), {len(gen.hooks)} hooks, "
          f"{len(gen.derived)} pointee types"
          + ("" if args.check else f"; {written} files written to {args.out}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
