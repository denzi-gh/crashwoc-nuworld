CrashWOC: NuWorld {version}
A native PC port of Crash Bandicoot: The Wrath of Cortex (PS2), built on its decompilation.
https://github.com/denzi-gh/crashwoc-nuworld

NuWorld contains no game data. You need your own disc image (.iso, or .bin/.cue) of the
PS2 game: PAL v1.03 (SLES-50386) or USA v1.00 (SLUS-20238).

Starting
  Windows: run NuWorld.exe. The exe is not signed, so Windows may warn about an
  unrecognised app: choose "More info", then "Run anyway".
  Linux: run ./NuWorld. Needs glibc 2.35 or newer (Ubuntu 22.04 or later) and OpenGL 3.3.
  On the first start NuWorld asks for the disc image, checks it and imports it.

Keys
  F1   menu: settings, saves, N. Hanced Movement, help
  F2   Crash's movement
  F11  fullscreen
  F12  screenshot

Data folder (settings.toml, saves, screenshots, nuworld.log)
  Windows: %APPDATA%\crashwoc-nuworld\
  Linux:   ~/.local/share/crashwoc-nuworld/
  Portable: put an empty portable.txt next to the executable; the data then goes to
  user/ beside it.

Feedback and bugs
  F1 > Help in the game, or https://github.com/denzi-gh/crashwoc-nuworld/issues
  Discord: https://discord.gg/kmCPpW4KvJ

Linux app menu (optional)
  Copy NuWorld to ~/.local/bin/, nuworld.png to ~/.local/share/icons/hicolor/256x256/apps/
  and nuworld.desktop to ~/.local/share/applications/.

License
  GPL-3.0, see LICENSE.txt. Third-party licences: THIRD_PARTY_NOTICES.txt.
  NuWorld is a fan project and not affiliated with Activision or Traveller's Tales.
