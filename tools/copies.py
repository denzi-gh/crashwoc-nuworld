#!/usr/bin/env python3
"""Lock the retail functions NuWorld defines itself in place of the decomp C.

An override in port/units.toml drops the decomp's definition and compiles
NuWorld's; a wrap keeps it as <name>__retail behind NuWorld's. port/copies.lock
holds a hash of each such decomp definition (its tokens; comments and spacing
ignored). --check fails when the decomp changes one, so its NuWorld definition
gets a look (then --update), and when a port patch edits an overridden one:
that edit never compiles, drop it from the patch.
"""
from __future__ import annotations

import argparse
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nwunits  # noqa: E402
import patches  # noqa: E402

LOCK = nwunits.REPO / "port" / "copies.lock"
HEADER = ("# The decomp functions NuWorld defines itself (port/units.toml overrides and wraps), with a hash of the\n"
          "# decomp's definition. tools/copies.py --check fails when the decomp changes one: look at NuWorld's\n"
          "# definition, then --update. A port patch must not edit an overridden one (the edit never compiles).\n")


def bodies(tree: Path, source: str, cache: dict) -> dict[str, str]:
    """{function: body hash} of one decomp source; latin-1 keeps every byte the same on every OS."""
    key = (tree, source)
    if key not in cache:
        path = tree / source
        text = path.read_bytes().decode("latin-1").replace("\r\n", "\n") if path.is_file() else ""
        cache[key] = {d.name: d.body for d in nwunits.scan_c(text, source).decls if d.is_def}
    return cache[key]


def replaced(model: nwunits.Model) -> list[nwunits.Function]:
    """The functions with decomp C whose NuWorld definition takes the calls (overrides and wraps)."""
    return [f for u in model.units if u.kind == "wrapper" for f in u.functions
            if (f.overridden or f.wrapped) and f.provider == "port" and f.definition is not None]


def current(decomp: Path, pristine: Path, units: Path, port_src: Path) -> tuple[dict[str, str], list[str]]:
    """({unit:function: retail hash}, errors for port patches that edit one)."""
    model = nwunits.load(decomp, units, port_src)
    if not nwunits.report(model, sys.stderr):
        raise SystemExit(1)
    cache, hashes, errors = {}, {}, []
    for f in replaced(model):
        key = f"{f.unit.name}:{f.name}"
        retail = bodies(pristine, f.unit.source, cache).get(f.name)
        patched = bodies(decomp, f.unit.source, cache).get(f.name)
        if retail is None:
            errors.append(f"{key}: the decomp has no definition of it")
            continue
        hashes[key] = retail
        if patched != retail and f.overridden:  # a wrap keeps the decomp C, patched or not
            errors.append(f"{key}: a port patch edits it, but NuWorld defines it itself; the edit never compiles, "
                          "drop it from the patch")
    return hashes, errors


def render(hashes: dict[str, str]) -> str:
    lines = [HEADER, "schema = 1\n\n[copies]\n"]
    lines += [f'"{k}" = "{v[:16]}"\n' for k, v in sorted(hashes.items())]
    return "".join(lines)


def check(hashes: dict[str, str], lock: Path) -> list[str]:
    old = tomllib.loads(lock.read_text(encoding="utf-8")).get("copies", {}) if lock.is_file() else {}
    errors = []
    for key in sorted(hashes.keys() | old.keys()):
        if key not in old:
            errors.append(f"{key}: replaced by NuWorld but not in {lock.name}; --update")
        elif key not in hashes:
            errors.append(f"{key}: in {lock.name} but no longer replaced; --update")
        elif hashes[key][:16] != old[key]:
            errors.append(f"{key}: the decomp changed it; bring NuWorld's definition in line, then --update")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    nwunits.add_common_args(parser)
    parser.add_argument("--lock", type=Path, default=LOCK)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help=f"compare with {LOCK.name}")
    mode.add_argument("--update", action="store_true", help=f"rewrite {LOCK.name}")
    args = parser.parse_args()
    pristine = patches.PRISTINE if args.decomp is None else args.decomp
    decomp = nwunits.decomp_tree(args.decomp)
    hashes, errors = current(decomp, pristine, args.units, args.port_src)
    if args.update and not errors:
        nwunits.write_if_changed(args.lock, render(hashes))
    elif args.check:
        errors += check(hashes, args.lock)
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    print(f"{len(hashes)} decomp functions replaced by NuWorld" + ("" if errors else ", all as locked"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
