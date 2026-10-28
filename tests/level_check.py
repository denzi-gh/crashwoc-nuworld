"""Levels entered headless (local only; run_local.py passes the data root): each runs on the stepped clock with
instant loads for VBLANKS vblanks after it runs (--level), and passes when it got there, the game thread parked
and it hit no stub. Near-match hits are listed, not failed. A route ("intro,farm,hub") passes when every leg ran and
a level that runs twice has as many scene GPU objects alive both times. -j runs that many levels at once.

    run_local.py python tests/level_check.py <crashwoc-nuworld> {root} [--vblanks N] [-j N] <level> ...
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import re
import subprocess
import sys

VBLANKS = 1500
STUB = re.compile(r"^\s*\d+ (stub|near) +(\S+) +(\S+) +0x[0-9a-f]+ +\d+$")
RUNNING = re.compile(r"^boot-check: level running at vblank \d+")
ARRIVAL = re.compile(r"^boot-check: level (\d+) ran with (\d+) scene GPU objects alive$")


def check(exe: str, root: str, level: str, vblanks: int) -> tuple[bool, str]:
    out = subprocess.run([exe, "--data-dir", root, "--stepped", "--hardware-speed", "off", "--boot-check",
                          str(vblanks), "--level", level], capture_output=True, text=True)
    lines = out.stdout.splitlines()
    hits = [m.groups() for line in lines if (m := STUB.match(line))]
    stubs = [name for kind, _unit, name in hits if kind == "stub"]
    near = [name for kind, _unit, name in hits if kind == "near"]
    running = any(RUNNING.match(line) for line in lines)
    arrivals = [(int(m[1]), int(m[2])) for line in lines if (m := ARRIVAL.match(line))]
    gpu: dict[int, set[int]] = {}
    for at, n in arrivals:
        gpu.setdefault(at, set()).add(n)
    flat = len(arrivals) == level.count(",") + 1 and all(len(n) == 1 for n in gpu.values())
    ok = out.returncode == 0 and running and not stubs and flat
    counts = " ".join(f"{at}:{n}" for at, n in arrivals)
    report = (f"{level}: {'ok' if ok else 'FAILED'} (exit {out.returncode}{'' if running else ', never ran'})"
              f"{', stubs ' + ' '.join(stubs) if stubs else ''}{', near ' + ' '.join(near) if near else ''}"
              f"{', scene GPU objects ' + counts if ',' in level or not flat else ''}\n")
    return ok, report if ok else report + out.stderr[-2000:]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("exe")
    ap.add_argument("root")
    ap.add_argument("levels", nargs="+")
    ap.add_argument("--vblanks", type=int, default=VBLANKS)
    ap.add_argument("-j", type=int, default=1)
    args = ap.parse_args()
    failed = 0
    with ThreadPoolExecutor(args.j) as pool:
        for ok, report in pool.map(lambda level: check(args.exe, args.root, level, args.vblanks), args.levels):
            print(report, end="", flush=True)
            failed += not ok
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
