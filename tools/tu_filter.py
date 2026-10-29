#!/usr/bin/env python3
"""Generate the wrapper TU of every compiled decomp unit.

Writes <unit>.wrap.c under --out: the port prelude (engine and game units),
the unit's source, then <unit>.stubs.inc, <unit>.funcmap.inc and
<unit>.data.inc (tools/gen_dataimage.py). libclang
locates the definitions: an overridden Foo is dropped and only a prototype of
the port's Foo (or its stub) stays, so callers bind to the port; a `rename`
entry renames the function's declarations, definition and calls in the
source; a wrapped Foo keeps its definition as Foo__retail (declared in
<unit>.retail.h) behind a prototype of the port's Foo, which takes every
call; a near-match definition starts with an NW_NEAR probe (port/stub.h),
so the stub telemetry also orders that decomp work. #line keeps diagnostics
and debug info on the decomp source.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nwclang  # noqa: E402
import nwunits  # noqa: E402

cindex = nwclang.cindex

INLINE_WORDS = frozenset(("inline", "__inline", "__inline__"))
PRELUDE = "port/nw_prelude.h"
NEWLIB = "port/nw_newlib.h"
DIRECTIVE = re.compile(rb"^[ \t]*#", re.M)


class FilterError(Exception):
    pass


class Parser:
    """The host view of each unit; only the positions of definitions matter."""

    def __init__(self, decomp: Path):
        try:
            self.clang = nwclang.Clang(decomp, "host")
        except nwclang.ClangError as e:
            raise FilterError(str(e)) from None

    def close(self) -> None:
        self.clang.close()

    def definitions(self, path: Path) -> dict:
        """Top-level function definitions in the file itself, by name."""
        try:
            tu = self.clang.parse(path)
        except nwclang.ClangError as e:
            raise FilterError(str(e)) from None
        main = Path(tu.spelling).resolve()
        out = {}
        for c in tu.cursor.get_children():
            if (c.kind == cindex.CursorKind.FUNCTION_DECL and c.is_definition()
                    and c.location.file and Path(c.location.file.name).resolve() == main):
                out[c.spelling] = c
        return out


def _line_directive(line: int, path: str) -> str:
    return '#line %d "%s"' % (line, path.replace("\\", "\\\\").replace('"', '\\"'))


def _join(tokens, swap: dict[int, str]) -> str:
    """Token spellings joined by single spaces; `swap` replaces the token at an offset."""
    out, end = [], None
    for t in tokens:
        start = t.extent.start.offset
        if end is not None and start > end:
            out.append(" ")
        out.append(swap.get(start, t.spelling))
        end = t.extent.end.offset
    return "".join(out)


def prototype(cursor, host: str) -> str:
    """Declaration of `host` with the signature of the definition at `cursor`."""
    body = next((c for c in cursor.get_children()
                 if c.kind == cindex.CursorKind.COMPOUND_STMT), None)
    if body is None:
        raise FilterError(f"{cursor.spelling}: definition without a body")
    name_at = cursor.location.offset
    head = [t for t in cursor.get_tokens()
            if t.extent.start.offset < body.extent.start.offset
            and t.kind != cindex.TokenKind.COMMENT and t.spelling not in INLINE_WORDS]
    i = next((i for i, t in enumerate(head) if t.extent.start.offset == name_at), None)
    if i is None or head[i].spelling != cursor.spelling:
        raise FilterError(f"{cursor.spelling}: the name is not a plain token (macro?)")
    if any(t.spelling == "#" for t in head):
        raise FilterError(f"{cursor.spelling}: preprocessor directive inside the signature")
    if any(t.spelling == ";" for t in head):
        # K&R definition: its identifier list is no prototype.
        return _join(head[:i + 1], {name_at: host}) + "();"
    return _join(head, {name_at: host}) + ";"


def references(cursor, name: str, size: int) -> list[int]:
    """Offsets of the function's declarations, definition and calls in the main file."""
    tu = cursor.translation_unit
    extent = tu.get_extent(tu.spelling, (0, size))
    out = []
    for t in tu.get_tokens(extent=extent):
        if t.kind != cindex.TokenKind.IDENTIFIER or t.spelling != name:
            continue
        ref = cindex.Cursor.from_location(tu, t.location).referenced
        if ref is None:
            raise FilterError(f"{name}: line {t.location.line} uses the name where libclang "
                              "sees no declaration (macro?)")
        if ref.kind == cindex.CursorKind.FUNCTION_DECL and ref.spelling == name:
            out.append(t.extent.start.offset)
    return out


def retail_name(name: str) -> str:
    return name + "__retail"


def retail_header(unit: nwunits.Unit, defs: dict) -> str | None:
    """<unit>.retail.h: prototypes of the decomp definitions the unit's wraps keep, or None without wraps."""
    protos = [prototype(defs[f.name], retail_name(f.name)) for f in unit.functions if f.wrapped and f.name in defs]
    if not protos:
        return None
    guard = "NW_RETAIL_" + re.sub(r"[^A-Za-z0-9]", "_", unit.name).upper() + "_H"
    tags = sorted(set(re.findall(r"\b((?:struct|union)\s+[A-Za-z_]\w*)", " ".join(protos))))
    lines = [f"/* Generated by tools/tu_filter.py for decomp unit {unit.name}; do not edit. The decomp's",
             "   definitions of the functions port/units.toml wraps; include it after the headers of their types. */",
             f"#ifndef {guard}", f"#define {guard}", ""]
    lines += [f"{t};" for t in tags] + ([""] if tags else []) + protos + ["", f"#endif /* {guard} */", ""]
    return "\n".join(lines)


def retail_path(out: Path, unit: nwunits.Unit) -> Path:
    return out / (unit.name + ".retail.h")


def probed(unit: nwunits.Unit) -> list[nwunits.Function]:
    """Near-match decomp C the wrapper counts (port/stub.h NW_NEAR): telemetry for the decomp work."""
    if not wants_prelude(unit):
        return []
    return [f for f in unit.functions if f.provider == "decomp" and f.provenance == "near"]


def filter_source(data: bytes, unit: nwunits.Unit, defs: dict, source: str) -> tuple[bytes, list[str]]:
    """Drop overridden definitions, apply renames and probe near-match C; returns (text, changes)."""
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    edits = []                              # (offset, delete, insert)
    dropped = []                            # (start, end) of removed definitions
    renames = []                            # (offset, old, new)
    changes = []
    for f in probed(unit):
        c = defs.get(f.name)
        body = c and next((b for b in c.get_children() if b.kind == cindex.CursorKind.COMPOUND_STMT), None)
        if body is None or data[body.extent.start.offset:body.extent.start.offset + 1] != b"{":
            raise FilterError(f"{unit.name}: {f.name}: no plain body to probe (macro?)")
        probe = f' NW_NEAR("{unit.name}", "{f.name}", 0x{f.address:08x}u);'
        edits.append((body.extent.start.offset + 1, 0, probe.encode()))
        changes.append(f"{f.name} probed")
    for f in unit.functions:
        if not (f.overridden or f.wrapped or f.host != f.name):
            continue
        c = defs.get(f.name)
        if c is None:
            continue
        off = c.location.offset
        if data[off:off + len(f.name)] != f.name.encode():
            raise FilterError(f"{unit.name}: {f.name}: the name is not a plain token (macro?)")
        if f.overridden:
            start, end = c.extent.start.offset, c.extent.end.offset
            line_start = data.rfind(b"\n", 0, start) + 1
            if data[line_start:start].strip():
                raise FilterError(f"{unit.name}: {f.name}: the definition does not start a line")
            line_end = data.find(b"\n", end)
            line_end = len(data) if line_end < 0 else line_end + 1
            if data[end:line_end].strip():
                raise FilterError(f"{unit.name}: {f.name}: the definition does not end its line")
            if DIRECTIVE.search(data, start, end):
                raise FilterError(f"{unit.name}: {f.name}: preprocessor directive inside the "
                                  "definition")
            insert = (prototype(c, f.host).encode() + newline
                      + _line_directive(c.extent.end.line + 1, source).encode() + newline)
            edits.append((line_start, line_end - line_start, insert))
            dropped.append((line_start, line_end))
            changes.append(f"{f.name} dropped" + ("" if f.host == f.name else f", -> {f.host}"))
        elif f.wrapped:
            # The definition becomes <name>__retail; a prototype of <name> in front of it keeps the calls below
            # bound to the port's. The rename goes first: both edits may start at the same offset.
            line_start = data.rfind(b"\n", 0, c.extent.start.offset) + 1
            if data[line_start:c.extent.start.offset].strip():
                raise FilterError(f"{unit.name}: {f.name}: the definition does not start a line")
            edits.append((off, len(f.name), retail_name(f.name).encode()))
            edits.append((line_start, 0, prototype(c, f.name).encode() + newline
                          + _line_directive(c.extent.start.line, source).encode() + newline))
            changes.append(f"{f.name} wrapped, the decomp's is {retail_name(f.name)}")
        else:
            changes.append(f"{f.name} -> {f.host}")
        if f.host != f.name:
            try:
                offsets = references(c, f.name, len(data))
            except FilterError as e:
                raise FilterError(f"{unit.name}: {e}") from None
            renames.extend((o, f.name, f.host) for o in offsets)
    edits.extend((o, len(old), new.encode()) for o, old, new in renames
                 if not any(s <= o < e for s, e in dropped))
    out = bytearray(data)
    for off, delete, insert in sorted(edits, key=lambda e: e[0], reverse=True):
        out[off:off + delete] = insert
    return bytes(out), changes


def check(unit: nwunits.Unit, defs: dict, errors: list[str]) -> None:
    """nwunits (regex scan) and libclang must agree on what the unit defines."""
    for f in unit.functions:
        if (f.definition is not None) != (f.name in defs):
            seen = "libclang" if f.name in defs else "nwunits"
            errors.append(f"{unit.name}: only {seen} sees a definition of {f.name}")


def wants_prelude(unit: nwunits.Unit) -> bool:
    """Engine and game units only: host <math.h> breaks libm's isinf/isnan/finite."""
    return not unit.name.startswith("sdk/")


def prelude(unit: nwunits.Unit) -> str:
    """Lines the wrapper starts with: the port prelude, or the newlib renames for newlib units."""
    if wants_prelude(unit):
        return f'#include "{PRELUDE}"\n'
    if unit.name.startswith("sdk/newlib/"):
        return f'#define NW_NEWLIB_UNIT\n#include "{NEWLIB}"\n'
    return ""


def render(unit: nwunits.Unit, data: bytes, defs: dict, source: str,
           wrapper: str) -> tuple[bytes, list[str]]:
    body, changes = filter_source(data, unit, defs, source)
    if not body.endswith(b"\n"):
        body += b"\n"
    probes = '#include "port/stub.h"\n' if probed(unit) else ""
    head = (f"/* Generated by tools/tu_filter.py for decomp unit {unit.name}; do not edit. */\n"
            + prelude(unit) + probes + _line_directive(1, source) + "\n").encode()
    text = head + body
    stem = unit.name.rsplit("/", 1)[-1]
    tail = (_line_directive(text.count(b"\n") + 2, wrapper) + "\n"
            f'#include "{stem}.stubs.inc"\n'
            f'#include "{stem}.funcmap.inc"\n'
            f'#include "{stem}.data.inc"\n')
    return text + tail.encode(), changes


def output_path(out: Path, unit: nwunits.Unit) -> Path:
    return out / (unit.name + ".wrap.c")


def write_if_changed(path: Path, data: bytes) -> bool:
    if path.is_file() and path.read_bytes() == data:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    nwunits.add_common_args(parser)
    parser.add_argument("--out", type=Path, default=nwunits.REPO / "build" / "gen")
    parser.add_argument("--check", action="store_true", help="validate only, write nothing")
    parser.add_argument("--list", action="store_true",
                        help="print every dropped or renamed definition")
    args = parser.parse_args()
    args.decomp = nwunits.decomp_tree(args.decomp)

    model = nwunits.load(args.decomp, args.units, args.port_src, args.editor)
    if not nwunits.report(model, sys.stderr):
        return 1
    wrappers = [u for u in model.units if u.kind == "wrapper"]
    if args.list_outputs:
        nwunits.print_outputs(args.out, [output_path(args.out, u) for u in wrappers]
                              + [retail_path(args.out, u) for u in wrappers if any(f.wrapped for f in u.functions)])
        return 0
    errors, written, changed = [], 0, 0
    try:
        clang = Parser(args.decomp)
    except FilterError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    try:
        for unit in wrappers:
            path = args.decomp / unit.source
            out = output_path(args.out, unit)
            try:
                defs = clang.definitions(path)
                check(unit, defs, errors)
                text, names = render(unit, path.read_bytes(), defs, path.resolve().as_posix(),
                                     out.resolve().as_posix())
                header = retail_header(unit, defs)
            except FilterError as e:
                errors.append(str(e))
                continue
            changed += len(names)
            if args.list:
                for n in names:
                    print(f"{unit.name:32} {n}")
            if not args.check:
                written += write_if_changed(out, text)
                if header is not None:
                    written += write_if_changed(retail_path(args.out, unit), header.encode())
    finally:
        clang.close()
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    if errors:
        return 1
    print(f"{len(wrappers)} wrapper units, {changed} definitions dropped, renamed or wrapped"
          + ("" if args.check else f"; {written} files written to {args.out}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
