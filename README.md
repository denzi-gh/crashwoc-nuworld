# CrashWOC: NuWorld

A native PC source port of Crash Bandicoot: The Wrath of Cortex (PS2, PAL v1.03), built on the
[crashwoc-decomp-ps2](https://github.com/denzi-gh/crashwoc-decomp-ps2) decompilation (`decomp/` submodule).

CrashWOC: NuWorld contains no game data. It needs your own copy of the game (PAL, SLES-50386).

Status: early. The executable opens an SDL3 / OpenGL 3.3 window, can import and verify your disc
(see Game data) and links the whole decomp, whose data it builds from your disc (`--check-data`). The game
boots to its first loading screen on a 50 Hz vblank clock (`--boot`, `--boot-check`), and the decomp draws any
scene through the port's renderer (`--scene`).

## Building

Clone with the submodule:

```sh
git clone --recursive https://github.com/denzi-gh/crashwoc-nuworld.git
cd crashwoc-nuworld
```

Requirements: Clang, CMake 3.25+, Ninja, and Python 3.11+ with libclang (`python -m pip install -r
tools/requirements.txt`) for the generators that turn the decomp units into host sources. CMake takes the first
Python on the PATH; pick another with `-DPython3_EXECUTABLE=<python>`, or skip the decomp units with
`-DNUWORLD_DECOMP=OFF`. SDL3 is downloaded and built automatically (pass `-DNUWORLD_SYSTEM_SDL3=ON` to use an
installed one).

- **Windows:** use an MSYS2 CLANG64 shell with `mingw-w64-clang-x86_64-{clang,cmake,ninja}`.
- **Linux:** install clang, cmake, ninja and the SDL3 build dependencies (X11/Wayland, GL, audio dev packages).

```sh
cmake --preset release
cmake --build --preset release
./build/release/crashwoc-nuworld
ctest --test-dir build/release
```

On Windows, the executable is `build/release/crashwoc-nuworld.exe`.

## Changing the game

The `decomp/` submodule is the original game and is never edited here. What the port changes in its code lives in
`port/patches/`: unified diffs per concern, applied in the order of `series.toml`, with new files in `files/`. The
build copies the decomp to `build/<preset>/decomp` and applies them; the tools in `tools/` use `build/decomp`
(`python tools/patches.py apply`). Every hunk must match exactly, so a decomp update that touches patched code
stops the build at that patch.

Edit code, not diffs:

```sh
python tools/patches.py setup                # build/patch-work: the decomp, files, then one commit per concern
# edit the files in build/patch-work
python tools/patches.py fixup 70-widescreen  # fold the edits into that concern and rewrite port/patches
python tools/patches.py rebase <decomp sha>  # move the series and the submodule to another decomp commit
```

`fixup <name> --new "<reason>"` starts a concern at the end of the series. When `fixup` or `rebase` stops at a
conflict, resolve it in `build/patch-work`, `git add` the files, `git rebase --continue`, then run
`python tools/patches.py rebuild`.

Single retail values that a seam wraps are rules, not patches: `port/values/hz.toml` names each 50/60 Hz value
by file, function, value and which of its matches it is, and the build wraps it as `NW_HZ(pal, ntsc)` after the
patches. A decomp change that adds or removes such a value stops the build at its rule; one that only reorders
the function keeps the count, so every function with rules is locked too (below). `python tools/ntsc_diff.py
check` proves the NTSC values.

A patch must leave the PS2 build unchanged: host behaviour goes behind a `nu_port.h` seam or `#ifdef NW_HOST`.
`python tools/ee_gate.py` proves it (Docker with the decomp image and its toolchain): it compiles the decomp with the
EE compilers with and without the patches and compares the objects. Host-only blocks are invisible to it, so their
count per concern is kept in `port/ee_gate.toml`; change it together with the patch that needs it.

Changes to how the game plays or what it shows live in `src/gameplay/`, each behind a `[game]` setting in
`NwProfile` (`gameplay/gameplay.h`). With every switch off the profile is the retail game: only the windowed boot
fills it from `settings.toml`, so every check, test and pad-script run plays retail. A feature marks its code with
`NW_DEVIATION(<switch>)`, its `NwProfile` field, which stops the game if that code runs with the switch off.

To run code before, after or instead of a decomp function, list it under `wrap` in `port/units.toml` and define it
in NuWorld: the decomp's definition stays as `<name>__retail`, and NuWorld's takes every call, the decomp's own
included. Include the generated `<unit>.retail.h`: it declares `<name>__retail` and the decomp's prototype of
`<name>`, so the compiler holds NuWorld's definition to the decomp's signature. A file `static` that NuWorld code
must reach goes under `expose`: the generated wrapper drops its `static`, the name stays.

Files and assets are replaced without code: `--overlay DIR` (with `--boot`, up to four, the later wins) lays a
folder over the disc. Each file in it replaces the disc file of the same path, or, in a folder named after an
archive (`ATS.DAT/LEVELS/...`), that archive's entry; `SFX.DAT` is replaced only whole, the sound driver reads it
itself. An overlay only replaces: a file the disc lacks, the boot ELF, `SYSTEM.CNF`, the IRX modules and
`DUMMY.DAT` stop the start, and a file two overlays have is reported. The mod format with manifests (`mods/`) comes
with the engine rewrite.

The decomp functions NuWorld depends on by their text are locked in `port/copies.lock`: each override in
`port/units.toml` (NuWorld's copy) and each function with value rules. `python tools/copies.py --check` fails when
a decomp update changes one, so it gets a look (bring the copy in line, or run `python tools/ntsc_diff.py check`;
then `--update`), and when a port patch edits an overridden one: that edit never compiles. A wrap is not locked:
the decomp's C compiles as it is.

## Game data

Import your disc image once (`.iso`, or `.bin`/`.cue`; PAL v1.03 only):

```sh
./build/release/crashwoc-nuworld --import "Crash Bandicoot - The Wrath of Cortex (Europe).cue"
```

Every file is checked against a SHA-256 manifest and copied to the data folder: `%APPDATA%\crashwoc-nuworld\` on Windows,
`~/.local/share/crashwoc-nuworld/` on Linux (or `$XDG_DATA_HOME/crashwoc-nuworld/` when set),
or `user/` beside the executable when a `portable.txt` is there.
`--verify` re-checks the imported files; `--check-data` builds the game's data from the imported executable,
converts it back and checks that it gives the executable's bytes again; `--check-scenes` reads every `.gsc` scene
and decodes its geometry and textures, one line per file; `--check-disc` unpacks every disc file and every entry
of its `.DAT` archives, one SHA-256 line each; `--scene <path>` has the decomp load and draw one scene
(e.g. `LEVELS/B/HUB/HUB.GSC`) under a fly camera (WASD, Q/E, Shift, right mouse to look, wheel for speed);
`--scene-check <path|all>` draws a scene headless for a few frames and reports its frame packet (`all`: every scene);
`--boot` runs the game in a window that shows its frame count in the title; `--boot-check [vblanks]`
runs it headless to that many vblanks (default 50) after its first screen swap; both list, in the order the game first
reached them, the functions that have no C yet and the decomp C that is only a near match
(`python tools/stub_hits.py <saved output>...` orders the remaining decomp work by those lists); `--data-dir <dir>`
uses another folder.

## Saves

The game's memory card is the data folder's `saves/`: its four save slots are `saves/BESLES-50386slot00.toml`, a
TOML file in which levels are keyed by their path and hubs by their number, so that mods can change or add levels
without breaking a save. Keys the port does not know are kept when it saves, and the previous file stays as `.bak`.
`--import-save <file>` imports a PS2 save: a raw `BESLES-50386slot00`, or a PCSX2 memory card (`.ps2`) holding one.
Without a save, the first load also imports one that is in `saves/`.

## License

GPL-3.0, see [LICENSE](LICENSE).
