"""Runner of the local-only ctests (game data never reaches CI): exits 77, which ctest reports as skipped, unless
NUWORLD_TEST_DATA names an imported disc folder (<data root>/disc). Otherwise runs the command with {root}
replaced by that data root and exits with its code.

    run_local.py <program> [args with {root}]
"""
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    disc = os.environ.get("NUWORLD_TEST_DATA")
    if not disc:
        print("NUWORLD_TEST_DATA is not set: skipped")
        return 77
    root = str(Path(disc).resolve().parent)
    return subprocess.run([arg.replace("{root}", root) for arg in sys.argv[1:]]).returncode


if __name__ == "__main__":
    sys.exit(main())
