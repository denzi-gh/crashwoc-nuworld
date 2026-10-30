#!/usr/bin/env python3
"""Lock the retail functions NuWorld depends on by their decomp text.

copies: an override in port/units.toml drops the decomp's definition and
compiles NuWorld's copy. values: a rule of port/values/*.toml finds its value
by its count in the function, so a reordered body moves it unseen.
port/copies.lock holds a hash of each such decomp definition (its tokens;
comments and spacing ignored). --check fails when the decomp changes one, so it
gets a look (then --update), and when a port patch or value rule edits an
overridden one: that edit never compiles, drop it. A wrap needs no lock: its
decomp C compiles, and <unit>.retail.h makes the compiler check the signature.
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
HEADER = ("# The decomp functions NuWorld depends on by their text, with a hash of the decomp's definition: [copies]\n"
          "# the port/units.toml overrides, [values] the functions with port/values rules. tools/copies.py --check\n"
          "# fails when the decomp changes one: bring NuWorld's copy in line or prove the rules with\n"
          "# tools/ntsc_diff.py check, then --update. A port patch must not edit an overridden one (that never\n"
          "# compiles).\n")
# table: (what its functions are, the look a decomp change needs)
TABLES = {"copies": ("replaced by NuWorld", "bring NuWorld's definition in line"),
          "values": ("with value rules", "prove its rules with tools/ntsc_diff.py check")}


def bodies(tree: Path, source: str, cache: dict) -> dict[str, str]:
    """{function: body hash} of one decomp source."""
    key = (tree, source)
    if key not in cache:
        path = tree / source
        cache[key] = nwunits.body_hashes(path.read_bytes(), source) if path.is_file() else {}
    return cache[key]


def replaced(model: nwunits.Model) -> list[nwunits.Function]:
    """The functions with decomp C whose NuWorld copy takes the calls (overrides)."""
    return [f for u in model.units if u.kind == "wrapper" for f in u.functions
            if f.overridden and f.provider == "port" and f.definition is not None]


def current(decomp: Path, pristine: Path, units: Path, port_src: Path,
            values: Path | None = patches.VALUES) -> tuple[dict[str, dict[str, str]], list[str]]:
    """({table: {function: retail hash}}, errors for port patches that edit an overridden one)."""
    model = nwunits.load(decomp, units, port_src)
    if not nwunits.report(model, sys.stderr):
        raise SystemExit(1)
    cache, errors = {}, []
    locks: dict[str, dict[str, str]] = {name: {} for name in TABLES}
    for f in replaced(model):
        key = f"{f.unit.name}:{f.name}"
        retail = bodies(pristine, f.unit.source, cache).get(f.name)
        if retail is None:
            errors.append(f"{key}: the decomp has no definition of it")
            continue
        locks["copies"][key] = retail
        if bodies(decomp, f.unit.source, cache).get(f.name) != retail:
            errors.append(f"{key}: a port patch or value rule edits it, but NuWorld defines it itself; the edit "
                          "never compiles, drop it")
    for rules in patches.load_values(values):
        for path, sites in rules.sites.items():
            for fn in sorted({s.fn for s in sites}):
                retail = bodies(pristine, path, cache).get(fn)
                if retail is None:
                    errors.append(f"{rules.origin.name}: {path}: the decomp has no definition of {fn}")
                else:
                    locks["values"][f"{path}:{fn}"] = retail
    return locks, errors


def render(locks: dict[str, dict[str, str]]) -> str:
    lines = [HEADER, "schema = 1\n"]
    for name in TABLES:
        lines += [f"\n[{name}]\n"] + [f'"{k}" = "{v[:16]}"\n' for k, v in sorted(locks[name].items())]
    return "".join(lines)


def check(locks: dict[str, dict[str, str]], lock: Path) -> list[str]:
    doc = tomllib.loads(lock.read_text(encoding="utf-8")) if lock.is_file() else {}
    errors = []
    for name, (what, look) in TABLES.items():
        old, new = doc.get(name, {}), locks[name]
        for key in sorted(new.keys() | old.keys()):
            if key not in old:
                errors.append(f"{key}: {what} but not in {lock.name}; --update")
            elif key not in new:
                errors.append(f"{key}: in {lock.name} but no longer {what}; --update")
            elif new[key][:16] != old[key]:
                errors.append(f"{key}: the decomp changed it; {look}, then --update")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    nwunits.add_common_args(parser)
    parser.add_argument("--values", type=Path, default=patches.VALUES, help="the value rules (default: port/values)")
    parser.add_argument("--lock", type=Path, default=LOCK)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--check", action="store_true", help=f"compare with {LOCK.name}")
    mode.add_argument("--update", action="store_true", help=f"rewrite {LOCK.name}")
    args = parser.parse_args()
    pristine = patches.PRISTINE if args.decomp is None else args.decomp
    decomp = nwunits.decomp_tree(args.decomp)
    locks, errors = current(decomp, pristine, args.units, args.port_src, args.values)
    if args.update and not errors:
        nwunits.write_if_changed(args.lock, render(locks))
    elif args.check:
        errors += check(locks, args.lock)
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    print(f"{len(locks['copies'])} decomp functions replaced by NuWorld, {len(locks['values'])} with value rules"
          + ("" if errors else ", all as locked"))
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
