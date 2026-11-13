#!/usr/bin/env python3
"""NuWorld's release packages: the executable with its licences, a readme and, on Linux, a desktop entry.

  package.py version [--full]          CMakeLists.txt's version, --full with NUWORLD_TAG ("0.8.0 BETA")
  package.py stage BUILD PLATFORM OUT  OUT/crashwoc-nuworld-PLATFORM/ from a release build dir
  package.py archive NAME OUT DIR...   OUT/NAME-PLATFORM.zip (Windows) or .tar.gz per staged DIR, and OUT/SHA256SUMS

PLATFORM is windows-x64 or linux-x86_64. Every package is checked with tools/deny_gamedata.py.
"""
import hashlib
import re
import shutil
import sys
import tarfile
import zipfile
from pathlib import Path

import deny_gamedata

REPO = Path(__file__).resolve().parent.parent
PREFIX = "crashwoc-nuworld-"
PLATFORMS = {
    "windows-x64": {"exe": "NuWorld.exe", "newline": "\r\n", "extra": {}},
    "linux-x86_64": {
        "exe": "NuWorld",
        "newline": "\n",
        "extra": {
            "nuworld.desktop": "assets/dist/nuworld.desktop",
            "nuworld.png": "assets/branding/nuworld-icon-256.png",
        },
    },
}

# (name, FetchContent name or None, licence, home page, licence file: under BUILD/_deps/<name>-src/ or the repo)
NOTICES = [
    ("SDL", "SDL3", "zlib License", "https://libsdl.org", "LICENSE.txt"),
    ("Dear ImGui", "imgui", "MIT License", "https://github.com/ocornut/imgui", "LICENSE.txt"),
    ("Atkinson Hyperlegible", None, "SIL Open Font License 1.1", "https://www.brailleinstitute.org/freefont",
     "assets/fonts/OFL.txt"),
    ("Kenney Input Prompts", None, "CC0 1.0", "https://kenney.nl", "assets/prompts/License.txt"),
]
NOTICES_HEAD = """\
CrashWOC: NuWorld is licensed under the GNU General Public License v3.0 (LICENSE.txt).
Parts of its SPU2 emulation (the interpolation table, the reverb filter) follow PCSX2
(https://pcsx2.net, GPL-3.0-or-later). The Windows build links the LLVM C++ runtime
(Apache-2.0 WITH LLVM-exception).

It includes the following third-party software and assets.
"""


def cmake_text():
    return (REPO / "CMakeLists.txt").read_text(encoding="utf-8")


def version(full=False):
    text = cmake_text()
    number = re.search(r"^project\(crashwoc-nuworld VERSION ([0-9.]+)", text, re.M).group(1)
    tag = re.search(r'^set\(NUWORLD_TAG "([^"]*)"', text, re.M).group(1)
    return f"{number} {tag}".strip() if full else number


def fetch_tags():
    return dict(re.findall(r"FetchContent_Declare\((\w+)\s+GIT_REPOSITORY\s+\S+\s+GIT_TAG\s+(\S+)", cmake_text()))


def notices(build):
    tags = fetch_tags()
    parts = [NOTICES_HEAD]
    for name, fetch, licence, url, file in NOTICES:
        path = build / "_deps" / f"{fetch.lower()}-src" / file if fetch else REPO / file
        title = f"{name} {tags[fetch]}" if fetch else name
        parts.append(f"\n{'=' * 78}\n{title} ({licence})\n{url}\n{'=' * 78}\n\n")
        parts.append(path.read_text(encoding="utf-8").strip() + "\n")
    return "".join(parts)


def write_text(path, text, newline):
    path.write_bytes(text.replace("\r\n", "\n").replace("\n", newline).encode("utf-8"))


def stage(build, platform, out):
    spec = PLATFORMS[platform]
    build = Path(build)
    dest = Path(out) / (PREFIX + platform)
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    shutil.copy2(build / spec["exe"], dest / spec["exe"])
    newline = spec["newline"]
    write_text(dest / "LICENSE.txt", (REPO / "LICENSE").read_text(encoding="utf-8"), newline)
    readme = (REPO / "assets/dist/README.txt").read_text(encoding="utf-8")
    write_text(dest / "README.txt", readme.replace("{version}", version(full=True)), newline)
    write_text(dest / "THIRD_PARTY_NOTICES.txt", notices(build), newline)
    for name, src in spec["extra"].items():
        if name.endswith(".png"):
            shutil.copy2(REPO / src, dest / name)
        else:
            write_text(dest / name, (REPO / src).read_text(encoding="utf-8"), newline)
    return deny_gamedata.check(deny_gamedata.files_under([str(dest)]))


def archive(name, out, staged):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    made = []
    for src in map(Path, staged):
        platform = src.name.removeprefix(PREFIX)
        exe = PLATFORMS[platform]["exe"]
        if not deny_gamedata.check(deny_gamedata.files_under([str(src)])):
            return False
        root = f"{name}-{platform}"
        files = sorted(p for p in src.rglob("*") if p.is_file())
        if platform.startswith("windows"):
            path = out / f"{root}.zip"
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
                for f in files:
                    z.write(f, f"{root}/{f.relative_to(src).as_posix()}")
        else:
            path = out / f"{root}.tar.gz"
            with tarfile.open(path, "w:gz") as t:
                for f in files:
                    info = t.gettarinfo(f, f"{root}/{f.relative_to(src).as_posix()}")
                    info.mode = 0o755 if f.name == exe else 0o644  # artifacts drop the executable bit
                    info.uid = info.gid = 0
                    info.uname = info.gname = ""
                    with open(f, "rb") as fh:
                        t.addfile(info, fh)
        made.append(path)
    sums = "".join(f"{hashlib.sha256(p.read_bytes()).hexdigest()}  {p.name}\n" for p in sorted(made))
    (out / "SHA256SUMS").write_bytes(sums.encode())
    for path in sorted(made):
        print(f"{path} ({path.stat().st_size >> 10} KB)")
    return True


def main(argv):
    if argv[:1] == ["version"] and argv[1:] in ([], ["--full"]):
        # LF on Windows too: $(...) strips the \n, not a \r
        sys.stdout.buffer.write((version(full=argv[1:] == ["--full"]) + "\n").encode())
        return 0
    if argv[:1] == ["stage"] and len(argv) == 4 and argv[2] in PLATFORMS:
        return 0 if stage(*argv[1:]) else 1
    if argv[:1] == ["archive"] and len(argv) >= 4:
        return 0 if archive(argv[1], argv[2], argv[3:]) else 1
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
