#!/usr/bin/env python3
"""Port patches over the decomp: the patched tree every host tool reads.

The decomp submodule stays retail. apply copies its files (LF line endings) to
the patched tree, adds port/patches/files/ and applies the concerns of
port/patches/series.toml in order. A concern is a directory of
<decomp path>.patch files (unified diffs); every hunk must match exactly, it
may move but is never fuzzed. verify proves the series between two decomp
commits; import writes a concern from a decomp diff.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import tomllib
from dataclasses import dataclass, field
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PRISTINE = REPO / "decomp"
TREE = REPO / "build" / "decomp"
PATCHES = REPO / "port" / "patches"
STAMP = ".nw-tree.json"
SCHEMA = 1
HUNK = re.compile(rb"^@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@(.*)$")


class PatchError(Exception):
    pass


@dataclass
class Hunk:
    old_start: int
    new_start: int
    context: str
    old: list[bytes] = field(default_factory=list)
    new: list[bytes] = field(default_factory=list)


@dataclass
class Patch:
    origin: Path
    path: str
    hunks: list[Hunk]


@dataclass
class Concern:
    name: str
    reason: str
    patches: list[Patch]


def normalize(data: bytes) -> bytes:
    """LF line endings for text; binary files (a NUL byte) stay as they are."""
    return data if b"\0" in data else data.replace(b"\r\n", b"\n")


def git(decomp: Path, *args: str) -> bytes:
    return subprocess.run(["git", "-C", str(decomp), *args], capture_output=True, check=True).stdout


def decomp_sha(decomp: Path) -> str | None:
    if not (decomp / ".git").exists():
        return None
    try:
        return git(decomp, "rev-parse", "HEAD").decode().strip() or None
    except (OSError, subprocess.CalledProcessError):
        return None


def worktree_files(decomp: Path) -> dict[str, bytes]:
    """The checkout's files, committed or not (ignored ones excepted), local edits included."""
    if (decomp / ".git").exists():
        try:
            names = git(decomp, "ls-files", "-z", "-co", "--exclude-standard").decode().split("\0")
        except (OSError, subprocess.CalledProcessError) as e:
            detail = getattr(e, "stderr", b"").decode(errors="replace").strip() or e
            raise PatchError(f"git ls-files in {decomp} failed: {detail}") from None
    else:
        names = [p.relative_to(decomp).as_posix() for p in decomp.rglob("*")
                 if p.is_file() and ".git" not in p.relative_to(decomp).parts]
    out = {}
    for name in names:
        p = decomp / name
        if name and p.is_file():
            out[name] = normalize(p.read_bytes())
    return out


def commit_files(decomp: Path, rev: str) -> dict[str, bytes]:
    """The files of a decomp commit, from the object store (no checkout)."""
    listing = git(decomp, "ls-tree", "-r", "-z", rev).split(b"\0")
    blobs = []
    for entry in listing:
        if not entry:
            continue
        meta, name = entry.split(b"\t", 1)
        mode, kind, sha = meta.split()
        if kind == b"blob":
            blobs.append((name.decode(), sha))
    proc = subprocess.run(["git", "-C", str(decomp), "cat-file", "--batch"], capture_output=True, check=True,
                          input=b"".join(sha + b"\n" for _, sha in blobs))
    out, data, pos = {}, proc.stdout, 0
    for name, sha in blobs:
        header_end = data.index(b"\n", pos)
        size = int(data[pos:header_end].split()[2])
        start = header_end + 1
        out[name] = normalize(data[start:start + size])
        pos = start + size + 1
    return out


def parse_patch(origin: Path, path: str) -> Patch:
    data = origin.read_bytes()
    if b"\r" in data:
        raise PatchError(f"{origin}: CR line endings (patches are LF, see .gitattributes)")
    lines = data.split(b"\n")
    if len(lines) < 2 or not lines[0].startswith(b"--- ") or not lines[1].startswith(b"+++ "):
        raise PatchError(f"{origin}: not a unified diff of one file")
    hunks: list[Hunk] = []
    i = 2
    while i < len(lines):
        line = lines[i]
        if not line:
            i += 1
            continue
        m = HUNK.match(line)
        if not m:
            raise PatchError(f"{origin}:{i + 1}: expected a hunk header")
        old_len = int(m.group(2) or b"1")
        new_len = int(m.group(4) or b"1")
        h = Hunk(int(m.group(1)), int(m.group(3)), m.group(5).decode(errors="replace").strip())
        i += 1
        last = None
        while len(h.old) < old_len or len(h.new) < new_len:
            if i >= len(lines):
                raise PatchError(f"{origin}: hunk at old line {h.old_start} is truncated")
            body = lines[i] or b" "  # a blank context line whose space an editor dropped
            tag, text = body[:1], body[1:] + b"\n"
            if tag == b" ":
                h.old.append(text)
                h.new.append(text)
                last = (h.old, h.new)
            elif tag == b"-":
                h.old.append(text)
                last = (h.old,)
            elif tag == b"+":
                h.new.append(text)
                last = (h.new,)
            else:
                raise PatchError(f"{origin}:{i + 1}: bad hunk line")
            i += 1
            if i < len(lines) and lines[i].startswith(b"\\"):
                for side in last:
                    side[-1] = side[-1][:-1]
                i += 1
        hunks.append(h)
    return Patch(origin, path, hunks)


def apply_patch(patch: Patch, data: bytes) -> bytes:
    """Every hunk where the patch puts it, or at its one other exact match; nothing fuzzy."""
    old = data.splitlines(keepends=True)
    out: list[bytes] = []
    pos = offset = 0
    for n, h in enumerate(patch.hunks, 1):
        size = len(h.old)
        want = h.old_start - 1 + offset if size else h.old_start + offset
        if pos <= want and old[want:want + size] == h.old:
            at = want
        else:
            found = [p for p in range(pos, len(old) - size + 1) if old[p:p + size] == h.old]
            if len(found) != 1:
                where = f"hunk {n} (old line {h.old_start}{', ' + h.context if h.context else ''})"
                why = "matches nowhere" if not found else f"matches {len(found)} places"
                raise PatchError(f"{patch.origin}: {where} {why} in {patch.path}")
            at = found[0]
        out.extend(old[pos:at])
        out.extend(h.new)
        pos = at + size
        offset = at - (h.old_start - 1 if size else h.old_start)
    out.extend(old[pos:])
    return b"".join(out)


def load_series(patches: Path = PATCHES) -> tuple[list[Concern], dict[str, bytes]]:
    """The concerns in order and the new files (port/patches/files/)."""
    files = {}
    for p in sorted((patches / "files").rglob("*")):
        if p.is_file():
            files[p.relative_to(patches / "files").as_posix()] = normalize(p.read_bytes())
    series = patches / "series.toml"
    if not series.is_file():
        return [], files
    doc = tomllib.loads(series.read_text(encoding="utf-8"))
    if doc.get("schema") != SCHEMA:
        raise PatchError(f"{series}: schema {doc.get('schema')}, expected {SCHEMA}")
    concerns, seen = [], set()
    for entry in doc.get("concern", []):
        name = entry["name"]
        if name in seen or name == "files" or not (patches / name).is_dir():
            raise PatchError(f"{series}: concern {name!r} is repeated, reserved or has no directory")
        seen.add(name)
        items = []
        for p in sorted((patches / name).rglob("*.patch")):
            items.append(parse_patch(p, p.relative_to(patches / name).as_posix()[:-len(".patch")]))
        concerns.append(Concern(name, entry.get("reason", ""), items))
    stray = {p.name for p in patches.iterdir() if p.is_dir()} - seen - {"files"}
    if stray:
        raise PatchError(f"{series}: directories not in the series: {', '.join(sorted(stray))}")
    return concerns, files


def patch_tree(files: dict[str, bytes], patches: Path = PATCHES) -> dict[str, bytes]:
    """The decomp files with the series applied."""
    concerns, new = load_series(patches)
    tree = dict(files)
    for name, data in new.items():
        if name in tree:
            raise PatchError(f"{patches / 'files' / name}: the decomp has this file; patch it instead")
        tree[name] = data
    for c in concerns:
        for p in c.patches:
            if p.path not in tree:
                raise PatchError(f"{p.origin}: {p.path} is not in the decomp")
            tree[p.path] = apply_patch(p, tree[p.path])
    return tree


def series_hash(patches: Path = PATCHES) -> str:
    h = hashlib.sha256()
    for p in sorted(patches.rglob("*")):
        if p.is_file():
            h.update(p.relative_to(patches).as_posix().encode() + b"\0")
            h.update(normalize(p.read_bytes()) + b"\0")
    return h.hexdigest()


def _replace(tmp: Path, dest: Path) -> None:
    for attempt in range(5):
        try:
            os.replace(tmp, dest)
            return
        except PermissionError:  # an editor or indexer holding the file on Windows
            if attempt == 4:
                raise
            time.sleep(0.2 * (attempt + 1))


def write_tree(tree: dict[str, bytes], out: Path, stamp: dict) -> int:
    """Writes the files that changed, removes the ones a previous apply wrote and this one does not."""
    old_stamp = out / STAMP
    previous = set()
    if old_stamp.is_file():
        previous = set(json.loads(old_stamp.read_text(encoding="utf-8")).get("files", []))
    written = 0
    for name, data in tree.items():
        dest = out / name
        if dest.is_file() and dest.read_bytes() == data:
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        tmp = dest.with_name(dest.name + ".nw-tmp")
        tmp.write_bytes(data)
        _replace(tmp, dest)
        written += 1
    for name in sorted(previous - tree.keys()):
        (out / name).unlink(missing_ok=True)
    stamp = {**stamp, "files": sorted(tree)}
    text = json.dumps(stamp, indent=1) + "\n"
    if not old_stamp.is_file() or old_stamp.read_text(encoding="utf-8") != text:
        old_stamp.write_text(text, encoding="utf-8", newline="\n")
    return written


def apply(decomp: Path = PRISTINE, out: Path = TREE, patches: Path = PATCHES) -> tuple[int, int]:
    """Builds or refreshes the patched tree; returns (files, written)."""
    tree = patch_tree(worktree_files(decomp), patches)
    stamp = {"schema": SCHEMA, "decomp": decomp_sha(decomp), "series": series_hash(patches)}
    return len(tree), write_tree(tree, out, stamp)


def link_compiler(tree: Path, decomp: Path = PRISTINE) -> None:
    """The submodule's EE toolchain (setup_toolchain.py) under a patched tree, for the decomp's cc.py (Linux)."""
    link, target = tree / "compiler", decomp / "compiler"
    if not link.exists() and target.is_dir():
        link.symlink_to(target.resolve(), target_is_directory=True)


def tree_id(tree: Path) -> str | None:
    """The decomp commit and series a patched tree was built from (None for a plain checkout)."""
    stamp = tree / STAMP
    if not stamp.is_file():
        return None
    doc = json.loads(stamp.read_text(encoding="utf-8"))
    return f"{doc.get('decomp')}+{doc.get('series', '')[:12]}"


def cmd_apply(args) -> int:
    files, written = apply(args.decomp, args.out, args.patches)
    print(f"{files} files in {args.out}, {written} written")
    return 0


def cmd_verify(args) -> int:
    """The series on --base gives --expect, file for file (--exclude: paths left out of both)."""
    got = patch_tree(commit_files(args.decomp, args.base), args.patches)
    want = commit_files(args.decomp, args.expect)
    for name in args.exclude:
        got.pop(name, None)
        want.pop(name, None)
    bad = sorted(n for n in got.keys() | want.keys() if got.get(n) != want.get(n))
    for n in bad:
        state = "missing" if n not in got else "extra" if n not in want else "differs"
        print(f"{state}: {n}")
    print(f"{len(want)} files, {len(bad)} different")
    return 1 if bad else 0


def cmd_import(args) -> int:
    """Writes --concern from the decomp diff --base..--host: new files to files/, changes as patches."""
    names = git(args.decomp, "diff", "--name-status", "-z", "--no-renames", args.base, args.host).split(b"\0")
    new_files = commit_files(args.decomp, args.host)
    target = args.patches / args.concern
    count = 0
    for status, name in zip(names[0::2], names[1::2]):
        path = name.decode()
        if path in args.exclude:
            continue
        if status == b"A":
            dest = args.patches / "files" / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(new_files[path])
        elif status == b"M":
            diff = git(args.decomp, "diff", "--no-color", "--no-ext-diff", "--no-renames", "-U3",
                       args.base, args.host, "--", path)
            body = diff[diff.index(b"\n--- ") + 1:]
            dest = target / f"{path}.patch"
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(normalize(body))
        else:
            raise PatchError(f"{path}: status {status.decode()} (only added and changed files)")
        count += 1
    print(f"{count} files into {target} and {args.patches / 'files'}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--decomp", type=Path, default=PRISTINE, help="the decomp checkout (default: decomp/)")
    parser.add_argument("--patches", type=Path, default=PATCHES, help="the series (default: port/patches)")
    sub = parser.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("apply", help="build or refresh the patched tree")
    p.add_argument("--out", type=Path, default=TREE, help="the patched tree (default: build/decomp)")
    p.set_defaults(fn=cmd_apply)
    p = sub.add_parser("verify", help="the series on one decomp commit gives another")
    p.add_argument("--base", required=True)
    p.add_argument("--expect", required=True)
    p.add_argument("--exclude", nargs="*", default=[])
    p.set_defaults(fn=cmd_verify)
    p = sub.add_parser("import", help="write a concern from a decomp diff")
    p.add_argument("--base", required=True)
    p.add_argument("--host", required=True)
    p.add_argument("--concern", required=True)
    p.add_argument("--exclude", nargs="*", default=[])
    p.set_defaults(fn=cmd_import)
    args = parser.parse_args()
    try:
        return args.fn(args)
    except PatchError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
