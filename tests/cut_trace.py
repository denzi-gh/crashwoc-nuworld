"""The title cut's sound trace from a stepped boot (local only), up to UPTO vblanks of the cut.

  default    a second run traces the same
  --pcsx2 T  PCSX2's trace exactly up to logos2 and the logos stream's end; music key-ons within a vblank, either
             order (stream slot order follows the module's ticks since boot; boot disc reads take no time here)
  --instant  instant loading; against --pcsx2 counted from the logos key-on (the texanim is not compared), music at
             most a vblank later, and the stub hits of a hardware-speed run

    run_local.py python tests/cut_trace.py <crashwoc-nuworld> {root} [--pcsx2 tests/fixtures/pcsx2_title_cut.txt]
        [--instant]
"""
import re
import subprocess
import sys

VBLANKS = 2600  # after the first swap: the boot, the loads and the cut to UPTO
UPTO = 1900  # vblanks of the cut: past the end of the logos stream
LINE = re.compile(r"^vblank (\d+): key (\d{6}) movie \d+ world (\d+) ")
STUB = re.compile(r"^\s*\d+ (stub|near) +(\S+) +(\S+) +(0x[0-9a-f]+) +\d+$")


def cut(lines: list[str]) -> list[str]:
    return [line for line in lines if (m := LINE.match(line)) and int(m.group(1)) <= UPTO]


def run(exe: str, root: str, instant: bool) -> list[str]:
    speed = ["--hardware-speed", "off"] if instant else []
    out = subprocess.run([exe, "--data-dir", root, "--stepped", "--boot-check", str(VBLANKS), "--trace-sound", *speed],
                         capture_output=True, text=True)
    if out.returncode != 0:
        sys.exit(f"boot-check failed ({out.returncode}):\n{out.stderr[-2000:]}")
    return out.stdout.splitlines()


def stubs(lines: list[str]) -> list[tuple]:
    """The stub and near-match hits in first-hit order, without their counts."""
    return [m.groups() for line in lines if (m := STUB.match(line))]


def events(lines: list[str]) -> dict:
    """Vblanks of the switch to logos2, logos stream key-on and end (channel 4) and music key-ons (channels 0, 1)."""
    found = {"world": None, "keyon": None, "logos_end": None, "music": {}}
    keyed = False
    for line in lines:
        m = LINE.match(line)
        vblank, key, world = int(m.group(1)), m.group(2), int(m.group(3))
        if world == 1 and found["world"] is None:
            found["world"] = vblank
        if key[4] == "1" and found["keyon"] is None:
            found["keyon"] = vblank
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


def compare_instant(port: list[str], pcsx2: list[str]) -> list[str]:
    want, got = events(pcsx2), events(port)
    if None in (want["world"], want["keyon"], got["keyon"]):
        return ["no switch to logos2 or no key-on of the logos stream"]
    problems = []
    for name in ("world", "logos_end"):
        after = None if got[name] is None else got[name] - got["keyon"]
        if after != want[name] - want["keyon"]:
            problems.append(f"{name} {after} vblanks after the key-on, PCSX2 {want[name] - want['keyon']}")
    music = sorted(v - got["keyon"] for v in got["music"].values())
    ref = sorted(v - want["keyon"] for v in want["music"].values())
    if len(music) != 2 or len(ref) != 2 or any(a > b + 1 for a, b in zip(music, ref)):
        problems.append(f"music keyed on {music} vblanks after the key-on, PCSX2 {ref}")
    return problems


def main() -> int:
    exe, root, rest = sys.argv[1], sys.argv[2], sys.argv[3:]
    instant = "--instant" in rest
    lines = run(exe, root, instant)
    first = cut(lines)
    print("\n".join(first))
    if not first:
        print("no cut traced", file=sys.stderr)
        return 1
    if "--pcsx2" in rest:
        with open(rest[rest.index("--pcsx2") + 1], encoding="utf-8") as f:
            pcsx2 = cut(f.read().splitlines())
        problems = compare_instant(first, pcsx2) if instant else compare(first, pcsx2)
        if instant and stubs(lines) != stubs(run(exe, root, False)):
            problems.append("the stub hits differ from a run at the hardware speed")
        for p in problems:
            print(p, file=sys.stderr)
        return 1 if problems else 0
    if first != cut(run(exe, root, instant)):
        print("the two runs trace the cut differently", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
