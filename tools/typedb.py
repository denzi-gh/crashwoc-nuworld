#!/usr/bin/env python3
"""Type DB: every struct/union, typedef and enum of decomp/include with its
PS2 layout (mipsel, as the decomp compiles) and host layout (x86_64, NW_HOST).

Writes typedb.json under --out. Records are keyed by tag (`struct nuvec_s`),
unnamed ones by `<parent>$<n>` in declaration order. Offsets are bytes; a
bitfield also has `bit` (from the record start) and `bits`. Lint: `long`
(8 bytes on the EE, 4 in o32, so not modelled) and u128 fields.
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nwclang  # noqa: E402
import nwunits  # noqa: E402

cindex = nwclang.cindex

SCHEMA = 1
TU_NAME = "__nw_typedb__.c"
UNNAMED = re.compile(r"\((?:anonymous|unnamed)\b[^)]*\)")


class TypeDBError(Exception):
    pass


def headers(decomp: Path) -> list[str]:
    inc = decomp / "include"
    return sorted(p.relative_to(inc).as_posix() for p in inc.rglob("*.h"))


class View:
    """Records, typedefs and enums of one view."""

    def __init__(self, clang: nwclang.Clang, names: list[str]):
        self.include = clang.include.resolve()
        tu = clang.parse(clang.include / TU_NAME, "".join(f'#include "{h}"\n' for h in names))
        errors = clang.errors(tu)
        if errors:
            raise TypeDBError(f"{clang.view} view: " + "\n".join(errors[:20]))
        self.ids: dict[tuple, str] = {}
        self.records: dict[str, dict] = {}
        self.typedefs: dict[str, dict] = {}
        self.enums: dict[str, dict] = {}
        self.lint: list[dict] = []
        top = [c for c in tu.cursor.get_children() if self.where(c)]
        for i, c in enumerate(top):
            if c.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL) \
                    and c.is_definition():
                self.record(c, self.top_name(c, top[i + 1:]))
        for c in top:
            if c.kind == cindex.CursorKind.TYPEDEF_DECL:
                t = c.underlying_typedef_type
                self.typedefs[c.spelling] = {"where": self.where(c), "type": self.spell(t),
                                             **self.describe(t)}
            elif c.kind == cindex.CursorKind.ENUM_DECL and c.is_definition():
                consts = {e.spelling: e.enum_value for e in c.get_children()}
                tag = c.spelling if not c.is_anonymous() else "$" + next(iter(consts), "")
                self.enums[f"enum {tag}"] = {"where": self.where(c), "constants": consts}

    def where(self, c) -> str | None:
        f = c.location.file
        if f is None:
            return None
        try:
            rel = Path(f.name).resolve().relative_to(self.include).as_posix()
        except ValueError:
            return None
        return None if rel == TU_NAME else f"{rel}:{c.location.line}"

    def key(self, c) -> tuple:
        loc = c.location
        return (loc.file.name if loc.file else "", loc.offset, c.kind.name)

    @staticmethod
    def tagged(c) -> bool:
        """clang names `typedef struct {...} T` after T; the tokens show no tag."""
        if c.is_anonymous():
            return False
        tokens = [t.spelling for t in c.get_tokens()]
        return len(tokens) > 1 and tokens[1] != "{"

    def top_name(self, c, rest) -> str:
        kind = "struct" if c.kind == cindex.CursorKind.STRUCT_DECL else "union"
        if self.tagged(c):
            return f"{kind} {c.spelling}"
        for n in rest:
            if n.kind == cindex.CursorKind.TYPEDEF_DECL and \
                    self.key(n.underlying_typedef_type.get_canonical().get_declaration()) == self.key(c):
                return n.spelling
        return f"{kind} ${self.where(c)}"

    def record(self, c, rid: str) -> None:
        self.ids[self.key(c)] = rid
        n = 0
        for ch in c.get_children():
            if ch.kind in (cindex.CursorKind.STRUCT_DECL, cindex.CursorKind.UNION_DECL) \
                    and ch.is_definition():
                if ch.is_anonymous():
                    n += 1
                    self.record(ch, f"{rid}${n}")
                else:
                    kind = "struct" if ch.kind == cindex.CursorKind.STRUCT_DECL else "union"
                    self.record(ch, f"{kind} {ch.spelling}")
        fields = []
        for f in c.type.get_fields():
            name = f.spelling if f.spelling.isidentifier() else ""
            layout = {"offset": f.get_field_offsetof() // 8, "size": f.type.get_size()}
            if f.is_bitfield():
                layout.update(bit=f.get_field_offsetof(), bits=f.get_bitfield_width())
            desc = self.describe(f.type)
            fields.append({"name": name, "type": self.spell(f.type), "where": self.where(f),
                           "layout": layout, **desc})
            for kind in self.kinds(desc):
                if kind in ("long", "u128"):
                    self.lint.append({"kind": kind, "record": rid, "field": name,
                                      "where": self.where(f)})
        self.records[rid] = {
            "kind": "struct" if c.kind == cindex.CursorKind.STRUCT_DECL else "union",
            "where": self.where(c), "size": c.type.get_size(), "align": c.type.get_align(),
            "fields": fields,
        }

    @staticmethod
    def kinds(desc: dict):
        while desc:
            yield desc["kind"]
            desc = desc.get("element")

    def ref(self, t) -> str:
        decl = t.get_canonical().get_declaration()
        return self.ids.get(self.key(decl)) or self.spell(t.get_canonical())

    def spell(self, t) -> str:
        """Type spelling without clang's path-bearing names for unnamed types."""
        s = t.spelling
        if not UNNAMED.search(s):
            return s
        canon = t.get_canonical()
        k = canon.kind
        if k == cindex.TypeKind.RECORD:
            return self.ids.get(self.key(canon.get_declaration()), UNNAMED.sub("$", s))
        if k == cindex.TypeKind.CONSTANTARRAY:
            return f"{self.spell(canon.element_type)}[{canon.element_count}]"
        if k == cindex.TypeKind.POINTER:
            return self.spell(canon.get_pointee()) + " *"
        return UNNAMED.sub("$", s)

    def describe(self, t) -> dict:
        canon = t.get_canonical()
        k = canon.kind
        T = cindex.TypeKind
        if k == T.POINTER:
            pointee = canon.get_pointee().get_canonical()
            if pointee.kind in (T.FUNCTIONPROTO, T.FUNCTIONNOPROTO):
                return {"kind": "fnptr"}
            out = {"kind": "pointer", "pointee": self.spell(pointee)}
            if pointee.kind == T.RECORD:
                out["pointee"] = self.ref(pointee)
            return out
        if k in (T.CONSTANTARRAY, T.INCOMPLETEARRAY):
            count = canon.element_count if k == T.CONSTANTARRAY else 0
            return {"kind": "array", "count": count, "element": self.describe(canon.element_type)}
        if k == T.RECORD:
            return {"kind": "record", "ref": self.ref(canon)}
        if k == T.ENUM:
            return {"kind": "enum"}
        if k in (T.FLOAT, T.DOUBLE, T.LONGDOUBLE):
            return {"kind": "float"}
        if k in (T.INT128, T.UINT128):
            return {"kind": "u128"}
        if k in (T.LONG, T.ULONG):
            return {"kind": "long"}
        if k in (T.BOOL, T.CHAR_U, T.UCHAR, T.CHAR_S, T.SCHAR, T.USHORT, T.SHORT, T.UINT,
                 T.INT, T.ULONGLONG, T.LONGLONG, T.CHAR16, T.CHAR32, T.WCHAR):
            return {"kind": "int", "signed": k in (T.CHAR_S, T.SCHAR, T.SHORT, T.INT,
                                                   T.LONGLONG, T.WCHAR)}
        return {"kind": "other"}


def merge(ps2: View, host: View) -> dict:
    """PS2 view with the host layout added as `host` next to each `layout`."""
    records = {}
    for rid in sorted(set(ps2.records) | set(host.records)):
        p, h = ps2.records.get(rid), host.records.get(rid)
        base = p or h
        out = {"kind": base["kind"], "where": base["where"], "fields": []}
        if p:
            out["ps2"] = {"size": p["size"], "align": p["align"]}
        if h:
            out["host"] = {"size": h["size"], "align": h["align"]}
        if p and h and [f["name"] for f in p["fields"]] != [f["name"] for f in h["fields"]]:
            raise TypeDBError(f"{rid}: the PS2 and host views have different fields")
        for i, f in enumerate(base["fields"]):
            field = {k: v for k, v in f.items() if k != "layout"}
            if p:
                field["ps2"] = p["fields"][i]["layout"]
            if h:
                field["host"] = h["fields"][i]["layout"]
            out["fields"].append(field)
        records[rid] = out
    return records


def decomp_sha(decomp: Path) -> str | None:
    if not (decomp / ".git").exists():
        return None
    try:
        out = subprocess.run(["git", "-C", str(decomp), "rev-parse", "HEAD"],
                             capture_output=True, text=True, check=True)
    except (OSError, subprocess.CalledProcessError):
        return None
    return out.stdout.strip() or None


def build(decomp: Path) -> dict:
    names = headers(decomp)
    views = {}
    for view in ("ps2", "host"):
        clang = nwclang.Clang(decomp, view)
        try:
            views[view] = View(clang, names)
        finally:
            clang.close()
    ps2 = views["ps2"]
    return {
        "schema": SCHEMA,
        "decomp": decomp_sha(decomp),
        "headers": names,
        "records": merge(ps2, views["host"]),
        "typedefs": ps2.typedefs,
        "enums": ps2.enums,
        "lint": ps2.lint,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--decomp", type=Path, default=nwunits.DEFAULT_DECOMP,
                        help="decomp checkout (default: the decomp/ submodule)")
    parser.add_argument("--out", type=Path, default=nwunits.REPO / "build" / "gen")
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    args = parser.parse_args()

    try:
        db = build(args.decomp)
    except (nwclang.ClangError, TypeDBError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    for item in db["lint"]:
        print(f"warning: {item['where']}: {item['record']}.{item['field'] or '<unnamed>'} "
              f"is {item['kind']}", file=sys.stderr)
    written = ""
    if not args.check:
        path = args.out / "typedb.json"
        changed = nwunits.write_if_changed(path, json.dumps(db, indent=1, sort_keys=True) + "\n")
        written = f"; {path} {'written' if changed else 'unchanged'}"
    nfields = sum(len(r["fields"]) for r in db["records"].values())
    print(f"{len(db['records'])} records, {nfields} fields, {len(db['typedefs'])} typedefs, "
          f"{len(db['enums'])} enums, {len(db['lint'])} lint{written}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
