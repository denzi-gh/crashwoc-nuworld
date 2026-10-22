#!/usr/bin/env python3
"""Order the remaining decomp work by stub hits.

Reads the hit lists that `--boot-check` and `--boot` print (one line per
stub or near-match probe: order, stub|near, unit, function, retail address,
hits; other lines are skipped, a list that starts again at 1 is the next run)
and prints

  - every hit, in first-hit order over the runs as given, with what is left
    to do for it: `decomp:stub` (no C in a compiled unit), `decomp:near`
    (near-match C to finish), `port:override|hle|replace|shim` (NuWorld),
    `dead`/`editor` (retail should not get there), `done` (matching C or the
    port since that run);
  - the remaining decomp worklist (`decomp:stub` and `decomp:near` of the
    build), the hit ones first in that order, then the rest by unit and
    address.

  crashwoc-nuworld --boot-check > run1.txt
  python tools/stub_hits.py run1.txt [run2.txt ...]
"""
from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nwunits  # noqa: E402

LINE = re.compile(r"^\s*(\d+)\s+(stub|near)\s+(\S+)\s+(\S+)\s+0x([0-9a-fA-F]{8})\s+(\d+)\s*$")
WORKLIST = ("decomp:stub", "decomp:near")


@dataclass
class Hit:
    rank: int
    unit: str
    name: str
    address: int
    hits: int = 0
    runs: int = 0


def parse_runs(text: str) -> list[list[tuple[int, str, str, int, int]]]:
    """The hit lists in `text`: per run (order, unit, name, address, hits) in first-hit order."""
    runs: list[list[tuple[int, str, str, int, int]]] = []
    last = None
    for line in text.splitlines():
        m = LINE.match(line)
        if not m:
            continue
        order = int(m.group(1))
        if last is None or order <= last:
            runs.append([])
        last = order
        runs[-1].append((order, m.group(3), m.group(4), int(m.group(5), 16), int(m.group(6))))
    for run in runs:
        run.sort()
    return runs


def merge(runs) -> dict[int, Hit]:
    """Hits by retail address; rank 1 is the first hit of the first run, then that run's next ones, then the
    new ones of the next run, ..."""
    hits: dict[int, Hit] = {}
    for run in runs:
        for _order, unit, name, address, count in run:
            hit = hits.setdefault(address, Hit(len(hits) + 1, unit, name, address))
            hit.hits += count
            hit.runs += 1
    return hits


def todo(f: nwunits.Function | None, unit: str = "") -> str:
    """What is left to do for a function of the build."""
    if f is None:
        return "port:shim" if unit == "sdk" else "unknown"
    if f.provider == "decomp":
        return "decomp:near" if f.provenance == "near" else "done"
    if f.provider == "port":
        return "done"
    if f.provider == "shim":
        return "port:shim"
    if f.unit.treatment in ("dead", "editor"):
        return f.unit.treatment
    if f.unit.treatment in ("hle", "replace"):
        return "port:" + f.unit.treatment
    return "port:override" if f.overridden else "decomp:stub"


def render(model: nwunits.Model, hits: dict[int, Hit], nruns: int) -> str:
    by_address = {f.address: f for f in model.functions()}
    out = [f"Hits in first-hit order ({len(hits)} functions, {nruns} run{'' if nruns == 1 else 's'}):",
           f"{'#':>4}  {'function':40} {'unit':32} {'address':10} {'to do':14} {'hits':>7} runs"]
    tally: dict[str, int] = {}
    for h in sorted(hits.values(), key=lambda h: h.rank):
        what = todo(by_address.get(h.address), h.unit)
        tally[what] = tally.get(what, 0) + 1
        out.append(f"{h.rank:4}  {h.name:40} {h.unit:32} 0x{h.address:08x} {what:14} {h.hits:7} {h.runs}")
    if tally:
        out.append("      " + ", ".join(f"{n} {k}" for k, n in sorted(tally.items())))

    order = {u.name: i for i, u in enumerate(model.units)}
    work = [f for f in model.functions() if todo(f) in WORKLIST]
    work.sort(key=lambda f: (hits[f.address].rank if f.address in hits else len(hits) + 1,
                             order[f.unit.name], f.address))
    reached = sum(1 for f in work if f.address in hits)
    out += ["", f"Remaining decomp worklist ({len(work)} functions, {reached} hit):",
            f"{'#':>4}  {'function':40} {'unit':32} {'address':10} to do"]
    for f in work:
        rank = str(hits[f.address].rank) if f.address in hits else "-"
        out.append(f"{rank:>4}  {f.name:40} {f.unit.name:32} 0x{f.address:08x} {todo(f)}")
    return "\n".join(out) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("logs", nargs="+", help="captured output of runs, in order ('-' reads stdin)")
    parser.add_argument("--decomp", type=Path, default=nwunits.DEFAULT_DECOMP,
                        help="decomp checkout (default: the decomp/ submodule)")
    parser.add_argument("--units", type=Path, default=nwunits.DEFAULT_UNITS,
                        help="unit treatments (default: port/units.toml)")
    parser.add_argument("--port-src", type=Path, default=nwunits.DEFAULT_PORT_SRC,
                        help="the port's sources, scanned for definitions (default: src/)")
    parser.add_argument("--editor", action="store_true", help="the runs used an editor build")
    args = parser.parse_args()

    runs = []
    for log in args.logs:
        text = sys.stdin.read() if log == "-" else Path(log).read_text(encoding="utf-8", errors="replace")
        runs += parse_runs(text)
    model = nwunits.load(args.decomp, args.units, args.port_src, args.editor)
    if not nwunits.report(model, sys.stderr):
        return 1
    sys.stdout.write(render(model, merge(runs), len(runs)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
