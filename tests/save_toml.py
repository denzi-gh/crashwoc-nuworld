"""The saves the C tests wrote (src/save/savecodec.h), read with Python's tomllib as a mod tool would: every *.toml of
each folder parses and follows the save schema, and each folder has one.

    python tests/save_toml.py <dir>...
"""
import sys
import tomllib
from pathlib import Path

SLOT = ("lives", "wumpas", "mask", "powerbits", "percent", "crystals", "relics", "crate_gems", "bonus_gems", "gems",
        "gembits")
OPTIONS = ("vibration", "surround", "sfx_volume", "music_volume", "language")


def byte_text(value, size: int) -> bool:
    return isinstance(value, str) and len(value) <= size and all(ord(c) <= 0xFF for c in value)


def in_range(value, lo: int, hi: int) -> bool:
    return isinstance(value, int) and not isinstance(value, bool) and lo <= value <= hi


def check(path: Path) -> list[str]:
    doc = tomllib.loads(path.read_text(encoding="utf-8"))
    errors = []
    if doc.get("format") != 1:
        errors.append("format is not 1")
    slots = doc.get("slot", {})
    if not {"0", "1", "2", "3"} <= set(slots):
        errors.append(f"slots {sorted(slots)}")
    for index, slot in slots.items():
        where = f"slot.{index}"
        if not isinstance(slot.get("empty"), (bool, int)) or not byte_text(slot.get("name"), 9):
            errors.append(f"{where}: empty or name")
        errors += [f"{where}.{k}" for k in SLOT if not in_range(slot.get(k), 0, 0xFF)]
        if not in_range(slot.get("cutbits"), 0, 0xFFFFFFFF):
            errors.append(f"{where}.cutbits")
        options = slot.get("options", {})
        errors += [f"{where}.options.{k}" for k in OPTIONS if not in_range(options.get(k), 0, 0xFF)]
        errors += [f"{where}.options.{k}" for k in ("screen_x", "screen_y") if not in_range(options.get(k), -128, 127)]
        for key, hub in slot.get("hub", {}).items():
            if not in_range(hub.get("flags"), 0, 0xFF) or not in_range(hub.get("crystals"), 0, 0xFF):
                errors.append(f"{where}.hub.{key}")
        for key, level in slot.get("level", {}).items():
            times = level.get("time", [])
            if not in_range(level.get("flags"), 0, 0xFFFF) or len(times) != 3 or not all(
                    len(t) == 2 and byte_text(t[0], 4) and in_range(t[1], 0, 0xFFFFFFFF) for t in times):
                errors.append(f"{where}.level.{key}")
    return errors


def main() -> int:
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    failed = False
    for folder in map(Path, sys.argv[1:]):
        files = sorted(folder.glob("*.toml"))
        if not files:
            print(f"{folder}: no saves", file=sys.stderr)
            failed = True
        for path in files:
            errors = check(path)
            if errors:
                print(f"{path}: {', '.join(errors[:10])}", file=sys.stderr)
                failed = True
            else:
                print(f"{path}: ok")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
