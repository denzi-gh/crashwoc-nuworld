<p align="center">
  <img src="assets/branding/nuworld-icon.svg" width="144" alt="NuWorld">
</p>

<h1 align="center">CrashWOC: NuWorld</h1>

<p align="center">
  A native PC port of <i>Crash Bandicoot: The Wrath of Cortex</i> (PS2)
</p>

<p align="center">
  <a href="#download">Download</a> ·
  <a href="#first-startup">First startup</a> ·
  <a href="#settings">Settings</a> ·
  <a href="#building">Building</a> ·
  <a href="https://discord.gg/kmCPpW4KvJ">Discord</a> ·
  <a href="https://github.com/denzi-gh/crashwoc-decomp-ps2">Decompilation</a>
</p>

<p align="center">
  <a href="https://github.com/denzi-gh/crashwoc-nuworld/releases/latest"><img src="https://img.shields.io/github/v/release/denzi-gh/crashwoc-nuworld?label=release" alt="release"></a>
  <a href="https://github.com/denzi-gh/crashwoc-nuworld/actions/workflows/build.yml"><img src="https://github.com/denzi-gh/crashwoc-nuworld/actions/workflows/build.yml/badge.svg" alt="build"></a>
  <a href="https://decomp.dev/denzi-gh/crashwoc-decomp-ps2"><img src="https://decomp.dev/denzi-gh/crashwoc-decomp-ps2.svg?mode=shield&label=decompiled" alt="decompiled"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-informational" alt="platform">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/denzi-gh/crashwoc-nuworld" alt="license"></a>
  <a href="https://discord.gg/kmCPpW4KvJ"><img src="https://img.shields.io/badge/discord-join-5865F2?logo=discord&logoColor=white" alt="discord"></a>
</p>

---

NuWorld is not an emulator or a static recomp. It runs the game's own code, rebuilt from the
[decompilation](https://github.com/denzi-gh/crashwoc-decomp-ps2), natively on PC. It contains no game data: you
need your own copy of the game, PAL v1.03 (SLES-50386) or USA v1.00 (SLUS-20238).

> [!NOTE]
> This is the **movement beta**. The whole game runs from start to end, but the Beta aims to find the right controls for the future release.
> Pull requests are open to collaborators only for now.

## Features

- N. Hanced Movement for Crash, Coco and every vehicle, tunable in the game, or the original movement
- 50 or 60 Hz, any aspect ratio, frame interpolation, draw distance up to 400 %
- Window, fullscreen and borderless, resolution scale, MSAA, enhanced gems and relics
- Remappable gamepad and keyboard, rumble, button prompts
- Autosave next to 100 manual slots, readable saves, import from and export to PCSX2 memory cards
- Air indicator, boss health, camera stability, longer time trial names, the loading minigame in the hub

## Download

Get the latest build from [Releases](https://github.com/denzi-gh/crashwoc-nuworld/releases/latest).

| Platform | Package | Start |
|---|---|---|
| Windows (x64) | `CrashWOC-NuWorld-v…-windows-x64.zip` | `NuWorld.exe` |
| Linux (x86_64) | `CrashWOC-NuWorld-v…-linux-x86_64.tar.gz` | `./NuWorld` |

- Windows: the exe is not signed, so Windows may warn about an unrecognised app: *More info*, then *Run anyway*.
- Linux: needs glibc 2.35 or newer (Ubuntu 22.04 or later) and OpenGL 3.3.

## First startup

On the first start NuWorld asks for your disc image (`.iso`, or `.bin`/`.cue`), checks it and imports it. After that
the game starts right away.

### Data folder

Settings (`settings.toml`), saves, screenshots and the log (`nuworld.log`) are kept in

| | |
|---|---|
| Windows | `%APPDATA%\crashwoc-nuworld\` |
| Linux | `~/.local/share/crashwoc-nuworld/` (`$XDG_DATA_HOME`) |

## Settings

| Key | |
|---|---|
| F1 | Menu: settings, gameplay, saves, N. Hanced Movement, help |
| F2 | Crash's movement (F3 to F10: Coco and the vehicles) |
| F11 | Fullscreen |
| F12 | Screenshot |

Changes are saved to `settings.toml`.

> [!NOTE]
> The settings menu is a debug menu. Proper in-game settings screens come with v1.0.

## Feedback

Found a bug, or something about the movement feels wrong? Open an issue with one of the
[forms](https://github.com/denzi-gh/crashwoc-nuworld/issues/new/choose) (Beta Feedback, Bug report, Crash / freeze, ...).
In the game, F1 > Help opens them and copies your system info for the report. Updates are posted on
[Discord](https://discord.gg/kmCPpW4KvJ).

## Building

Needs Clang, CMake 3.25+, Ninja and Python 3.11+. On Windows,
use an MSYS2 CLANG64 shell. On Linux, SDL3 is built from source and needs the X11, Wayland and audio development
packages that the `linux` job of [build.yml](.github/workflows/build.yml) installs.

```sh
git clone --recursive https://github.com/denzi-gh/crashwoc-nuworld.git
cd crashwoc-nuworld
cmake --preset release
cmake --build --preset release
```

## Credits

- [Kenney](https://kenney.nl) Input Prompts (CC0)
- Atkinson Hyperlegible (SIL OFL)
- [SDL3](https://libsdl.org)
- [Dear ImGui](https://github.com/ocornut/imgui)
- SPU2 interpolation table and reverb filter from [PCSX2](https://pcsx2.net)


## License

GPL-3.0, see [LICENSE](LICENSE).
