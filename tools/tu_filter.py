#!/usr/bin/env python3
"""Generate the wrapper TU of every compiled decomp unit.

Writes <unit>.wrap.c under --out: the unit's source, then <unit>.stubs.inc and
<unit>.funcmap.inc. libclang locates the definitions to rename: an overridden
Foo becomes Foo__ps2 behind a prototype of the port's Foo (callers below it
bind to the port), a `rename` entry gets its host name. #line keeps
diagnostics and debug info on the decomp source.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nwclang  # noqa: E402
import nwunits  # noqa: E402

cindex = nwclang.cindex

SUFFIX = "__ps2"
INLINE_WORDS = frozenset(("inline", "__inline", "__inline__"))


class FilterError(Exception):
    pass


class Parser:
    """The host view of each unit; bodies may have errors, only the positions
    of definitions matter here."""

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
    """Token spellings, one space where the source had a gap (drops comments);
    `swap` replaces the token at an offset."""
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


def filter_source(data: bytes, unit: nwunits.Unit, defs: dict, source: str) -> tuple[bytes, list[str]]:
    """Rename the unit's overridden/renamed definitions; returns (text, renamed)."""
    newline = b"\r\n" if b"\r\n" in data else b"\n"
    edits = []                              # (offset, delete, insert)
    renamed = []
    for f in unit.functions:
        if not (f.overridden or f.host != f.name):
            continue
        c = defs.get(f.name)
        if c is None:
            continue
        off = c.location.offset
        if data[off:off + len(f.name)] != f.name.encode():
            raise FilterError(f"{unit.name}: {f.name}: the name is not a plain token (macro?)")
        if f.overridden:
            start = c.extent.start.offset
            line_start = data.rfind(b"\n", 0, start) + 1
            if data[line_start:start].strip():
                raise FilterError(f"{unit.name}: {f.name}: the definition does not start a line")
            insert = (prototype(c, f.host).encode() + newline
                      + _line_directive(c.extent.start.line, source).encode() + newline)
            edits.append((line_start, 0, insert))
            new = f.name + SUFFIX
        else:
            new = f.host
        edits.append((off, len(f.name), new.encode()))
        renamed.append(f"{f.name} -> {new}")
    out = bytearray(data)
    for off, delete, insert in sorted(edits, key=lambda e: e[0], reverse=True):
        out[off:off + delete] = insert
    return bytes(out), renamed


def check(unit: nwunits.Unit, defs: dict, errors: list[str]) -> None:
    """nwunits (regex scan) and libclang must agree on what the unit defines."""
    for f in unit.functions:
        if (f.definition is not None) != (f.name in defs):
            seen = "libclang" if f.name in defs else "nwunits"
            errors.append(f"{unit.name}: only {seen} sees a definition of {f.name}")


def render(unit: nwunits.Unit, data: bytes, defs: dict, source: str,
           wrapper: str) -> tuple[bytes, list[str]]:
    body, renamed = filter_source(data, unit, defs, source)
    if not body.endswith(b"\n"):
        body += b"\n"
    head = (f"/* Generated by tools/tu_filter.py for decomp unit {unit.name}; do not edit. */\n"
            + _line_directive(1, source) + "\n").encode()
    text = head + body
    stem = unit.name.rsplit("/", 1)[-1]
    tail = (_line_directive(text.count(b"\n") + 2, wrapper) + "\n"
            f'#include "{stem}.stubs.inc"\n'
            f'#include "{stem}.funcmap.inc"\n')
    return text + tail.encode(), renamed


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
    parser.add_argument("--list", action="store_true", help="print every renamed definition")
    args = parser.parse_args()

    model = nwunits.load(args.decomp, args.units, args.port_src, args.editor)
    if not nwunits.report(model, sys.stderr):
        return 1
    wrappers = [u for u in model.units if u.kind == "wrapper"]
    errors, written, renamed = [], 0, 0
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
            except FilterError as e:
                errors.append(str(e))
                continue
            renamed += len(names)
            if args.list:
                for n in names:
                    print(f"{unit.name:32} {n}")
            if not args.check:
                written += write_if_changed(out, text)
    finally:
        clang.close()
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    if errors:
        return 1
    print(f"{len(wrappers)} wrapper units, {renamed} definitions renamed"
          + ("" if args.check else f"; {written} files written to {args.out}"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
