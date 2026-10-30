#!/usr/bin/env python3
"""ctest decomp-closure: every retail name the decomp objects leave undefined must come from the port or HOST_LIBC.

The link proves it resolves; this catches a missing rename binding a host library (kernel32's CreateThread, CRT rand).
"""
from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tomllib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import nwunits  # noqa: E402

# strtod and strtol: glibc's stdlib.h inlines atof and atoi into them.
HOST_LIBC = frozenset("""
    abort abs atof atoi calloc exit fflush fprintf fputs free fwrite getenv malloc memcmp memcpy memmove memset
    printf putchar puts qsort realloc snprintf sprintf strcasecmp strcat strchr strcmp strcpy strlen strncasecmp
    strncmp strncpy strrchr strstr strtod strtol tolower toupper vfprintf vsnprintf vsprintf
""".split())


def retail_names(decomp: Path) -> set[str]:
    cfg = decomp / "config" / nwunits.VERSION
    names = {f["name"] for f in tomllib.loads((cfg / "functions.toml").read_text())["function"]}
    names |= {s["name"] for s in tomllib.loads((cfg / "data_symbols.toml").read_text())["symbol"]}
    for line in (cfg / "symbol_addrs.txt").read_text().splitlines():
        m = re.match(r"\s*([A-Za-z_][\w.$]*)\s*=", line)
        if m:
            names.add(m.group(1))
    return names


def symbols(nm: str, objects: list[str]) -> tuple[set[str], set[str]]:
    run = subprocess.run([nm, "--format=posix", *objects], capture_output=True, text=True)
    if run.returncode != 0:
        raise SystemExit(f"{nm} failed: {run.stderr.strip()}")
    defined, undefined = set(), set()
    for line in run.stdout.splitlines():
        parts = line.split()
        if len(parts) < 2 or parts[0].endswith(":"):
            continue
        name = parts[0].removeprefix("__imp_")
        (undefined if parts[1] in ("U", "u") else defined).add(name)
    return defined, undefined


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--nm", required=True, help="llvm-nm")
    parser.add_argument("--decomp", type=Path, default=None,
                        help="decomp tree (default: the patched tree build/decomp, refreshed first)")
    parser.add_argument("--objects", type=Path, required=True,
                        help="file listing the objects of the linked game, one per line")
    args = parser.parse_args()
    args.decomp = nwunits.decomp_tree(args.decomp)

    objects = [line for line in args.objects.read_text().splitlines() if line.strip()]
    defined, undefined = symbols(args.nm, objects)
    retail = retail_names(args.decomp)
    host = sorted(undefined - defined)
    stray = [n for n in host if (n in retail or re.fullmatch(r"D_[0-9A-Fa-f]{8}", n)) and n not in HOST_LIBC]
    if stray:
        print("retail names the port does not define (they would bind to a host library): "
              + ", ".join(stray), file=sys.stderr)
        return 1
    print(f"{len(objects)} objects; from the host: {', '.join(host)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
