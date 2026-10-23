"""The title cut's sound trace from a stepped boot (local only; run_local.py passes the data root): runs the port
twice with --stepped --boot-check --trace-sound and checks that both runs trace the cut the same, up to UPTO vblanks
after its start.

    run_local.py python tests/cut_trace.py <crashwoc-nuworld> {root}
"""
import re
import subprocess
import sys

VBLANKS = 2600  # after the first swap: the boot, the loads and the cut to UPTO
UPTO = 1900  # vblanks of the cut: past the end of the logos stream
LINE = re.compile(r"^vblank (\d+): ")


def trace(exe: str, root: str) -> list[str]:
    out = subprocess.run([exe, "--data-dir", root, "--stepped", "--boot-check", str(VBLANKS), "--trace-sound"],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"boot-check failed ({out.returncode}):\n{out.stderr[-2000:]}")
    return [line for line in out.stdout.splitlines() if (m := LINE.match(line)) and int(m.group(1)) <= UPTO]


def main() -> int:
    exe, root = sys.argv[1:3]
    first, second = trace(exe, root), trace(exe, root)
    print("\n".join(first))
    if not first or not LINE.match(first[-1]) or first != second:
        print("the two runs trace the cut differently" if first != second else "no cut traced", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
