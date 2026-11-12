<p align="center">
  <img src="assets/branding/nuworld-icon.svg" width="144" alt="NuWorld">
</p>

<h1 align="center">CrashWOC: NuWorld</h1>

<p align="center">
  A native PC port of <i>Crash Bandicoot: The Wrath of Cortex</i> (PS2, PAL v1.03)
</p>

<p align="center">
  <a href="#nuworld-first-startup">First startup</a> ·
  <a href="#building">Building</a> ·
  <a href="#settings">Settings</a> ·
  <a href="https://discord.gg/kmCPpW4KvJ">Discord</a> ·
  <a href="https://github.com/denzi-gh/crashwoc-decomp-ps2">Decompilation</a>
</p>

<p align="center">
  <a href="https://github.com/denzi-gh/crashwoc-nuworld/actions/workflows/build.yml"><img src="https://github.com/denzi-gh/crashwoc-nuworld/actions/workflows/build.yml/badge.svg" alt="build"></a>
  <a href="https://decomp.dev/denzi-gh/crashwoc-decomp-ps2"><img src="https://decomp.dev/denzi-gh/crashwoc-decomp-ps2.svg?mode=shield&label=decompiled" alt="decompiled"></a>
  <img src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-informational" alt="platform">
  <a href="LICENSE"><img src="https://img.shields.io/github/license/denzi-gh/crashwoc-nuworld" alt="license"></a>
  <a href="https://discord.gg/kmCPpW4KvJ"><img src="https://img.shields.io/badge/discord-join-5865F2?logo=discord&logoColor=white" alt="discord"></a>
</p>

---

NuWorld is not an emulator. It runs the game's own code, rebuilt from the
[decompilation](https://github.com/denzi-gh/crashwoc-decomp-ps2), natively on PC. It contains no game data: you
need your own copy of the game (PAL, SLES-50386, v1.03).

> [!WARNING]
> NuWorld is **not** meant to be played yet, even though most things already work. The first release will be the
> movement beta. Issues and pull requests are closed until then.

## NuWorld Features

- 50 or 60 Hz, any aspect ratio, frame interpolation
- Window, fullscreen and borderless, resolution scale, MSAA
- Remappable gamepad and keyboard, rumble, button prompts
- Readable saves, import of PS2 and PCSX2 saves
- *many Quality of Life changes and additions*
- **new native Gameplay features**

## NuWorld first startup

On the first start NuWorld asks for your disc image (`.iso`, or `.bin`/`.cue`), checks it and imports it. After that
the game starts right away; `--setup` opens the import again. On Windows, `--console` shows the log in a console
window.

## Building

Needs Clang, CMake 3.25+, Ninja and Python 3.11+ (`python -m pip install -r tools/requirements.txt`). On Windows,
use an MSYS2 CLANG64 shell.

```sh
git clone --recursive https://github.com/denzi-gh/crashwoc-nuworld.git
cd crashwoc-nuworld
cmake --preset release
cmake --build --preset release
```

## Settings

| Key | |
|---|---|
| F1 | Menu: video, audio, controls, game speed |
| F2 | Crash's movement |
| F11 | Fullscreen |
| F12 | Screenshot |

> [!NOTE]
> The settings menu is a debug menu. A proper in-game menu comes with the first release.

## Saves

After entering a new game's name, choose whether to enable autosave. Each playthrough has its own automatic slot,
separate from the 100 manual slots; the choice can be changed in the pause menu's Options. Autosave records earned
level/boss rewards, confirmed time trial names and changes in the hub. Loading resumes from the hub, with the last
completed save's progress. Progress inside an unfinished level is not restored.

In Load/Delete, use the shoulder buttons (or their bound keys) to switch between manual and automatic saves. Manual
saving never overwrites an automatic slot. Loading older progress asks before replacing a newer autosave.

While an autosave is writing, its notice blinks; completion and error notices stay steady. Both manual and automatic
saves are verified before atomic replacement, synchronized to disk and keep two previous versions. A damaged main
file falls back to a valid backup; previous versions can also be selected from the load confirmation. A failed or
interrupted write leaves the last completed save available.

Manual writes, automatic writes, backup rotation and destination searches use one persistent low-priority I/O
worker. Gameplay only queues immutable snapshots and polls completion; it does not wait for disk flushes. Save-menu
draws use cached slot availability. Save-page reads still occur when opening or changing a page.

Save files remain TOML. New writes include a length/CRC integrity comment on the first line. When deliberately
editing a save, remove that line so it is read as a legacy editable document; the next save restores protection.
Back up the file before editing. Existing saves and PS2 imports remain readable and start with autosave disabled.

## Discord

Updates on NuWorld are posted on [Discord](https://discord.gg/kmCPpW4KvJ).

## Credits

- [Kenney](https://kenney.nl) Input Prompts (CC0)
- Atkinson Hyperlegible (SIL OFL)
- [SDL3](https://libsdl.org),
- [Dear ImGui](https://github.com/ocornut/imgui)
- SPU2 interpolation table from [PCSX2](https://pcsx2.net).

NuWorld is a fan project and not affiliated with Activision or Traveller's Tales.

## License

GPL-3.0, see [LICENSE](LICENSE).
