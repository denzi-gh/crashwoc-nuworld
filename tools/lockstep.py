#!/usr/bin/env python3
"""Run the game on PCSX2 and in the headless port over the same vblanks and pokes, and diff their states.

1. PCSX2, booted and on its way to the title (PINE): tools/pcsx2_dump.py saves its EE RAM at --vblanks and makes
   the --poke writes (they stand in for pad input), into <out>/pcsx2.
2. The port: crashwoc-nuworld --dump at the vblanks PCSX2's states hold, --poke as the writes landed on PCSX2, into
   <out>/port.
3. tools/ps2_diff.py per vblank: one summary line each, the blocks that differ first at that vblank, and the full
   report of the first vblank that differs to <out>/<vblank>.txt.

  python tools/lockstep.py --exe build/release/crashwoc-nuworld.exe --out <dir> --vblanks 100,500
      [--poke 300:new_level=0x25,...] [--after GameMode=1] [--slot 28011 --sstates <dir> --slots 4-10]
      [--skip-pcsx2]

Run it with Python 3.14 (pcsx2_dump.py reads the states' zstd entries). --skip-pcsx2 reuses <out>/pcsx2.
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

import ps2_diff

HERE = Path(__file__).resolve().parent


def run_pcsx2(args, out: Path) -> tuple[list[int], list[str]]:
    cmd = [sys.executable, str(HERE / "pcsx2_dump.py"), "--out", str(out), "--vblanks", args.vblanks,
           "--slot", str(args.slot)]
    for flag, value in (("--poke", args.poke), ("--after", args.after), ("--sstates", args.sstates),
                        ("--slots", args.slots)):
        if value:
            cmd += [flag, str(value)]
    text = subprocess.run(cmd, check=True, capture_output=True, text=True).stdout
    (out / "pcsx2.txt").write_text(text, encoding="utf-8")
    return read_pcsx2(text)


def read_pcsx2(text: str) -> tuple[list[int], list[str]]:
    vblanks = [int(m.group(1)) for m in re.finditer(r"^vblank (-?\d+)$", text, re.M)]
    pokes = [m.group(1) for m in re.finditer(r"^poke (\S+)$", text, re.M)]
    return vblanks, pokes


def run_port(args, vblanks: list[int], pokes: list[str], out: Path) -> None:
    cmd = [str(args.exe.resolve()), "--dump", ",".join(map(str, vblanks)), "--dump-dir", str(out)]
    if pokes:
        cmd += ["--poke", ",".join(pokes)]
    if args.after:
        cmd += ["--dump-after", args.after]
    if args.data_dir:
        cmd += ["--data-dir", str(args.data_dir)]
    log = subprocess.run(cmd, capture_output=True, text=True)
    (out / "port.log").write_text(log.stdout + log.stderr, encoding="utf-8")
    if log.returncode:
        raise SystemExit(f"crashwoc-nuworld --dump failed ({log.returncode}): see {out / 'port.log'}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--exe", type=Path, required=True, help="crashwoc-nuworld")
    parser.add_argument("--out", type=Path, required=True, help="folder for both sides' states and the reports")
    parser.add_argument("--vblanks", default="", help="comma-separated trace vblanks (not needed with --skip-pcsx2)")
    parser.add_argument("--poke", default="", help="comma-separated vblank:symbol[+offset]=value writes")
    parser.add_argument("--after", help="symbol=value: the vblanks count from the first vblank it holds on")
    parser.add_argument("--data-dir", type=Path, help="the port's data folder")
    parser.add_argument("--slot", type=int, default=28011, help="PINE slot (default 28011)")
    parser.add_argument("--sstates", type=Path, help="PCSX2's state folder (pcsx2_dump.py's default otherwise)")
    parser.add_argument("--slots", help="state slots to use in turn (pcsx2_dump.py's default otherwise)")
    parser.add_argument("--skip-pcsx2", action="store_true", help="reuse <out>/pcsx2")
    parser.add_argument("--typedb", type=Path, default=ps2_diff.TYPEDB)
    parser.add_argument("--known", type=Path, default=ps2_diff.KNOWN)
    args = parser.parse_args()

    pcsx2_dir, port_dir = args.out / "pcsx2", args.out / "port"
    pcsx2_dir.mkdir(parents=True, exist_ok=True)
    port_dir.mkdir(parents=True, exist_ok=True)
    if args.skip_pcsx2:
        vblanks, pokes = read_pcsx2((pcsx2_dir / "pcsx2.txt").read_text(encoding="utf-8"))
    else:
        if not args.vblanks:
            raise SystemExit("--vblanks is needed unless --skip-pcsx2")
        vblanks, pokes = run_pcsx2(args, pcsx2_dir)
    print(f"PCSX2: states at vblanks {vblanks}; pokes landed {pokes}", flush=True)
    run_port(args, vblanks, pokes, port_dir)

    fields = ps2_diff.load_typedb(args.typedb)
    known = ps2_diff.Known(args.known)
    seen: set[str] = set()
    first = None
    for vblank in vblanks:
        dump = ps2_diff.read_dump((port_dir / f"{vblank}.nwd").read_bytes())
        res = ps2_diff.compare(dump, (pcsx2_dir / f"{vblank}.ee").read_bytes(), fields, known, limit=1 << 30)
        print(res.summary())
        new = [label for label in res.differing if label not in seen]
        seen.update(res.differing)
        for label in new[:20]:
            print(f"  new: {label}")
        if len(new) > 20:
            print(f"  ... and {len(new) - 20} more new")
        if res.differ_blocks and first is None:
            first = vblank
            (args.out / f"{vblank}.txt").write_text("\n".join([res.summary(), *res.lines]) + "\n", encoding="utf-8")
    if first is not None:
        print(f"first difference at vblank {first}: {args.out / f'{first}.txt'}")
    return 1 if first is not None else 0


if __name__ == "__main__":
    sys.exit(main())
