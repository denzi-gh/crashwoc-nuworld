"""The title cut's sound trace from a stepped boot (local only; run_local.py passes the data root). Runs the port
with --stepped --boot-check --trace-sound and checks, up to UPTO vblanks of the cut,

  - by default, that a second run traces it the same;
  - with --pcsx2 <trace>, that it matches tools/pcsx2_trace.py's trace of PCSX2: every line up to the switch to
    logos2 and the end of the logos stream exactly, the two music channels' key-ons each within a vblank, in
    either order (which stream slot the module starts first follows its ticks since boot, and the boot's own disc
    reads take no time here).

    run_local.py python tests/cut_trace.py <crashwoc-nuworld> {root} [--pcsx2 tests/fixtures/pcsx2_title_cut.txt]
"""
import re
import subprocess
import sys

VBLANKS = 2600  # after the first swap: the boot, the loads and the cut to UPTO
UPTO = 1900  # vblanks of the cut: past the end of the logos stream
LINE = re.compile(r"^vblank (\d+): key (\d{6}) movie \d+ world (\d+) ")


def cut(lines: list[str]) -> list[str]:
    return [line for line in lines if (m := LINE.match(line)) and int(m.group(1)) <= UPTO]


def trace(exe: str, root: str) -> list[str]:
    out = subprocess.run([exe, "--data-dir", root, "--stepped", "--boot-check", str(VBLANKS), "--trace-sound"],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"boot-check failed ({out.returncode}):\n{out.stderr[-2000:]}")
    return cut(out.stdout.splitlines())


def events(lines: list[str]) -> dict:
    """The switch to logos2, the logos stream's end (channel 4: 1 -> 0), the music channels' key-ons (0, 1)."""
    found = {"world": None, "logos_end": None, "music": {}}
    keyed = False
    for line in lines:
        m = LINE.match(line)
        vblank, key, world = int(m.group(1)), m.group(2), int(m.group(3))
        if world == 1 and found["world"] is None:
            found["world"] = vblank
        keyed |= key[4] == "1"
        if keyed and key[4] == "0" and found["logos_end"] is None:
            found["logos_end"] = vblank
        for ch in (0, 1):
            if key[ch] == "1":
                found["music"].setdefault(ch, vblank)
    return found


def compare(port: list[str], pcsx2: list[str]) -> list[str]:
    want, got = events(pcsx2), events(port)
    problems = []
    if want["world"] is None:
        return ["the PCSX2 trace has no switch to logos2"]
    head = [line for line in pcsx2 if int(LINE.match(line).group(1)) <= want["world"]]
    if port[:len(head)] != head:
        problems.append("the cut up to logos2 differs")
    if got["logos_end"] != want["logos_end"]:
        problems.append(f"logos stream ends at {got['logos_end']}, PCSX2 {want['logos_end']}")
    music, ref = sorted(got["music"].values()), sorted(want["music"].values())
    if len(music) != 2 or len(ref) != 2 or any(abs(a - b) > 1 for a, b in zip(music, ref)):
        problems.append(f"music keyed on at {music}, PCSX2 {ref}")
    return problems


def main() -> int:
    exe, root = sys.argv[1:3]
    first = trace(exe, root)
    print("\n".join(first))
    if not first:
        print("no cut traced", file=sys.stderr)
        return 1
    if len(sys.argv) == 5 and sys.argv[3] == "--pcsx2":
        with open(sys.argv[4], encoding="utf-8") as f:
            problems = compare(first, cut(f.read().splitlines()))
        for p in problems:
            print(p, file=sys.stderr)
        return 1 if problems else 0
    if first != trace(exe, root):
        print("the two runs trace the cut differently", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
