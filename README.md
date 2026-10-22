# CrashWOC: NuWorld

A native PC source port of Crash Bandicoot: The Wrath of Cortex (PS2, PAL v1.03), built on the
[crashwoc-decomp-ps2](https://github.com/denzi-gh/crashwoc-decomp-ps2) decompilation (`decomp/` submodule).

CrashWOC: NuWorld contains no game data. It needs your own copy of the game (PAL, SLES-50386).

Status: early. The executable opens an SDL3 / OpenGL 3.3 window, can import and verify your disc
(see Game data) and links the whole decomp, whose data it builds from your disc (`--check-data`). The game
boots to its first loading screen on a 50 Hz vblank clock (`--boot`, `--boot-check`) but draws nothing yet.

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

## Game data

Import your disc image once (`.iso`, or `.bin`/`.cue`; PAL v1.03 only):

```sh
./build/release/crashwoc-nuworld --import "Crash Bandicoot - The Wrath of Cortex (Europe).cue"
```

Every file is checked against a SHA-256 manifest and copied to the data folder: `%APPDATA%\crashwoc-nuworld\` on Windows,
`~/.local/share/crashwoc-nuworld/` on Linux (or `$XDG_DATA_HOME/crashwoc-nuworld/` when set),
or `user/` beside the executable when a `portable.txt` is there.
`--verify` re-checks the imported files; `--check-data` builds the game's data from the imported executable,
converts it back and checks that it gives the executable's bytes again; `--boot` runs the game in a window that shows its frame count in the title; `--boot-check [vblanks]`
runs it headless to that many vblanks (default 50) after its first screen swap and lists the functions it reached
that have no C yet; `--data-dir <dir>` uses another folder.

## License

GPL-3.0, see [LICENSE](LICENSE).
