#!/usr/bin/env python3
"""Fail if any tracked file, or any file under the given paths, looks like data from the game disc (CI: every push and
pull request, and the release packages)."""
import os
import subprocess
import sys

DENY_EXTENSIONS = {
    ".iso", ".cue", ".chd", ".cso", ".mdf", ".img",
    ".gsc", ".ghg", ".cut", ".ani", ".anm", ".bsa", ".shd", ".vag",
    ".ter", ".trg", ".ptl", ".cpt", ".vis", ".ppd", ".pad", ".irx",
    ".ai", ".crt", ".lgt", ".wmp", ".gra", ".ats", ".dat",
    # memory card images and save exports: a card's save folder holds the game's icon files
    ".ps2", ".mcd", ".psu", ".max",
}
DENY_NAMES = {"sles_503.86", "slus_202.38", "sceu_000.01", "system.cnf"}

# (offset, magic): 32-bit little-endian ELF (the PS2's; NuWorld's own Linux build is 64-bit), RNC, NU20 scene, VAG,
# memory card image, raw CD sector, ISO9660 volume descriptor.
DENY_MAGIC = [
    (0, b"\x7fELF\x01\x01"),
    (0, b"RNC\x02"),
    (0, b"NU20"),
    (0, b"VAGp"),
    (0, b"Sony PS2 Memory Card Format"),
    (0, b"\x00" + b"\xff" * 10 + b"\x00"),
    (0x8001, b"CD001"),
    (37657, b"CD001"),
]
HEAD_BYTES = 37657 + 5


def tracked_files():
    out = subprocess.run(["git", "ls-files", "-s", "-z"], capture_output=True, check=True).stdout
    for entry in out.split(b"\0"):
        if not entry:
            continue
        meta, path = entry.split(b"\t", 1)
        if meta.startswith(b"160000"):  # submodule gitlink (decomp/)
            continue
        yield path.decode("utf-8", "surrogateescape")


def problems(path):
    name = os.path.basename(path).lower()
    if name in DENY_NAMES:
        yield "game file name"
    if os.path.splitext(name)[1] in DENY_EXTENSIONS:
        yield "game file type"
    try:
        with open(path, "rb") as fh:
            head = fh.read(HEAD_BYTES)
    except OSError:
        return
    for offset, magic in DENY_MAGIC:
        if head[offset:offset + len(magic)] == magic:
            yield f"magic {magic[:8]!r} at {offset:#x}"


def files_under(paths):
    for path in paths:
        if os.path.isdir(path):
            for root, dirs, names in os.walk(path):
                dirs.sort()
                for name in sorted(names):
                    yield os.path.join(root, name)
        else:
            yield path


def check(files):
    """Prints each problem; True when there is none."""
    bad = [(p, why) for p in files for why in problems(p)]
    for path, why in bad:
        print(f"{path}: {why}")
    if bad:
        print(f"{len(bad)} problem(s): game data must never be committed or shipped.")
        return False
    print("no game data found")
    return True


def main(argv):
    return 0 if check(files_under(argv) if argv else tracked_files()) else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
