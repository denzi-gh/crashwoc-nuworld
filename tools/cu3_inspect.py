"""Inspect DC Super-Villains PC CU3 timeline headers and their SUB sidecars."""
import argparse
from decimal import Decimal
import json
import math
from pathlib import Path
import re
import struct


HEADER = struct.Struct(">IIIIIf")
NUMBER = r"[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[Ee][+-]?\d+)?"
SUBTITLE = re.compile(
    rf'Subtitle\s+text\s+"([^"]+)"\s+start_time\s+({NUMBER})\s+end_time\s+({NUMBER})',
    re.IGNORECASE,
)


def inspect_timeline(path):
    with path.open("rb") as stream:
        header = stream.read(HEADER.size)
    if len(header) != HEADER.size:
        raise ValueError(f"{path}: incomplete CU3 timeline header")
    extent, marker, version, flags, frames, fps = HEADER.unpack(header)
    if (marker, version) != (1, 0x8000001E) or flags not in (0, 1):
        raise ValueError(f"{path}: unsupported CU3 envelope")
    if extent != path.stat().st_size - 4 or not math.isfinite(fps) or fps <= 0:
        raise ValueError(f"{path}: invalid extent or playback rate")
    report = {"clip": path.name, "version": 30, "frames": frames, "fps": fps}
    stem = path.stem
    if stem.upper().endswith("_DX11"):
        stem = stem[:-5]
    sub = path.with_name(stem + ".SUB")
    if not sub.is_file():
        report["subtitle_file"] = None
        return report
    report["subtitle_file"] = sub.name
    subtitles = []
    for line_no, line in enumerate(sub.read_text(encoding="utf-8-sig").splitlines(), 1):
        line = line.strip()
        if not line or line.startswith("//"):
            continue
        strings = re.fullmatch(r'StringsFile\s+"([^"]+)"', line, re.IGNORECASE)
        if strings and "strings_file" not in report:
            report["strings_file"] = strings[1]
            continue
        match = SUBTITLE.fullmatch(line)
        if not match or Decimal(match[3]) < Decimal(match[2]):
            raise ValueError(f"{sub}:{line_no}: unsupported subtitle entry")
        # Preserve the authored decimal spelling for DCC tools and exact round trips.
        subtitles.append({"text_key": match[1], "start_seconds": match[2], "end_seconds": match[3]})
    report["subtitles"] = subtitles
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("cu3", type=Path)
    args = parser.parse_args()
    try:
        print(json.dumps(inspect_timeline(args.cu3), indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
