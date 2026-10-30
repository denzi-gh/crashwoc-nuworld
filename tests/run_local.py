"""Runner of the local-only ctests: skipped (77) unless NUWORLD_TEST_DATA names <data root>/disc, which fills {root}.

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
