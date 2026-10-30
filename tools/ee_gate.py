#!/usr/bin/env python3
"""Prove that the port patches leave the PS2 build unchanged.

Compiles the decomp's `current` objects (src/ with the EE compilers, no game
files) from the plain decomp and from the patched tree, both exported under
build/ee, and compares them with the decomp's own check_codegen.py: every seam
must expand to the decomp's text without NW_HOST. The plain snapshot is cached
by the export's content.

The EE build never sees host-only code, so the #ifdef NW_HOST blocks each
concern adds are counted against port/ee_gate.toml; a change there is a
deliberate, reviewed one. --lint runs only that count (no compiler).

Runs in the decomp image; on a host it re-runs itself there with docker (repo
at /nw, the submodule's EE toolchain mounted). Needs the toolchain once:
python decomp/tools/setup_toolchain.py --download (in the image).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import patches  # noqa: E402

REPO = patches.REPO
EE = REPO / "build" / "ee"
COUNTS = REPO / "port" / "ee_gate.toml"
IMAGE = os.environ.get("CRASHWOC_IMAGE", "crashwoc-decomp")
HOST_BLOCK = re.compile(rb"\s*#\s*if(n?def\s+NW_HOST\b|\s+!?\s*defined\s*\(?\s*NW_HOST\b)")


def in_container() -> bool:
    return Path("/.dockerenv").exists() or os.environ.get("NUWORLD_EE_DIRECT") == "1"


def host_blocks(series: Path = patches.PATCHES) -> dict[str, int]:
    """The #ifdef / #ifndef NW_HOST blocks each concern adds."""
    concerns, _ = patches.load_series(series)
    out = {}
    for c in concerns:
        n = 0
        for p in c.patches:
            for h in p.hunks:
                n += sum(1 for ln in h.new if HOST_BLOCK.match(ln)) - sum(1 for ln in h.old if HOST_BLOCK.match(ln))
        out[c.name] = n
    return out


def lint(series: Path = patches.PATCHES, counts: Path = COUNTS) -> list[str]:
    want = tomllib.loads(counts.read_text(encoding="utf-8")).get("host_blocks", {})
    got = host_blocks(series)
    errors = []
    for name in sorted(want.keys() | got.keys()):
        w, g = want.get(name, 0), got.get(name, 0)
        if w != g:
            errors.append(f"{name}: the patches add {g} #ifdef NW_HOST blocks, {counts.name} says {w}; "
                          "update it in the same change and say why")
    return errors


def exports() -> str:
    """Exports the plain and the patched decomp under build/ee; returns the plain export's content hash."""
    patches.apply(patches.PRISTINE, EE / "pristine", None, None)
    patches.apply(patches.PRISTINE, EE / "patched")
    stamp = json.loads((EE / "pristine" / patches.STAMP).read_text(encoding="utf-8"))
    h = hashlib.sha256()
    for name in stamp["files"]:
        h.update(name.encode() + b"\0" + (EE / "pristine" / name).read_bytes() + b"\0")
    return h.hexdigest()[:16]


def run(tree: Path, *cmd: str, check: bool = True) -> subprocess.CompletedProcess:
    proc = subprocess.run(cmd, cwd=tree, capture_output=True, text=True)
    if check and proc.returncode:
        sys.exit(f"error: {' '.join(cmd)} in {tree}:\n{proc.stdout}{proc.stderr}")
    return proc


def build(tree: Path) -> None:
    patches.link_compiler(tree)
    if not (tree / "compiler").is_dir():
        sys.exit("error: no decomp toolchain: python3 decomp/tools/setup_toolchain.py --download "
                 "(run it in the decomp image)")
    run(tree, sys.executable, "tools/gen_ninja.py")
    run(tree, "ninja", "current")
    # check_codegen counts an object missing on both sides as equal: every one must exist.
    phony = next(ln for ln in (tree / "build.ninja").read_text().splitlines() if ln.startswith("build current: phony"))
    missing = [o for o in phony.split()[3:] if not (tree / o).is_file()]
    if missing:
        sys.exit(f"error: {tree}: {len(missing)} current objects were not built, e.g. {missing[0]}")


def gate(key: str) -> int:
    snap = EE / f"pristine-{key}.json"
    if not snap.is_file():
        build(EE / "pristine")
        run(EE / "pristine", sys.executable, "tools/check_codegen.py", "--snapshot", "--file", str(snap))
    build(EE / "patched")
    proc = run(EE / "patched", sys.executable, "tools/check_codegen.py", "--file", str(snap), "--compare", check=False)
    print(proc.stdout.rstrip())
    if proc.returncode:
        print(proc.stderr.rstrip(), file=sys.stderr)
    return proc.returncode


def forward(key: str) -> int:
    """Re-run the build half in the decomp image, the repo at /nw and the toolchain under each export."""
    compiler = (patches.PRISTINE / "compiler").resolve()
    cmd = ["docker", "run", "--rm", "-v", f"{REPO}:/nw"]
    for side in ("pristine", "patched"):
        cmd += ["-v", f"{compiler}:/nw/build/ee/{side}/compiler"]
    cmd += ["-w", "/nw", IMAGE, "python3", "tools/ee_gate.py", "--built", key]
    return subprocess.run(cmd, stdin=subprocess.DEVNULL).returncode


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--lint", action="store_true", help=f"only compare the block counts with {COUNTS.name}")
    parser.add_argument("--built", metavar="KEY", help=argparse.SUPPRESS)  # in the image, exports made by the host
    args = parser.parse_args()
    if args.built:
        return gate(args.built)
    try:
        errors = lint()
        key = None if args.lint else exports()
    except patches.PatchError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    for e in errors:
        print(f"error: {e}", file=sys.stderr)
    if args.lint or errors:
        print(f"{sum(host_blocks().values())} #ifdef NW_HOST blocks in the series" + ("" if errors else ", as listed"))
        return 1 if errors else 0
    return gate(key) if in_container() else forward(key)


if __name__ == "__main__":
    sys.exit(main())
