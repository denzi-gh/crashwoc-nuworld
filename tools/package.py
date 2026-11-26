#!/usr/bin/env python3
"""NuWorld's release packages: the executable with its licences, a readme and, on Linux, a desktop entry.

  package.py version [--full]          CMakeLists.txt's version, --full with NUWORLD_TAG ("0.8.0 BETA")
  package.py stage BUILD PLATFORM OUT  OUT/crashwoc-nuworld-PLATFORM/ from a release build dir; on Windows with
                                       the libraries it loads, and OUT/crashwoc-nuworld-PLATFORM-symbols/
  package.py archive NAME OUT DIR...   OUT/NAME-PLATFORM.zip (Windows) or .tar.gz per staged DIR, and OUT/SHA256SUMS

PLATFORM is windows-x64 or linux-x86_64. Every package is checked with tools/deny_gamedata.py.
"""
import hashlib
import os
import re
import shutil
import struct
import sys
import tarfile
import zipfile
from pathlib import Path

import deny_gamedata

REPO = Path(__file__).resolve().parent.parent
PREFIX = "crashwoc-nuworld-"
ROOT = "CrashWOC-NuWorld"
SHADERS = ("vu.vert.glsl", "vu.frag.glsl", "gem.frag.glsl", "sky.frag.glsl", "clouds.frag.glsl", "earth.frag.glsl",
           "fx.frag.glsl")
SYMBOLS = "-symbols"
RUNTIME = "x64/Microsoft.VC*.CRT"
WINDOWS = frozenset(("advapi32", "gdi32", "imm32", "kernel32", "ole32", "oleaut32", "powrprof", "setupapi",
                     "shell32", "user32", "version", "winhttp", "winmm"))
CRASHPAD = [
    ("Crashpad {tag}", "crashpad", "Apache License 2.0", "https://github.com/getsentry/crashpad", "LICENSE"),
    ("mini_chromium", "crashpad", "BSD 3-Clause License", "https://chromium.googlesource.com/chromium/mini_chromium",
     "third_party/mini_chromium/mini_chromium/LICENSE"),
    ("zlib", "crashpad", "zlib License", "https://zlib.net", "third_party/zlib/zlib/LICENSE"),
    ("MPack", "crashpad", "MIT License", "https://github.com/ludocode/mpack", "third_party/mpack/LICENSE"),
    ("getopt", "crashpad", "public domain", "https://sourceware.org/ml/newlib/2005/msg00758.html",
     "third_party/getopt/LICENSE"),
]
PLATFORMS = {
    "windows-x64": {
        "programs": ["NuWorld.exe", "NuWorldUpdater.exe", "crashpad_handler.exe"],
        "files": ["NuWorld.sym"],
        "symbols": ["NuWorld.pdb", "NuWorld.map"],
        "notices": CRASHPAD,
        "newline": "\r\n",
        "extra": {},
    },
    "linux-x86_64": {
        "programs": ["NuWorld", "NuWorldUpdater"],
        "files": [],
        "symbols": [],
        "notices": [],
        "newline": "\n",
        "extra": {
            "nuworld.desktop": "assets/dist/nuworld.desktop",
            "nuworld.png": "assets/branding/nuworld-icon-256.png",
        },
    },
}

# (name, FetchContent name or None, licence, home page, licence file: under BUILD/_deps/<name>-src/ or the repo)
NOTICES = [
    ("SDL {tag}", "SDL3", "zlib License", "https://libsdl.org", "LICENSE.txt"),
    ("Dear ImGui {tag}", "imgui", "MIT License", "https://github.com/ocornut/imgui", "LICENSE.txt"),
    ("miniz {tag}", "miniz", "MIT License", "https://github.com/richgel999/miniz", "LICENSE"),
    ("Monocypher {tag}", "monocypher", "BSD 2-Clause License or CC0 1.0", "https://monocypher.org", "LICENCE.md"),
    ("Atkinson Hyperlegible", None, "SIL Open Font License 1.1", "https://www.brailleinstitute.org/freefont",
     "assets/fonts/OFL.txt"),
    ("Kenney Input Prompts", None, "CC0 1.0", "https://kenney.nl", "assets/prompts/License.txt"),
]
NOTICES_HEAD = """\
CrashWOC: NuWorld is licensed under the GNU General Public License v3.0 (LICENSE.txt).
Parts of its SPU2 emulation (the interpolation table, the reverb filter) follow PCSX2
(https://pcsx2.net, GPL-3.0-or-later). The Windows package carries Microsoft's Visual C++
runtime libraries as Visual Studio's licence lets applications redistribute them.

It includes the following third-party software and assets.
"""


def cmake_text():
    return "".join((REPO / name).read_text(encoding="utf-8") for name in ("CMakeLists.txt", "cmake/crashpad.cmake"))


def version(full=False):
    text = cmake_text()
    number = re.search(r"^project\(crashwoc-nuworld VERSION ([0-9.]+)", text, re.M).group(1)
    tag = re.search(r'^set\(NUWORLD_TAG "([^"]*)"', text, re.M).group(1)
    return f"{number} {tag}".strip() if full else number


def fetch_tags():
    return dict(re.findall(r"FetchContent_Declare\((\w+)\s+GIT_REPOSITORY\s+\S+\s+GIT_TAG\s+(\S+)", cmake_text()))


def notices(build, extra=()):
    tags = fetch_tags()
    parts = [NOTICES_HEAD]
    for name, fetch, licence, url, file in (*NOTICES, *extra):
        path = build / "_deps" / f"{fetch.lower()}-src" / file if fetch else REPO / file
        title = name.format(tag=tags.get(fetch))
        parts.append(f"\n{'=' * 78}\n{title} ({licence})\n{url}\n{'=' * 78}\n\n")
        parts.append(path.read_text(encoding="utf-8").strip() + "\n")
    return "".join(parts)


def write_text(path, text, newline):
    path.write_bytes(text.replace("\r\n", "\n").replace("\n", newline).encode("utf-8"))


def pe_imports(data):
    if data[:2] != b"MZ" or len(data) < 0x40:
        raise ValueError("not a PE image")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("not a PE image")
    count, opt_size = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
    opt = pe + 24
    dirs = opt + (112 if struct.unpack_from("<H", data, opt)[0] == 0x20B else 96)
    sections = [struct.unpack_from("<IIII", data, opt + opt_size + 40 * i + 8) for i in range(count)]

    def at(rva):
        for vsize, va, rawsize, raw in sections:
            if va <= rva < va + max(vsize, rawsize):
                return raw + rva - va
        raise ValueError("an import lies outside the sections")

    names = []
    present = struct.unpack_from("<I", data, dirs - 4)[0]
    for index, size, field in ((1, 20, 12), (13, 32, 4)):
        rva = struct.unpack_from("<I", data, dirs + 8 * index)[0] if index < present else 0
        while rva:
            name = struct.unpack_from("<I", data, at(rva) + field)[0]
            if not name:
                break
            start = at(name)
            names.append(data[start:data.index(b"\0", start)].decode("ascii"))
            rva += size
    return names


def libraries(programs, folders):
    found, todo = {}, list(programs)
    while todo:
        user = todo.pop()
        for name in pe_imports(user.read_bytes()):
            key = name.lower()
            if key in found or key.removesuffix(".dll") in WINDOWS or key.startswith("api-ms-win-"):
                continue
            lib = next((p for d in folders for p in sorted(d.glob("*.dll")) if p.name.lower() == key), None)
            if lib is None:
                raise ValueError(f"{user.name} loads {name}: no Windows library, and not in "
                                 + ", ".join(map(str, folders)))
            found[key] = lib
            todo.append(lib)
    return [found[key] for key in sorted(found)]


def stage(build, platform, out, runtime=None):
    spec = PLATFORMS[platform]
    build = Path(build)
    dest = Path(out) / (PREFIX + platform)
    shutil.rmtree(dest, ignore_errors=True)
    dest.mkdir(parents=True)
    programs = [build / name for name in spec["programs"]]
    ship = programs + [build / name for name in spec["files"]]
    try:
        if platform.startswith("windows"):
            if runtime is None:
                redist = os.environ.get("VCToolsRedistDir")
                runtime = sorted(Path(redist).glob(RUNTIME)) if redist else []
            ship += libraries(programs, [build, *runtime])
        for src in ship:
            shutil.copy2(src, dest / src.name)
        (dest / "shaders").mkdir()
        for name in SHADERS:
            shader = build / "shaders" / name
            if not shader.is_file():
                raise ValueError(f"missing shader: {shader}")
            shutil.copy2(shader, dest / "shaders" / name)
        if spec["symbols"]:
            symbols = Path(out) / (PREFIX + platform + SYMBOLS)
            shutil.rmtree(symbols, ignore_errors=True)
            symbols.mkdir(parents=True)
            for name in spec["symbols"]:
                shutil.copy2(build / name, symbols / name)
    except (OSError, ValueError, struct.error) as e:
        print(f"error: {e}", file=sys.stderr)
        return False
    newline = spec["newline"]
    write_text(dest / "LICENSE.txt", (REPO / "LICENSE").read_text(encoding="utf-8"), newline)
    readme = (REPO / "assets/dist/README.txt").read_text(encoding="utf-8")
    write_text(dest / "README.txt", readme.replace("{version}", version(full=True)), newline)
    write_text(dest / "THIRD_PARTY_NOTICES.txt", notices(build, spec["notices"]), newline)
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
        programs = PLATFORMS[platform.removesuffix(SYMBOLS)]["programs"]
        if not deny_gamedata.check(deny_gamedata.files_under([str(src)])):
            return False
        files = sorted(p for p in src.rglob("*") if p.is_file())
        if platform.startswith("windows"):
            path = out / f"{name}-{platform}.zip"
            with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
                for f in files:
                    z.write(f, f"{ROOT}/{f.relative_to(src).as_posix()}")
        else:
            path = out / f"{name}-{platform}.tar.gz"
            with tarfile.open(path, "w:gz") as t:
                for f in files:
                    info = t.gettarinfo(f, f"{ROOT}/{f.relative_to(src).as_posix()}")
                    info.mode = 0o755 if f.name in programs else 0o644  # artifacts drop the executable bit
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
