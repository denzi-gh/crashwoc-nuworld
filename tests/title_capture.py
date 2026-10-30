"""The title cut's frame labels against PCSX2's (local only): world and texanim frame exact, time within 0.05.

The EE rounds every time step toward zero, so PCSX2's time drifts 0.02 below the port's (#91). PCSX2 screens in
<root>/captures/pcsx2 are paired with the port's into <out>/pairs; the pixels are reported, never checked.

    run_local.py python tests/title_capture.py <crashwoc-nuworld> {root} <fixture> <out>
"""
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
import title_frames  # noqa: E402

LABEL = re.compile(r"^frame (-?\d+): world (-?\d+) time (-?[\d.]+) tex (-?\d+)$")
TIME_SLACK = 0.05


def labels(lines: list[str]) -> dict[int, tuple[int, float, int]]:
    out = {}
    for line in lines:
        m = LABEL.match(line.strip())
        if m:
            out[int(m.group(1))] = (int(m.group(2)), float(m.group(3)), int(m.group(4)))
    return out


def compare(port: dict, pcsx2: dict) -> list[str]:
    problems = []
    for vblank, (world, when, tex) in sorted(pcsx2.items()):
        got = port.get(vblank)
        if got is None:
            problems.append(f"vblank {vblank}: no port frame")
        elif got[0] != world or got[2] != tex or abs(got[1] - when) > TIME_SLACK:
            problems.append(f"vblank {vblank}: port world {got[0]} time {got[1]:.2f} tex {got[2]}, "
                            f"PCSX2 world {world} time {when:.2f} tex {tex}")
    return problems


def main() -> int:
    exe, root, fixture, out = sys.argv[1], Path(sys.argv[2]), sys.argv[3], Path(sys.argv[4])
    run = subprocess.run([exe, "--data-dir", str(root), "--capture", fixture, "--capture-dir", str(out / "port")],
                         capture_output=True, text=True)
    if run.returncode != 0:
        sys.exit(f"capture failed ({run.returncode}):\n{run.stderr[-2000:]}")
    want = labels(Path(fixture).read_text(encoding="utf-8").splitlines())
    problems = compare(labels(run.stdout.splitlines()), want)
    print(f"{len(want)} PCSX2 frames, {len(problems)} with other labels")
    for p in problems:
        print(p, file=sys.stderr)
    screens = root / "captures" / "pcsx2"
    if screens.is_dir():
        (out / "pairs").mkdir(parents=True, exist_ok=True)
        found = title_frames.pair(screens, out / "port", out / "pairs")
        for vblank, offset, d in found:
            print(f"vblank {vblank}: port {vblank + offset} ({offset:+d}), difference {d:.1f}")
        print(f"side by side: {out / 'pairs'}")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
