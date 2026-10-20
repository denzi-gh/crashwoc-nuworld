#!/usr/bin/env python3
"""Check typedb.json against the PS2 compiler: compile every named record
with the decomp's `default` profile (SN ee-gcc 2.95.2, which built the game
and engine units) at -O0 -g and compare its STABS layouts with libclang's.
The `sce` profile (EE GCC 2.9, Sony SDK units only) cannot parse the game
headers: its C front end has no anonymous struct/union members.

Runs in the decomp container image (the EE compilers are Linux-only); on a
host it re-runs itself there with docker. Needs the decomp toolchain once:
python decomp/tools/setup_toolchain.py --download (in the container).
"""
from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DECOMP = REPO / "decomp"
PROFILE = "default"
IMAGE = os.environ.get("CRASHWOC_IMAGE", "crashwoc-decomp")
PREFIX = "nw_probe_"
STABS_RE = re.compile(r'^\s*\.stabs\s+"((?:[^"\\]|\\.)*)"')

sys.path.insert(0, str(DECOMP / "tools"))  # cc, declib (the decomp's own tools)


def in_container() -> bool:
    return Path("/.dockerenv").exists() or os.environ.get("NUWORLD_PROBE_DIRECT") == "1"


def forward(argv: list[str]) -> int:
    """Re-run this script in the decomp image with the repo at /nw."""
    for a in argv:
        p = Path(a)
        if p.is_absolute():
            sys.exit(f"error: pass paths relative to the repo when running from a host: {a}")
    cmd = ["docker", "run", "--rm", "-v", f"{REPO}:/nw", "-w", "/nw", IMAGE,
           "python3", "tools/typedb_probe.py", *argv]
    return subprocess.run(cmd, stdin=subprocess.DEVNULL).returncode


def stab_strings(asm: str) -> list[str]:
    """The .stabs strings, continuations (trailing backslash) joined."""
    out, pending = [], ""
    for line in asm.splitlines():
        m = STABS_RE.match(line)
        if not m:
            continue
        s = m.group(1).encode().decode("unicode_escape")
        if s.endswith("\\"):
            pending += s[:-1]
            continue
        out.append(pending + s)
        pending = ""
    return out


def probe_source(db: dict) -> tuple[str, dict[str, str]]:
    """Probe TU text and {variable: record id} for every named record."""
    names = {}
    lines = [f'#include "{h}"' for h in db["headers"]]
    for i, rid in enumerate(r for r in sorted(db["records"]) if "$" not in r):
        var = f"{PREFIX}{i}"
        names[var] = rid
        lines.append(f"{rid} {var};")
    return "\n".join(lines) + "\n", names


def compile_probe(text: str, profile: str, keep: Path | None) -> str:
    import cc
    argv, _driver = cc.compiler_command(cc.profile_spec(profile)["compiler"])
    work = Path(tempfile.mkdtemp(prefix=f"nw_probe_{profile}_"))
    try:
        cc._stage_headers(work)
        (work / "probe.c").write_text(text)
        proc = subprocess.run([*argv, "-O0", "-g", *cc.INCLUDE_ARGS, "-S", "-o", "probe.s",
                               "probe.c"], capture_output=True, text=True, cwd=work)
        if proc.returncode:
            raise RuntimeError(f"{profile}: probe compile failed: {proc.stderr.strip()[:2000]}")
        asm = (work / "probe.s").read_text()
    finally:
        shutil.rmtree(work, ignore_errors=True)
    if keep is not None:
        keep.mkdir(parents=True, exist_ok=True)
        (keep / f"probe_{profile}.s").write_text(asm)
    return asm


class Checker:
    """Compares typedb records with STABS structs; collects mismatches."""

    def __init__(self, db: dict, table, profile: str):
        self.db, self.table, self.profile = db, table, profile
        self.mismatches: list[str] = []
        self.fields = 0

    def struct(self, ref):
        from declib.stabs import Array, Struct
        typ = self.table.resolve(ref)
        while isinstance(typ, Array):
            typ = self.table.resolve(typ.element)
        return typ if isinstance(typ, Struct) else None

    def bad(self, rid: str, field: str, what: str, clang, gcc) -> None:
        where = self.db["records"][rid]["where"]
        name = f"{rid}.{field}" if field else rid
        self.mismatches.append(f"{where}: {name}: {what}: clang {clang}, {self.profile} {gcc}")

    def record(self, rid: str, st, base: int = 0) -> None:
        rec = self.db["records"][rid]
        if rec["ps2"]["size"] != st.size:
            self.bad(rid, "", "size", rec["ps2"]["size"], st.size)
        named = {n: (r, pos, bits) for n, r, pos, bits in st.fields if n}
        unnamed = [(r, pos, bits) for n, r, pos, bits in st.fields if not n]
        anon = iter(u for u in unnamed if self.struct(u[0]) is not None)
        for f in rec["fields"]:
            lay = f["ps2"]
            bit = lay.get("bit", lay["offset"] * 8)
            bits = lay.get("bits", lay["size"] * 8)
            if f["name"]:
                if f["name"] not in named:
                    self.bad(rid, f["name"], "field", "present", "missing")
                    continue
                ref, pos, size = named[f["name"]]
            elif f["kind"] == "record":
                hit = next(anon, None)
                if hit is None:
                    self.bad(rid, "<anonymous>", "field", "present", "missing")
                    continue
                ref, pos, size = hit
            else:
                continue                    # unnamed bitfield padding
            self.fields += 1
            label = f["name"] or "<anonymous>"
            if (bit, bits) != (pos, size):
                self.bad(rid, label, "bit offset/size", (bit, bits), (pos, size))
            sub = self.ref(f)
            if sub and "$" in sub:
                st2 = self.struct(ref)
                if st2 is None:
                    self.bad(rid, label, "type", sub, "not a struct")
                else:
                    self.record(sub, st2)

    @staticmethod
    def ref(f: dict) -> str | None:
        while f.get("kind") == "array":
            f = f["element"]
        return f.get("ref")


def check(db: dict, profile: str, keep: Path | None) -> Checker:
    from declib.stabs import TypeTable
    text, names = probe_source(db)
    table, refs = TypeTable(), {}
    for s in stab_strings(compile_probe(text, profile, keep)):
        name, _desc, ref = table.add(s)
        if name in names:
            refs[name] = ref
    checker = Checker(db, table, profile)
    for var, rid in names.items():
        if var not in refs:
            checker.mismatches.append(f"{rid}: no STABS for the probe variable")
            continue
        st = checker.struct(refs[var])
        if st is None:
            checker.mismatches.append(f"{rid}: STABS type is not a struct/union")
            continue
        checker.record(rid, st)
    return checker


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--typedb", type=Path, default=Path("build/gen/typedb.json"))
    parser.add_argument("--keep", type=Path, help="write the probe assembly to this directory")
    args = parser.parse_args()
    if not in_container():
        return forward(sys.argv[1:])

    if not (DECOMP / "compiler").is_dir():
        print("error: no decomp toolchain: python3 decomp/tools/setup_toolchain.py --download "
              "(run it in the decomp image)", file=sys.stderr)
        return 2
    db = json.loads((REPO / args.typedb).read_text())
    try:
        checker = check(db, PROFILE, args.keep and REPO / args.keep)
    except RuntimeError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    for m in checker.mismatches:
        print(m)
    records = sum(1 for r in db["records"] if "$" not in r)
    print(f"{PROFILE}: {records} records, {checker.fields} fields, "
          f"{len(checker.mismatches)} mismatches")
    return 1 if checker.mismatches else 0


if __name__ == "__main__":
    sys.exit(main())
