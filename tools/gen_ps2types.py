#!/usr/bin/env python3
"""Generate the PS2 mirror structs nw_ps2_<T> (pointers as u32) from typedb.json."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nwunits  # noqa: E402
import typedb  # noqa: E402

HEADER = "nw_ps2types.h"
TEST_TABLE = "nw_ps2types_test.inc"
INTS = {(1, True): "int8_t", (1, False): "uint8_t", (2, True): "int16_t", (2, False): "uint16_t",
        (4, True): "int32_t", (4, False): "uint32_t", (8, True): "int64_t", (8, False): "uint64_t",
        (16, True): "__int128", (16, False): "unsigned __int128"}
FLOATS = {4: "float", 8: "double"}


class GenError(Exception):
    pass


def type_name(rid: str) -> str:
    return rid.split(" ", 1)[1] if rid.startswith(("struct ", "union ")) else rid


def mirror_name(rid: str) -> str:
    return "nw_ps2_" + type_name(rid)


def nested(rid: str) -> bool:
    return "$" in rid


def leaf(d: dict) -> tuple[dict, list[int]]:
    dims = []
    while d["kind"] == "array":
        dims.append(d["count"])
        d = d["element"]
    return d, dims


class Mirror:
    def __init__(self, db: dict):
        if db.get("schema") != typedb.SCHEMA:
            raise GenError(f"typedb.json has schema {db.get('schema')}, expected {typedb.SCHEMA}: "
                           "regenerate it with tools/typedb.py")
        self.records = {rid: r for rid, r in db["records"].items() if "ps2" in r}
        self.errors: list[str] = []
        self.named = sorted(rid for rid in self.records if not nested(rid))
        for rid in self.records:
            if rid.startswith(("struct $", "union $")):
                self.errors.append(f"{rid}: an untagged record without a typedef has no name")
        owners: dict[str, list[str]] = {}
        for rid in self.named:
            owners.setdefault(mirror_name(rid), []).append(rid)
        for name, rids in sorted(owners.items()):
            if len(rids) > 1:
                self.errors.append(f"{name} would mirror {' and '.join(rids)}")
        for rid in self.records:
            for f in self.records[rid]["fields"]:
                self.check(rid, f)
        self.bitfields: list[tuple[str, str, int, int]] = []    # record, path, bits, bit

    def check(self, rid: str, f: dict) -> None:
        d, _dims = leaf(f)
        where = f"{self.records[rid]['where']}: {rid}.{f['name'] or '<unnamed>'}"
        if d["kind"] in ("long", "other", "void"):
            self.errors.append(f"{where}: {f['type']} has no PS2 mirror type")
        elif d["kind"] == "record" and d["ref"] not in self.records:
            self.errors.append(f"{where}: {d['ref']} has no PS2 layout")

    def order(self) -> list[str]:
        out, done = [], set()

        def deps(rid: str):
            for f in self.records[rid]["fields"]:
                d, _dims = leaf(f)
                if d["kind"] == "record":
                    yield from deps(d["ref"]) if nested(d["ref"]) else (d["ref"],)

        def visit(rid: str) -> None:
            if rid in done:
                return
            done.add(rid)
            for dep in sorted(set(deps(rid))):
                visit(dep)
            out.append(rid)

        for rid in self.named:
            visit(rid)
        return out

    def scalar(self, d: dict) -> str:
        kind = d["kind"]
        if kind in ("pointer", "fnptr"):
            return "uint32_t"
        if kind == "float":
            return FLOATS[d["size"]]
        return INTS[(d["size"], d["signed"])]

    def align(self, rid: str) -> int:
        out = 1
        for f in self.records[rid]["fields"]:
            if "bit" in f["ps2"] and not f["name"]:
                continue                    # unnamed bitfields do not align the record
            d, _dims = leaf(f)
            if d["kind"] == "record":
                out = max(out, self.records[d["ref"]]["ps2"]["align"])
            elif d["kind"] in ("pointer", "fnptr"):
                out = max(out, 4)
            else:
                out = max(out, d["size"])
        return out

    def attribute(self, rid: str) -> str:
        want, have = self.records[rid]["ps2"]["align"], self.align(rid)
        if want < have:
            raise GenError(f"{self.records[rid]['where']}: {rid} is aligned below its members "
                           "(packed records are not mirrored)")
        return f" __attribute__((aligned({want})))" if want > have else ""

    def body(self, rid: str, indent: str) -> list[str]:
        lines = []
        for f in self.records[rid]["fields"]:
            d, counts = leaf(f)
            dims = "".join(f"[{n}]" for n in counts)
            name = f["name"]
            if d["kind"] == "record" and nested(d["ref"]):
                ref = d["ref"]
                kind = self.records[ref]["kind"]
                lines.append(f"{indent}{kind} {{")
                lines += self.body(ref, indent + "    ")
                tail = f" {name}{dims}" if name else ""
                lines.append(f"{indent}}}{self.attribute(ref)}{tail};")
                continue
            ctype = mirror_name(d["ref"]) if d["kind"] == "record" else self.scalar(d)
            decl = f"{ctype} {name}{dims}" if name else ctype
            if "bit" in f["ps2"]:
                decl += f" : {f['ps2']['bits']}"
            note = f" /* {f['type']} */" if d["kind"] in ("pointer", "fnptr") or "typedef" in d \
                else ""
            lines.append(f"{indent}{decl};{note}")
        return lines

    def asserts(self, top: str, rid: str, base: int, prefix: str, view: str = "ps2") -> list[str]:
        out = []
        t = mirror_name(top) if view == "ps2" else top
        for f in self.records[rid]["fields"]:
            lay, path = f[view], prefix + f["name"]
            d, dims = leaf(f)
            if "bit" in lay:
                if f["name"] and view == "ps2":
                    self.bitfields.append((top, path, lay["bits"], base * 8 + lay["bit"]))
                continue
            if f["name"]:
                out.append(f"_Static_assert(offsetof({t}, {path}) == {base + lay['offset']} && "
                           f"sizeof((({t} *)0)->{path}) == {lay['size']}, \"{top}.{path}\");")
            if d["kind"] == "record" and nested(d["ref"]):
                inner = path + "[0]" * len(dims) + "." if f["name"] else prefix
                out += self.asserts(top, d["ref"], base + lay["offset"], inner, view)
        return out

    def render(self) -> tuple[str, str]:
        order = self.order()
        lines = ["/* Generated by tools/gen_ps2types.py from typedb.json; do not edit. */",
                 "/* The PS2 layout of the decomp records: pointers and function pointers are u32",
                 " * PS2 addresses. MinGW needs -mno-ms-bitfields. */",
                 "#ifndef NW_PS2TYPES_H", "#define NW_PS2TYPES_H", "",
                 "#include <stddef.h>", "#include <stdint.h>", ""]
        for rid in self.named:
            kind = self.records[rid]["kind"]
            lines.append(f"typedef {kind} {mirror_name(rid)} {mirror_name(rid)};")
        for rid in order:
            rec = self.records[rid]
            t = mirror_name(rid)
            lines += ["", f"/* {rid} ({rec['where']}) */", f"{rec['kind']} {t} {{"]
            lines += self.body(rid, "    ")
            lines.append(f"}}{self.attribute(rid)};")
            lines.append(f"_Static_assert(sizeof({t}) == {rec['ps2']['size']} && "
                         f"_Alignof({t}) == {rec['ps2']['align']}, \"{rid}\");")
            lines += self.asserts(rid, rid, 0, "")
        lines += ["", "#endif", ""]

        setters, rows = [], []
        for i, (top, path, bits, bit) in enumerate(self.bitfields):
            setters.append(f"static void nw_ps2_bf_{i}(void *p, int v) "
                           f"{{ (({mirror_name(top)} *)p)->{path} = v; }}")
            rows.append(f'    {{ "{top}.{path}", sizeof({mirror_name(top)}), nw_ps2_bf_{i}, '
                        f"{bit}u, {bits}u }},")
        test = ("/* Generated by tools/gen_ps2types.py from typedb.json; do not edit. */\n"
                "/* Every named bitfield of the mirror structs: setter and PS2 bit range. */\n"
                + "".join(s + "\n" for s in setters)
                + "\nstatic const NwPs2Bitfield nw_ps2_bitfields[] = {\n"
                + "".join(r + "\n" for r in rows)
                + "    { NULL, 0, NULL, 0u, 0u }\n};\n")
        return "\n".join(lines), test


def generate(db: dict) -> tuple[Mirror, dict[str, str]]:
    mirror = Mirror(db)
    if mirror.errors:
        return mirror, {}
    try:
        header, test = mirror.render()
    except GenError as e:
        mirror.errors.append(str(e))
        return mirror, {}
    return mirror, {HEADER: header, TEST_TABLE: test}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--typedb", type=Path, default=nwunits.REPO / "build" / "gen" / "typedb.json")
    parser.add_argument("--out", type=Path, default=nwunits.REPO / "build" / "gen")
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    parser.add_argument("--list-outputs", action="store_true",
                        help="print the files that would be written, relative to --out")
    args = parser.parse_args()
    if args.list_outputs:
        nwunits.print_outputs(args.out, [args.out / HEADER, args.out / TEST_TABLE])
        return 0

    try:
        mirror, outputs = generate(json.loads(args.typedb.read_text()))
    except (OSError, ValueError, GenError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    for e in mirror.errors:
        print(f"error: {e}", file=sys.stderr)
    if mirror.errors:
        return 1
    written = 0
    if not args.check:
        for name, text in outputs.items():
            written += nwunits.write_if_changed(args.out / name, text)
    print(f"{len(mirror.named)} mirror structs, {len(mirror.bitfields)} bitfields"
          + ("" if args.check else f"; {written} files written to {args.out}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
