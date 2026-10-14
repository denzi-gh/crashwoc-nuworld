# NuWorld

A native PC source port of Crash Bandicoot: The Wrath of Cortex (PS2, PAL v1.03), built on the
[crashwoc-decomp-ps2](https://github.com/denzi-gh/crashwoc-decomp-ps2) decompilation (`decomp/` submodule).

NuWorld contains no game data. It needs your own copy of the game (PAL, SLES-50386).

Status: early scaffold (window + OpenGL 3.3 context).

## Building

Clone with the submodule:

```sh
git clone --recursive https://github.com/denzi-gh/nuworld.git
```

Requirements: Clang, CMake 3.25+, Ninja. SDL3 is downloaded and built automatically
(pass `-DNUWORLD_SYSTEM_SDL3=ON` to use an installed one).

- **Windows:** use an MSYS2 CLANG64 shell with `mingw-w64-clang-x86_64-{clang,cmake,ninja}`.
- **Linux:** install clang, cmake, ninja and the SDL3 build dependencies (X11/Wayland, GL, audio dev packages).

```sh
cmake --preset release
cmake --build --preset release
./build/release/nuworld
```

## License

GPL-3.0, see [LICENSE](LICENSE).
