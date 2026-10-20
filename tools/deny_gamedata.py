#!/usr/bin/env python3
"""Fail if any tracked file looks like data from the game disc.

CrashWOC: NuWorld never ships game content; everything comes from the user's own disc
at runtime. This check runs in CI on every push and pull request.
"""
import os
import subprocess
import sys

# Disc file types and disc images.
DENY_EXTENSIONS = {
    ".iso", ".cue", ".chd", ".cso", ".mdf", ".img",
    ".gsc", ".ghg", ".cut", ".ani", ".anm", ".bsa", ".shd", ".vag",
    ".ter", ".trg", ".ptl", ".cpt", ".vis", ".ppd", ".pad", ".irx",
    ".ai", ".crt", ".lgt", ".wmp", ".gra", ".ats", ".dat",
}
DENY_NAMES = {"sles_503.86", "slus_202.38", "sceu_000.01", "system.cnf"}

# (offset, magic): ELF (game executable, IRX modules), RNC-packed files,
# NU20 scenes, PS VAG audio, raw CD sectors, ISO9660 volume descriptors.
DENY_MAGIC = [
    (0, b"\x7fELF"),
    (0, b"RNC\x02"),
    (0, b"NU20"),
    (0, b"VAGp"),
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


def main():
    bad = [(p, why) for p in tracked_files() for why in problems(p)]
    for path, why in bad:
        print(f"{path}: {why}")
    if bad:
        print(f"{len(bad)} problem(s): game data must never be committed.")
        return 1
    print("no game data found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
