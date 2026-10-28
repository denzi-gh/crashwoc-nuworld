#!/usr/bin/env python3
"""Every disc file decoded twice: crashwoc-nuworld --check-disc against the crashwoc tooling and WrathSDK.

  python tools/disc_check.py --data-dir <root> --compare <crashwoc-nuworld> [--tooling <dir>] [--wrathsdk <dir>]

Bytes: each disc file and each entry of ATS/CHARS/SFX.DAT, RNC-unpacked (rnc.py, unpack_dat.py), as the FILE/DAT
lines of --check-disc. Structure: the .gsc lines of --check-scenes (tools/gsc_hash.py), and what the Python readers
make of the rest: .ai/.crt/.lgt/.obj/.wmp written back byte for byte (WrathSDK formats), .ptl (nus_ptl.py), .ghg
(WrathSDK nus_ghg.py). The table at the end lists per extension what decoded it. The tooling comes from --tooling or
$CRASHWOC_TOOLING, WrathSDK from --wrathsdk or $WRATHSDK; without them the exit code is 77 (skipped).
"""
from __future__ import annotations

import argparse
import collections
import difflib
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gsc_hash  # noqa: E402

SKIP = 77
ROUND_TRIP = ("ai", "crt", "lgt", "obj", "wmp")
# The retail ctests whose C loaders read every file of an extension (cmake/decomp.cmake); the level files the game
# loads itself are covered by levels-retail.
C_TESTS = {
    "gsc": "check-scenes-retail nugscn-retail", "ghg": "nughg-retail", "cut": "nucut-retail",
    "ani": "nuanim-retail", "bsa": "nuanim-retail", "ter": "terrain-retail", "shd": "shadcom-retail",
    "vag": "audio-retail", "obj": "edunits-retail", "anm": "edunits-retail", "gra": "edunits-retail",
    "ptl": "edunits-retail", "cpt": "edunits-retail", "ats": "texanim-retail (title)",
    "ai": "levels-retail", "crt": "levels-retail", "lgt": "levels-retail", "wmp": "levels-retail",
    "vis": "levels-retail", "ppd": "levels-retail (--pad-script)",
}


def load(tooling: str | None, wrathsdk: str | None) -> dict | None:
    tooling = tooling or os.environ.get("CRASHWOC_TOOLING")
    wrathsdk = wrathsdk or os.environ.get("WRATHSDK")
    if not tooling or not wrathsdk or not (Path(tooling) / "unpack_dat.py").is_file() or \
            not (Path(wrathsdk) / "wrathsdk" / "formats").is_dir():
        return None
    mods = gsc_hash.load_tooling(tooling)
    sys.path.insert(0, str(Path(wrathsdk).resolve()))
    import nus_ptl  # noqa: E402
    import unpack_dat  # noqa: E402
    from wrathsdk.assets import nus_ghg  # noqa: E402
    from wrathsdk.formats import ai, crt, lgt, obj, wmp  # noqa: E402
    mods.update(unpack_dat=unpack_dat, nus_ptl=nus_ptl, nus_ghg=nus_ghg,
                formats={"ai": ai, "crt": crt, "lgt": lgt, "obj": obj, "wmp": wmp})
    return mods


def disc_paths() -> list[str]:
    """The disc manifest's files in its order, NW_MANIFEST_SKIP ones left out."""
    return [m.group(1) for m in re.finditer(r'\{ "([^"]+)", (\d+)u, "[0-9a-f]{64}", ([^ ]+) \}',
                                             gsc_hash.MANIFEST.read_text()) if m.group(3) == "0"]


def line(kind: str, name: str, data: bytes) -> str:
    return f"{kind} {name} size={len(data)} sha256={hashlib.sha256(data).hexdigest()}"


def structure(ext: str, data: bytes, mods: dict) -> str | None:
    """What the Python readers make of a decoded file: None when none reads it, else "ok" or the failure."""
    try:
        if ext in ROUND_TRIP:
            fmt = mods["formats"][ext]
            return "ok" if fmt.write(fmt.parse(data)) == data else "written back differently"
        if ext == "ptl":
            mods["nus_ptl"].load_ptl(data)
            return "ok"
        if ext == "ghg":
            mods["nus_ghg"].decode_geoms(data)
            return "ok"
    except Exception as e:  # noqa: BLE001 (any reader failure is a finding)
        return f"{type(e).__name__}: {e}"
    return None


def ext_of(path: str) -> str:
    name = path.replace("\\", "/").rsplit("/", 1)[-1]
    return name.rsplit(".", 1)[-1].lower() if "." in name else ""


def decode(root: Path, mods: dict) -> tuple[list[str], dict, list[str]]:
    """The FILE/DAT lines, per extension [count, structure ok], and the structure failures."""
    rnc, unpack_dat = mods["rnc"], mods["unpack_dat"]
    lines, failures = [], []
    table: dict[str, list[int]] = collections.defaultdict(lambda: [0, 0])
    files = entries = 0

    def add(kind: str, name: str, data: bytes) -> None:
        lines.append(line(kind, name, data))
        ext = ext_of(name)
        table[ext][0] += 1
        result = structure(ext, data, mods)
        if result == "ok":
            table[ext][1] += 1
        elif result is not None:
            failures.append(f"{name}: {result}")

    for path in disc_paths():
        raw = (root / "disc" / path).read_bytes()
        add("FILE", path, rnc.unpack(raw) if raw[:4] == b"RNC\x02" else raw)
        files += 1
        if path.upper().endswith(".DAT"):
            for e in unpack_dat.parse_dat(raw):
                add("DAT", f"{path}:{e.path}", e.data(raw))
                entries += 1
    lines.append(f"TOTAL files={files} entries={entries} failed=0")
    return lines, table, failures


def compare(name: str, ours: list[str], theirs: list[str]) -> bool:
    diff = list(difflib.unified_diff(ours, theirs, "python", "crashwoc-nuworld", lineterm="", n=0))
    if diff:
        print("\n".join(diff[:40]))
        print(f"disc_check: {name}: {sum(d[:1] in '+-' for d in diff[2:])} lines differ", file=sys.stderr)
    return not diff


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--data-dir", type=Path, required=True, help="the data root (with disc/)")
    parser.add_argument("--compare", required=True, help="crashwoc-nuworld")
    parser.add_argument("--tooling", help="the crashwoc tooling folder (default: $CRASHWOC_TOOLING)")
    parser.add_argument("--wrathsdk", help="the WrathSDK checkout (default: $WRATHSDK)")
    args = parser.parse_args()

    mods = load(args.tooling, args.wrathsdk)
    if mods is None:
        print("disc_check: no crashwoc tooling or WrathSDK (--tooling/CRASHWOC_TOOLING, --wrathsdk/WRATHSDK): skipped")
        return SKIP
    lines, table, failures = decode(args.data_dir, mods)
    run = subprocess.run([args.compare, "--data-dir", str(args.data_dir), "--check-disc"], capture_output=True,
                         text=True)
    ok = run.returncode == 0 and compare("bytes", lines, run.stdout.splitlines())

    scenes = gsc_hash.all_lines(args.data_dir, mods)
    run = subprocess.run([args.compare, "--data-dir", str(args.data_dir), "--check-scenes"], capture_output=True,
                         text=True)
    theirs = [s for s in run.stdout.splitlines() if s.split(" ", 1)[0] in ("OBJ", "GSC", "TOTAL")
              or (s.startswith("TEX ") and gsc_hash.compared(s))]
    scenes_ok = compare("scenes", scenes, theirs)
    table["gsc"][1] = len(gsc_hash.scene_paths()) if scenes_ok else 0
    ok = ok and scenes_ok

    for f in failures:
        print(f"disc_check: {f}", file=sys.stderr)
    print(f"{'ext':<6} {'files':>6} {'struct':>7}  C loaders")
    for ext in sorted(table):
        count, parsed = table[ext]
        print(f"{ext or '-':<6} {count:>6} {parsed or '-':>7}  {C_TESTS.get(ext, '-')}")
    print(f"disc_check: {lines[-1]}, bytes and scenes {'match' if ok else 'DIFFER'}, "
          f"{len(failures)} structure failures")
    return 0 if ok and not failures else 1


if __name__ == "__main__":
    sys.exit(main())
