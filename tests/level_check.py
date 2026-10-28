"""Levels entered headless (local only; run_local.py passes the data root): each runs on the stepped clock with
instant loads for VBLANKS vblanks after it runs (--level), and passes when it got there, the game thread parked
and every stub it hit is one of SHARED. Near-match hits are listed, not failed.

    run_local.py python tests/level_check.py <crashwoc-nuworld> {root} [--vblanks N] <level> ...
"""
import argparse
import re
import subprocess
import sys

VBLANKS = 1500
STUB = re.compile(r"^\s*\d+ (stub|near) +(\S+) +(\S+) +0x[0-9a-f]+ +\d+$")
RUNNING = re.compile(r"^boot-check: level running at vblank \d+")

# Stubs every level reaches through NuRndrFx and the spot lights; their issues own them.
SHARED = {
    "NuPs2ChangeTexPal": "#61",
    "NuLgtArcLaserDraw": "#61",
    "NuRndrFxCode": "#61",
    "NuLightPolyHaze": "#61",
    "NuRndrWater": "#62",
}


def check(exe: str, root: str, level: str, vblanks: int) -> bool:
    out = subprocess.run([exe, "--data-dir", root, "--stepped", "--hardware-speed", "off", "--boot-check",
                          str(vblanks), "--level", level], capture_output=True, text=True)
    lines = out.stdout.splitlines()
    hits = [m.groups() for line in lines if (m := STUB.match(line))]
    stubs = [name for kind, _unit, name in hits if kind == "stub" and name not in SHARED]
    near = [name for kind, _unit, name in hits if kind == "near"]
    running = any(RUNNING.match(line) for line in lines)
    ok = out.returncode == 0 and running and not stubs
    print(f"{level}: {'ok' if ok else 'FAILED'} (exit {out.returncode}{'' if running else ', never ran'})"
          f"{', stubs ' + ' '.join(stubs) if stubs else ''}{', near ' + ' '.join(near) if near else ''}")
    if not ok:
        print(out.stderr[-2000:], end="")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("root")
    ap.add_argument("levels", nargs="+")
    ap.add_argument("--vblanks", type=int, default=VBLANKS)
    args = ap.parse_args()
    failed = [level for level in args.levels if not check(args.exe, args.root, level, args.vblanks)]
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
